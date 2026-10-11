// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_DISPLAY_EDID_H
#define MTVK_DISPLAY_EDID_H
#include "display/control.h"
#define MTVK_EDID_BLOCK_BYTES 128u
#define MTVK_EDID_MAX_BLOCKS 32u
#define MTVK_EDID_MAX_BYTES (MTVK_EDID_BLOCK_BYTES * MTVK_EDID_MAX_BLOCKS)
#define MTVK_EDID_NAME_BYTES 14u
#define MTVK_EDID_DEPTH_BIT(depth) (UINT32_C(1) << (depth))
typedef struct mtvk_edid_info {
    char manufacturer[4], name[MTVK_EDID_NAME_BYTES];
    uint16_t product, year, width_mm, height_mm;
    uint32_t serial, preferred_mode_id, depth_mask;
    uint8_t revision, interface_type, extension_count, hdr_eotfs, hdr_descriptors;
    /* CTA encoded desired luminance: not mastering metadata / not cd/m2.
     * Bits 0/1/2 of luminance_present indicate max/average/min bytes present. */
    uint8_t luminance_present, luminance_max, luminance_average, luminance_min;
    bool digital, bt2020_rgb;
    uint32_t mode_count, omitted_timings, unhandled_blocks;
    mtvk_display_mode modes[MTVK_DISPLAY_MAX_MODES];
} mtvk_edid_info;
MTVK_EXTERN_C_BEGIN
/* Exact base-declared block count, bounded, checksummed immutable byte snapshot.
 * Failure leaves out unchanged. Unknown constructs counted, never fabricated.
 * EDID 1.3/1.4, base/CTA DTD, selected progressive VICs, DisplayID I/VII.
 * This parses sink information; it does NOT assert hardware/link capability. */
mtvk_status mtvk_edid_parse(const void *bytes, size_t size, mtvk_edid_info *out);
/* Intersect parsed complete timings with backend-verified RGB capabilities.
 * No mode synthesis, depth/HDR guessing or automatic connect. Unknown sink
 * depth -> NOT_SUPPORTED. Missing intersection -> NOT_FOUND. Transactional out.
 * Backend must still validate per-mode bandwidth/depth and current sink identity. */
mtvk_status mtvk_edid_select_capabilities(const mtvk_edid_info *sink,
    const mtvk_display_capabilities *hardware, mtvk_display_capabilities *out);
MTVK_EXTERN_C_END
#endif
