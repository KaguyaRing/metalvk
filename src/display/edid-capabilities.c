// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "edid-internal.h"
#include "base/span.h"
mtvk_status mtvk_edid_select_capabilities(const mtvk_edid_info *sink,
    const mtvk_display_capabilities *hardware, mtvk_display_capabilities *out) {
    if (!sink || !hardware || !out || !mtvk_span_valid(sink, sizeof(*sink)) ||
        !mtvk_span_valid(hardware, sizeof(*hardware)) || !mtvk_span_valid(out, sizeof(*out)) ||
        mtvk_spans_overlap(sink, sizeof(*sink), out, sizeof(*out)) ||
        mtvk_spans_overlap(hardware, sizeof(*hardware), out, sizeof(*out)) ||
        sink->mode_count > MTVK_DISPLAY_MAX_MODES ||
        mtvk_display_validate_capabilities(hardware) != MTVK_OK) return MTVK_INVALID_ARGUMENT;
    if (!sink->digital || !sink->depth_mask) return MTVK_NOT_SUPPORTED;
    mtvk_display_capabilities selected = {0};
    for (unsigned depth = hardware->min_depth; depth <= hardware->max_depth; ++depth) {
        const uint32_t bit = MTVK_DISPLAY_DEPTH_BIT(depth);
        if (!(sink->depth_mask & bit) || (hardware->depth_mask && !(hardware->depth_mask & bit))) continue;
        selected.depth_mask |= bit;
        if (!selected.min_depth) selected.min_depth = (uint8_t)depth;
        selected.max_depth = (uint8_t)depth;
    }
    if (!selected.depth_mask) return MTVK_NOT_SUPPORTED;
    selected.transfers = hardware->transfers & MTVK_DISPLAY_TRANSFER_BIT(MTVK_DISPLAY_SDR);
    selected.gamuts = hardware->gamuts & MTVK_DISPLAY_GAMUT_BIT(MTVK_DISPLAY_BT709);
    if (sink->bt2020_rgb) selected.gamuts |= hardware->gamuts & MTVK_DISPLAY_GAMUT_BIT(MTVK_DISPLAY_BT2020);
    if (selected.max_depth >= MTVK_DISPLAY_HDR_DEPTH_MIN &&
        (selected.gamuts & MTVK_DISPLAY_GAMUT_BIT(MTVK_DISPLAY_BT2020))) {
        if (sink->hdr_eotfs & 4u) selected.transfers |= hardware->transfers & MTVK_DISPLAY_TRANSFER_BIT(MTVK_DISPLAY_PQ);
        if (sink->hdr_eotfs & 8u) selected.transfers |= hardware->transfers & MTVK_DISPLAY_TRANSFER_BIT(MTVK_DISPLAY_HLG);
    }
    selected.static_metadata = hardware->static_metadata && (sink->hdr_descriptors & 1u) &&
        (selected.transfers & MTVK_DISPLAY_TRANSFER_BIT(MTVK_DISPLAY_PQ));
    for (uint32_t i = 0; i < sink->mode_count; ++i) {
        uint64_t refresh = 0;
        if (mtvk_display_refresh_millihz(&sink->modes[i], &refresh) != MTVK_OK) return MTVK_INVALID_ARGUMENT;
        for (uint32_t j = 0; j < hardware->mode_count; ++j) {
            if (!mtvk_edid_same_mode(&sink->modes[i], &hardware->modes[j])) continue;
            if (selected.mode_count == MTVK_DISPLAY_MAX_MODES) return MTVK_LIMIT_EXCEEDED;
            selected.modes[selected.mode_count++] = hardware->modes[j];
            break;
        }
    }
    if (!selected.mode_count) return MTVK_NOT_FOUND;
    if (mtvk_display_validate_capabilities(&selected) != MTVK_OK) return MTVK_NOT_SUPPORTED;
    *out = selected;
    return MTVK_OK;
}
