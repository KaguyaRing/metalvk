// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_EDID_INTERNAL_H
#define MTVK_EDID_INTERNAL_H
#include "display/edid.h"
enum {
    EDID_DTD_BYTES = 18, EDID_BASE_DTD_START = 54, EDID_BASE_DTD_COUNT = 4,
    EDID_CTA_TAG = 0x02, EDID_DISPLAYID_TAG = 0x70,
    EDID_CTA_DATA_START = 4, EDID_CHECKSUM_OFFSET = 127,
    EDID_DISPLAYID_HEADER_BYTES = 4, EDID_DISPLAYID_DATA_START = 5,
    EDID_DISPLAYID_TIMING_BYTES = 20
};
static inline uint32_t edid_le16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static inline uint32_t edid_le24(const uint8_t *p) { return edid_le16(p) | ((uint32_t)p[2] << 16); }
static inline bool edid_zero(const uint8_t *p, size_t size) {
    for (size_t i = 0; i < size; ++i) if (p[i]) return false;
    return true;
}
bool mtvk_edid_checksum(const uint8_t *p, size_t size);
bool mtvk_edid_same_mode(const mtvk_display_mode *a, const mtvk_display_mode *b);
mtvk_status mtvk_edid_add_mode(mtvk_edid_info *info, mtvk_display_mode mode, bool preferred);
mtvk_status mtvk_edid_dtd(mtvk_edid_info *info, const uint8_t *p, bool preferred);
mtvk_status mtvk_edid_cta(mtvk_edid_info *info, const uint8_t *block);
mtvk_status mtvk_edid_cta_data(mtvk_edid_info *info, const uint8_t *p, size_t size);
mtvk_status mtvk_edid_displayid(mtvk_edid_info *info, const uint8_t *block);
mtvk_status mtvk_edid_displayid_timings(mtvk_edid_info *info, const uint8_t *p, size_t size,
                                      bool type7, uint8_t revision);
#endif
