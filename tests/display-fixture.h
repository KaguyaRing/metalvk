// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_TEST_DISPLAY_FIXTURE_H
#define MTVK_TEST_DISPLAY_FIXTURE_H
#include "display/init.h"
#include <string.h>
static inline mtvk_display_capabilities display_caps(void) {
    mtvk_display_capabilities caps;
    memset(&caps, 0, sizeof(caps));
    caps.mode_count = 1;
    caps.transfers = MTVK_DISPLAY_TRANSFERS_ALL;
    caps.gamuts = MTVK_DISPLAY_GAMUTS_ALL;
    caps.min_depth = 8; caps.max_depth = 12; caps.static_metadata = true;
    const mtvk_display_mode mode = {1, 1920, 1080, 2200, 1125, 2008, 2052, 1084, 1089, 148500, true, true};
    caps.modes[0] = mode;
    return caps;
}
static inline mtvk_display_config display_sdr(void) {
    mtvk_display_config config;
    memset(&config, 0, sizeof(config));
    config.mode_id = 1; config.depth = 8;
    return config;
}
static inline mtvk_display_config display_hdr(void) {
    mtvk_display_config config = display_sdr();
    config.depth = 10; config.transfer = MTVK_DISPLAY_PQ; config.gamut = MTVK_DISPLAY_BT2020;
    config.metadata_present = true;
    const mtvk_display_hdr metadata = {{34000, 13250, 7500}, {16000, 34500, 3000},
        15635, 16450, 50, 1000000, 1000, 400};
    config.metadata = metadata;
    return config;
}
#endif
