// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_PROBE_PATTERN_H
#define MTVK_PROBE_PATTERN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define MTVK_PATTERN_EXTENT 64u
#define MTVK_PATTERN_CHANNELS 4u
#if defined(__cplusplus)
extern "C" {
#endif
/* Tightly packed RGBA8; even dimensions match the shader's quadrant split.
 * On failure, mismatch receives the first bad pixel, or SIZE_MAX for bad input. */
bool mtvk_probe_check_pattern(const uint8_t *rgba, size_t bytes, uint32_t width,
                             uint32_t height, size_t *mismatch);
#if defined(__cplusplus)
}
#endif
#endif
