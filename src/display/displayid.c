// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "edid-internal.h"
mtvk_status mtvk_edid_displayid_timings(mtvk_edid_info *info, const uint8_t *p, size_t size,
                                      bool type7, uint8_t revision) {
    const size_t stride = EDID_DISPLAYID_TIMING_BYTES + (type7 ? ((revision >> 4) & 7u) : 0u);
    if (!size || size % stride) return MTVK_INVALID_ARGUMENT;
    if ((revision & 7u) > 1 || (revision & (type7 ? 0x80u : 0xf8u))) {
        ++info->unhandled_blocks; return MTVK_OK;
    }
    for (size_t i = 0; i < size; i += stride) {
        const uint8_t *d = p + i;
        if ((d[3] & 0x70u) || stride != EDID_DISPLAYID_TIMING_BYTES) {
            ++info->omitted_timings; continue;
        }
        mtvk_display_mode mode = {0};
        mode.pixel_clock_khz = (edid_le24(d) + 1u) * (type7 ? 1u : 10u);
        mode.width = edid_le16(d + 4) + 1u;
        mode.htotal = mode.width + edid_le16(d + 6) + 1u;
        mode.hsync_start = mode.width + (edid_le16(d + 8) & 0x7fffu) + 1u;
        mode.hsync_end = mode.hsync_start + edid_le16(d + 10) + 1u;
        mode.height = edid_le16(d + 12) + 1u;
        mode.vtotal = mode.height + edid_le16(d + 14) + 1u;
        mode.vsync_start = mode.height + (edid_le16(d + 16) & 0x7fffu) + 1u;
        mode.vsync_end = mode.vsync_start + edid_le16(d + 18) + 1u;
        mode.hsync_positive = (d[9] & 0x80u) != 0;
        mode.vsync_positive = (d[17] & 0x80u) != 0;
        const mtvk_status status = mtvk_edid_add_mode(info, mode, (d[3] & 0x80u) != 0);
        if (status != MTVK_OK) return status;
    }
    return MTVK_OK;
}
mtvk_status mtvk_edid_displayid(mtvk_edid_info *info, const uint8_t *block) {
    const size_t end = EDID_DISPLAYID_DATA_START + (size_t)block[2];
    if (end >= EDID_CHECKSUM_OFFSET ||
        !mtvk_edid_checksum(block + 1, EDID_DISPLAYID_HEADER_BYTES + (size_t)block[2] + 1))
        return MTVK_INVALID_ARGUMENT;
    if (!edid_zero(block + end + 1, EDID_CHECKSUM_OFFSET - end - 1)) return MTVK_INVALID_ARGUMENT;
    if (block[1] != 0x12 && block[1] != 0x13 && block[1] != 0x20) {
        ++info->unhandled_blocks; return MTVK_OK;
    }
    for (size_t i = EDID_DISPLAYID_DATA_START; i < end;) {
        if (edid_zero(block + i, end - i)) break;
        if (end - i < 3 || (size_t)block[i + 2] > end - i - 3) return MTVK_INVALID_ARGUMENT;
        const uint8_t tag = block[i], revision = block[i + 1];
        const size_t size = block[i + 2];
        const uint8_t *data = block + i + 3;
        mtvk_status status = MTVK_OK;
        if ((tag == 0x03 && block[1] < 0x20) || (tag == 0x22 && block[1] == 0x20))
            status = mtvk_edid_displayid_timings(info, data, size, tag == 0x22, revision);
        else if (tag == 0x81 && !revision) status = mtvk_edid_cta_data(info, data, size);
        else ++info->unhandled_blocks;
        if (status != MTVK_OK) return status;
        i += 3 + size;
    }
    return MTVK_OK;
}
