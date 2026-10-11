// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "edid-internal.h"
#include "base/span.h"
bool mtvk_edid_checksum(const uint8_t *p, size_t size) {
    unsigned sum = 0;
    for (size_t i = 0; i < size; ++i) sum += p[i];
    return (sum & 255u) == 0;
}
bool mtvk_edid_same_mode(const mtvk_display_mode *a, const mtvk_display_mode *b) {
    return a->width == b->width && a->height == b->height && a->htotal == b->htotal &&
        a->vtotal == b->vtotal && a->hsync_start == b->hsync_start && a->hsync_end == b->hsync_end &&
        a->vsync_start == b->vsync_start && a->vsync_end == b->vsync_end &&
        a->pixel_clock_khz == b->pixel_clock_khz && a->hsync_positive == b->hsync_positive &&
        a->vsync_positive == b->vsync_positive;
}
mtvk_status mtvk_edid_add_mode(mtvk_edid_info *info, mtvk_display_mode mode, bool preferred) {
    mode.id = info->mode_count + 1;
    uint64_t refresh = 0;
    if (mtvk_display_refresh_millihz(&mode, &refresh) != MTVK_OK || !refresh) return MTVK_INVALID_ARGUMENT;
    for (uint32_t i = 0; i < info->mode_count; ++i) {
        if (!mtvk_edid_same_mode(&mode, &info->modes[i])) continue;
        if (preferred && !info->preferred_mode_id) info->preferred_mode_id = info->modes[i].id;
        return MTVK_OK;
    }
    if (info->mode_count == MTVK_DISPLAY_MAX_MODES) return MTVK_LIMIT_EXCEEDED;
    info->modes[info->mode_count++] = mode;
    if (preferred && !info->preferred_mode_id) info->preferred_mode_id = mode.id;
    return MTVK_OK;
}
mtvk_status mtvk_edid_dtd(mtvk_edid_info *info, const uint8_t *p, bool preferred) {
    if (!edid_le16(p)) return MTVK_OK;
    /* Interlace, stereo, borders and non-separate sync cannot be represented by
     * the current progressive RGB mode API. Record instead of misinterpreting. */
    if ((p[17] & 0x80u) || (p[17] & 0x61u) || (p[17] & 0x18u) != 0x18u || p[15] || p[16]) {
        ++info->omitted_timings;
        return MTVK_OK;
    }
    mtvk_display_mode mode = {0};
    mode.pixel_clock_khz = edid_le16(p) * 10u;
    mode.width = p[2] | ((uint32_t)(p[4] & 0xf0u) << 4);
    mode.height = p[5] | ((uint32_t)(p[7] & 0xf0u) << 4);
    mode.htotal = mode.width + p[3] + ((uint32_t)(p[4] & 15u) << 8);
    mode.vtotal = mode.height + p[6] + ((uint32_t)(p[7] & 15u) << 8);
    mode.hsync_start = mode.width + p[8] + ((uint32_t)(p[11] & 0xc0u) << 2);
    mode.hsync_end = mode.hsync_start + p[9] + ((uint32_t)(p[11] & 0x30u) << 4);
    mode.vsync_start = mode.height + (p[10] >> 4) + ((uint32_t)(p[11] & 0x0cu) << 2);
    mode.vsync_end = mode.vsync_start + (p[10] & 15u) + ((uint32_t)(p[11] & 3u) << 4);
    mode.hsync_positive = (p[17] & 2u) != 0;
    mode.vsync_positive = (p[17] & 4u) != 0;
    return mtvk_edid_add_mode(info, mode, preferred);
}
static void monitor_name(mtvk_edid_info *info, const uint8_t *p) {
    size_t count = 0;
    for (; count < MTVK_EDID_NAME_BYTES - 1; ++count) {
        const uint8_t c = p[count];
        if (c == '\n' || c == 0) break;
        info->name[count] = c >= 32 && c <= 126 ? (char)c : '?';
    }
    while (count && info->name[count - 1] == ' ') --count;
    info->name[count] = '\0';
}
mtvk_status mtvk_edid_parse(const void *bytes, size_t size, mtvk_edid_info *out) {
    if (!out || !bytes || !mtvk_span_valid(bytes, size) || !mtvk_span_valid(out, sizeof(*out)) ||
        mtvk_spans_overlap(bytes, size, out, sizeof(*out))) return MTVK_INVALID_ARGUMENT;
    if (size > MTVK_EDID_MAX_BYTES) return MTVK_LIMIT_EXCEEDED;
    if (size < MTVK_EDID_BLOCK_BYTES || size % MTVK_EDID_BLOCK_BYTES) return MTVK_INVALID_ARGUMENT;
    const uint8_t *p = bytes;
    const uint8_t header[] = {0, 255, 255, 255, 255, 255, 255, 0};
    for (size_t i = 0; i < sizeof(header); ++i) if (p[i] != header[i]) return MTVK_INVALID_ARGUMENT;
    if (((size_t)p[126] + 1) * MTVK_EDID_BLOCK_BYTES != size) return MTVK_INVALID_ARGUMENT;
    for (size_t i = 0; i < size; i += MTVK_EDID_BLOCK_BYTES)
        if (!mtvk_edid_checksum(p + i, MTVK_EDID_BLOCK_BYTES)) return MTVK_INVALID_ARGUMENT;
    if (p[18] != 1 || (p[19] != 3 && p[19] != 4)) return MTVK_INCOMPATIBLE_VERSION;
    const uint32_t vendor = ((uint32_t)p[8] << 8) | p[9];
    if (vendor & 0x8000u) return MTVK_INVALID_ARGUMENT;
    mtvk_edid_info info = {0};
    for (unsigned i = 0; i < 3; ++i) {
        const uint32_t code = (vendor >> (10u - i * 5u)) & 31u;
        if (!code || code > 26) return MTVK_INVALID_ARGUMENT;
        info.manufacturer[i] = (char)('A' + code - 1);
    }
    info.revision = p[19]; info.extension_count = p[126]; info.digital = (p[20] & 0x80u) != 0;
    info.product = (uint16_t)edid_le16(p + 10);
    info.serial = edid_le24(p + 12) | ((uint32_t)p[15] << 24);
    info.year = (uint16_t)(1990u + p[17]);
    // In EDID 1.4 one zero dimension encodes aspect ratio, not physical size.
    if (p[21] && p[22]) { info.width_mm = (uint16_t)(p[21] * 10u); info.height_mm = (uint16_t)(p[22] * 10u); }
    if (info.digital && info.revision == 4) {
        const unsigned depth_code = (p[20] >> 4) & 7u;
        info.interface_type = p[20] & 15u;
        if (depth_code == 7 || info.interface_type > 5) return MTVK_INVALID_ARGUMENT;
        if (depth_code) info.depth_mask = MTVK_EDID_DEPTH_BIT(4u + depth_code * 2u);
    }
    for (unsigned i = 0; i < 16; ++i) if (p[35 + i / 8] & (1u << (i % 8))) ++info.omitted_timings;
    if (p[37] & 0x80u) ++info.omitted_timings;
    for (unsigned i = 0; i < 8; ++i)
        if (p[38 + i * 2] != 1 || p[39 + i * 2] != 1) ++info.omitted_timings;
    for (unsigned i = 0; i < EDID_BASE_DTD_COUNT; ++i) {
        const uint8_t *d = p + EDID_BASE_DTD_START + i * EDID_DTD_BYTES;
        const mtvk_status status = mtvk_edid_dtd(&info, d, i == 0 && (p[24] & 2u));
        if (status != MTVK_OK) return status;
        if (!edid_le16(d) && d[2] == 0 && d[3] == 0xfc && d[4] == 0) monitor_name(&info, d + 5);
        else if (!edid_le16(d) && !edid_zero(d, EDID_DTD_BYTES)) ++info.unhandled_blocks;
    }
    unsigned displayid_blocks = 0;
    for (size_t i = MTVK_EDID_BLOCK_BYTES; i < size; i += MTVK_EDID_BLOCK_BYTES)
        if (p[i] == EDID_DISPLAYID_TAG) ++displayid_blocks;
    bool first_displayid = true;
    uint8_t displayid_version = 0;
    for (size_t i = MTVK_EDID_BLOCK_BYTES; i < size; i += MTVK_EDID_BLOCK_BYTES) {
        const uint8_t *block = p + i;
        mtvk_status status = MTVK_OK;
        if (block[0] == EDID_CTA_TAG) status = mtvk_edid_cta(&info, block);
        else if (block[0] == EDID_DISPLAYID_TAG) {
            if (first_displayid) {
                if (block[4] != displayid_blocks - 1) return MTVK_INVALID_ARGUMENT;
                first_displayid = false;
                displayid_version = block[1];
            } else if (block[4] || block[1] != displayid_version) return MTVK_INVALID_ARGUMENT;
            status = mtvk_edid_displayid(&info, block);
        }
        else ++info.unhandled_blocks;
        if (status != MTVK_OK) return status;
    }
    *out = info;
    return MTVK_OK;
}
