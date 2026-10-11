// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_PROBE_INPUT_H
#define MTVK_PROBE_INPUT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define MTVK_PROBE_SHADER_LIMIT (1024u * 1024u)
#define MTVK_PROBE_SPIRV_BOUND_LIMIT (1024u * 1024u)
bool mtvk_probe_parse_vendor(const char *text, uint32_t *vendor);
/* Structural checks only, not semantic validation. Generated shaders must also
 * pass spirv-val for Vulkan 1.2 before execution. */
bool mtvk_probe_spirv_valid(const uint32_t *words, size_t size);
/* Caller frees the returned allocation. No ownership is returned on failure. */
uint32_t *mtvk_probe_read_shader(const char *path, size_t *size);
#endif
