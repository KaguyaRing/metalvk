// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "pattern.h"
/* Shared oracle for the independent Vulkan and system Metal clients. */
bool mtvk_probe_check_pattern(const uint8_t *rgba, size_t bytes, uint32_t width,
                             uint32_t height, size_t *mismatch) {
    if (mismatch) *mismatch = SIZE_MAX;
    if (!rgba || !width || !height || (width & 1u) || (height & 1u) ||
        width > SIZE_MAX / MTVK_PATTERN_CHANNELS / height) return false;
    const size_t pixels = (size_t)width * height;
    if (bytes != pixels * MTVK_PATTERN_CHANNELS) return false;
    for (size_t i = 0; i < pixels; ++i) {
        const uint8_t *pixel = rgba + i * MTVK_PATTERN_CHANNELS;
        const uint8_t red = i % width >= width / 2u ? UINT8_MAX : 0;
        const uint8_t green = i / width >= height / 2u ? UINT8_MAX : 0;
        if (pixel[0] != red || pixel[1] != green || pixel[2] != 0 || pixel[3] != UINT8_MAX) {
            if (mismatch) *mismatch = i;
            return false;
        }
    }
    return true;
}
