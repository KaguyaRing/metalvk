// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "display/init.h"
static bool mode_valid(const mtvk_display_mode *mode) {
    return mode && mode->id && mode->width && mode->height &&
        mode->width <= MTVK_DISPLAY_MAX_DIMENSION && mode->height <= MTVK_DISPLAY_MAX_DIMENSION &&
        mode->width <= mode->hsync_start && mode->hsync_start < mode->hsync_end &&
        mode->hsync_end <= mode->htotal && mode->htotal <= MTVK_DISPLAY_MAX_TOTAL &&
        mode->height <= mode->vsync_start && mode->vsync_start < mode->vsync_end &&
        mode->vsync_end <= mode->vtotal && mode->vtotal <= MTVK_DISPLAY_MAX_TOTAL &&
        mode->pixel_clock_khz && mode->pixel_clock_khz <= MTVK_DISPLAY_MAX_CLOCK_KHZ;
}
mtvk_status mtvk_display_refresh_millihz(const mtvk_display_mode *mode, uint64_t *out) {
    if (!out || !mode_valid(mode)) return MTVK_INVALID_ARGUMENT;
    *out = (uint64_t)mode->pixel_clock_khz * UINT64_C(1000000) /
        ((uint64_t)mode->htotal * mode->vtotal);
    return MTVK_OK;
}
mtvk_status mtvk_display_validate_capabilities(const mtvk_display_capabilities *caps) {
    if (!caps || !caps->mode_count || caps->mode_count > MTVK_DISPLAY_MAX_MODES ||
        !caps->transfers || (caps->transfers & ~MTVK_DISPLAY_TRANSFERS_ALL) ||
        !caps->gamuts || (caps->gamuts & ~MTVK_DISPLAY_GAMUTS_ALL) ||
        caps->min_depth < MTVK_DISPLAY_DEPTH_MIN || caps->min_depth > caps->max_depth ||
        caps->max_depth > MTVK_DISPLAY_DEPTH_MAX) return MTVK_INVALID_ARGUMENT;
    if (caps->depth_mask) {
        const uint32_t range = ((UINT32_C(1) << (caps->max_depth + 1u)) - 1u) &
            ~((UINT32_C(1) << caps->min_depth) - 1u);
        if (caps->depth_mask & ~range) return MTVK_INVALID_ARGUMENT;
    }
    const uint32_t hdr = MTVK_DISPLAY_TRANSFER_BIT(MTVK_DISPLAY_PQ) | MTVK_DISPLAY_TRANSFER_BIT(MTVK_DISPLAY_HLG);
    if ((caps->transfers & hdr) && (caps->max_depth < MTVK_DISPLAY_HDR_DEPTH_MIN ||
        !(caps->gamuts & MTVK_DISPLAY_GAMUT_BIT(MTVK_DISPLAY_BT2020)) ||
        (caps->depth_mask && !(caps->depth_mask &
            (MTVK_DISPLAY_DEPTHS_ALL & ~((UINT32_C(1) << MTVK_DISPLAY_HDR_DEPTH_MIN) - 1u))))))
        return MTVK_INVALID_ARGUMENT;
    if (caps->static_metadata && !(caps->transfers & MTVK_DISPLAY_TRANSFER_BIT(MTVK_DISPLAY_PQ)))
        return MTVK_INVALID_ARGUMENT;
    for (uint32_t i = 0; i < caps->mode_count; ++i) {
        if (!mode_valid(&caps->modes[i])) return MTVK_INVALID_ARGUMENT;
        for (uint32_t j = 0; j < i; ++j)
            if (caps->modes[i].id == caps->modes[j].id) return MTVK_INVALID_ARGUMENT;
    }
    return MTVK_OK;
}
static bool xy_valid(uint16_t x, uint16_t y) {
    return x && y && (uint32_t)x + y <= MTVK_DISPLAY_CHROMATICITY_SCALE;
}
static bool metadata_zero(const mtvk_display_hdr *m) {
    for (unsigned i = 0; i < 3; ++i) if (m->primary_x[i] || m->primary_y[i]) return false;
    return !m->white_x && !m->white_y && !m->min_luminance_milli && !m->max_luminance_milli &&
        !m->max_cll && !m->max_fall;
}
mtvk_status mtvk_display_validate_config(const mtvk_display_capabilities *caps,
                                       const mtvk_display_config *config) {
    if (mtvk_display_validate_capabilities(caps) != MTVK_OK || !config || !config->mode_id ||
        config->transfer < MTVK_DISPLAY_SDR || config->transfer > MTVK_DISPLAY_HLG ||
        config->gamut < MTVK_DISPLAY_BT709 || config->gamut > MTVK_DISPLAY_BT2020 ||
        config->depth < MTVK_DISPLAY_DEPTH_MIN || config->depth > MTVK_DISPLAY_DEPTH_MAX)
        return MTVK_INVALID_ARGUMENT;
    if (!(caps->transfers & MTVK_DISPLAY_TRANSFER_BIT(config->transfer)) ||
        !(caps->gamuts & MTVK_DISPLAY_GAMUT_BIT(config->gamut)) ||
        config->depth < caps->min_depth || config->depth > caps->max_depth ||
        (caps->depth_mask && !(caps->depth_mask & MTVK_DISPLAY_DEPTH_BIT(config->depth))))
        return MTVK_NOT_SUPPORTED;
    bool found = false;
    for (uint32_t i = 0; i < caps->mode_count; ++i) if (caps->modes[i].id == config->mode_id) found = true;
    if (!found) return MTVK_NOT_SUPPORTED;
    if (config->transfer != MTVK_DISPLAY_SDR &&
        (config->gamut != MTVK_DISPLAY_BT2020 || config->depth < MTVK_DISPLAY_HDR_DEPTH_MIN))
        return MTVK_INVALID_ARGUMENT;
    if (!config->metadata_present) return metadata_zero(&config->metadata) ? MTVK_OK : MTVK_INVALID_ARGUMENT;
    if (config->transfer != MTVK_DISPLAY_PQ) return MTVK_INVALID_ARGUMENT;
    if (!caps->static_metadata) return MTVK_NOT_SUPPORTED;
    const mtvk_display_hdr *m = &config->metadata;
    if (!m->max_luminance_milli || m->max_luminance_milli > MTVK_DISPLAY_MAX_LUMINANCE_MILLI ||
        m->min_luminance_milli >= m->max_luminance_milli ||
        (m->max_fall && (!m->max_cll || m->max_fall > m->max_cll)) ||
        !xy_valid(m->white_x, m->white_y)) return MTVK_INVALID_ARGUMENT;
    for (unsigned i = 0; i < 3; ++i) if (!xy_valid(m->primary_x[i], m->primary_y[i])) return MTVK_INVALID_ARGUMENT;
    return MTVK_OK;
}
