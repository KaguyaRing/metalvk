// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "edid-internal.h"
typedef struct vic_timing {
    uint8_t vic;
    uint16_t width, height, htotal, vtotal, hstart, hend, vstart, vend;
    uint32_t clock;
} vic_timing;
/* Selected CTA progressive 16:9 timings, not generated from resolution/Hz.
 * Unlisted VICs are explicitly counted; no CVT/GTF or aspect-ratio guessing. */
static const vic_timing vic_modes[] = {
    {4,1280,720,1650,750,1390,1430,725,730,74250},
    {16,1920,1080,2200,1125,2008,2052,1084,1089,148500},
    {19,1280,720,1980,750,1720,1760,725,730,74250},
    {31,1920,1080,2640,1125,2448,2492,1084,1089,148500},
    {32,1920,1080,2750,1125,2558,2602,1084,1089,74250},
    {33,1920,1080,2640,1125,2448,2492,1084,1089,74250},
    {34,1920,1080,2200,1125,2008,2052,1084,1089,74250},
    {63,1920,1080,2200,1125,2008,2052,1084,1089,297000},
    {64,1920,1080,2640,1125,2448,2492,1084,1089,297000},
    {93,3840,2160,5500,2250,5116,5204,2168,2178,297000},
    {94,3840,2160,5280,2250,4896,4984,2168,2178,297000},
    {95,3840,2160,4400,2250,4016,4104,2168,2178,297000},
    {96,3840,2160,5280,2250,4896,4984,2168,2178,594000},
    {97,3840,2160,4400,2250,4016,4104,2168,2178,594000}
};
static mtvk_status video(mtvk_edid_info *info, const uint8_t *p, size_t size) {
    if (!size) return MTVK_INVALID_ARGUMENT;
    for (size_t i = 0; i < size; ++i) {
        const unsigned vic = p[i] >= 193u ? p[i] : p[i] & 127u;
        bool matched = false;
        for (size_t j = 0; j < sizeof(vic_modes) / sizeof(vic_modes[0]); ++j) {
            const vic_timing *v = &vic_modes[j];
            if (v->vic != vic) continue;
            matched = true;
            const mtvk_display_mode mode = {0, v->width, v->height, v->htotal, v->vtotal,
                v->hstart, v->hend, v->vstart, v->vend, v->clock, true, true};
            const mtvk_status status = mtvk_edid_add_mode(info, mode, p[i] >= 129u && p[i] <= 192u);
            if (status != MTVK_OK) return status;
        }
        if (!matched) ++info->omitted_timings;
    }
    return MTVK_OK;
}
static mtvk_status extended(mtvk_edid_info *info, const uint8_t *p, size_t size) {
    if (!size) return MTVK_INVALID_ARGUMENT;
    if (p[0] == 5) {
        if (size < 3) return MTVK_INVALID_ARGUMENT;
        info->bt2020_rgb = info->bt2020_rgb || (p[1] & 0x80u) != 0;
    } else if (p[0] == 6) {
        if (size < 3 || size > 6 || !p[1] || (p[1] & 0xf0u)) return MTVK_INVALID_ARGUMENT;
        const uint8_t present = (uint8_t)((1u << (size - 3)) - 1u);
        const uint8_t max = size >= 4 ? p[3] : 0, average = size >= 5 ? p[4] : 0, min = size >= 6 ? p[5] : 0;
        if (info->hdr_eotfs && (info->hdr_eotfs != p[1] || info->hdr_descriptors != p[2] ||
            info->luminance_present != present || info->luminance_max != max ||
            info->luminance_average != average || info->luminance_min != min)) return MTVK_INVALID_ARGUMENT;
        info->hdr_eotfs = p[1]; info->hdr_descriptors = p[2]; info->luminance_present = present;
        info->luminance_max = max; info->luminance_average = average; info->luminance_min = min;
    } else if (p[0] == 0x22) {
        if (size < 2) return MTVK_INVALID_ARGUMENT;
        return mtvk_edid_displayid_timings(info, p + 2, size - 2, true, p[1]);
    } else ++info->unhandled_blocks;
    return MTVK_OK;
}
mtvk_status mtvk_edid_cta_data(mtvk_edid_info *info, const uint8_t *p, size_t size) {
    for (size_t i = 0; i < size;) {
        if (!p[i]) return edid_zero(p + i, size - i) ? MTVK_OK : MTVK_INVALID_ARGUMENT;
        const unsigned tag = p[i] >> 5;
        const size_t length = p[i] & 31u;
        if (length > size - i - 1) return MTVK_INVALID_ARGUMENT;
        const uint8_t *data = p + i + 1;
        mtvk_status status = MTVK_OK;
        if (tag == 2) status = video(info, data, length);
        else if (tag == 7) status = extended(info, data, length);
        else if (tag == 3) {
            if (length < 3) return MTVK_INVALID_ARGUMENT;
            if (edid_le24(data) == 0x000c03u) {
                if (length < 5) return MTVK_INVALID_ARGUMENT;
                info->depth_mask |= MTVK_EDID_DEPTH_BIT(8);
                if (length >= 6) {
                    if (data[5] & 0x10u) info->depth_mask |= MTVK_EDID_DEPTH_BIT(10);
                    if (data[5] & 0x20u) info->depth_mask |= MTVK_EDID_DEPTH_BIT(12);
                    if (data[5] & 0x40u) info->depth_mask |= MTVK_EDID_DEPTH_BIT(16);
                }
            } else ++info->unhandled_blocks;
        } else ++info->unhandled_blocks;
        if (status != MTVK_OK) return status;
        i += length + 1;
    }
    return MTVK_OK;
}
mtvk_status mtvk_edid_cta(mtvk_edid_info *info, const uint8_t *block) {
    if (block[1] != 3) { ++info->unhandled_blocks; return MTVK_OK; }
    const size_t start = block[2];
    if (!start) return edid_zero(block + EDID_CTA_DATA_START, EDID_CHECKSUM_OFFSET - EDID_CTA_DATA_START) ?
        MTVK_OK : MTVK_INVALID_ARGUMENT;
    if (start < EDID_CTA_DATA_START || start > EDID_CHECKSUM_OFFSET) return MTVK_INVALID_ARGUMENT;
    mtvk_status status = mtvk_edid_cta_data(info, block + EDID_CTA_DATA_START, start - EDID_CTA_DATA_START);
    if (status != MTVK_OK) return status;
    size_t pos = start;
    unsigned count = 0;
    while (EDID_CHECKSUM_OFFSET - pos >= EDID_DTD_BYTES && edid_le16(block + pos)) {
        status = mtvk_edid_dtd(info, block + pos, count < (block[3] & 15u));
        if (status != MTVK_OK) return status;
        ++count; pos += EDID_DTD_BYTES;
    }
    if (count < (block[3] & 15u) || !edid_zero(block + pos, EDID_CHECKSUM_OFFSET - pos))
        return MTVK_INVALID_ARGUMENT;
    return MTVK_OK;
}
