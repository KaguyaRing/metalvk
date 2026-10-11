// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "input.h"
#include "pattern.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); return false; \
} } while (0)

static bool test_vendor_rejects_invalid_or_out_of_range_input(void) {
    uint32_t vendor = UINT32_MAX;
    const char *invalid[] = {NULL, "", "-1", "+1", " 1", "1 ", "0x", "0x10000",
        "65536", "08", "0x10degarbage", "99999999999999999999999999"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        CHECK(!mtvk_probe_parse_vendor(invalid[i], &vendor));
        CHECK(vendor == 0);
    }
    CHECK(!mtvk_probe_parse_vendor("0", NULL));
    CHECK(mtvk_probe_parse_vendor("0x10de", &vendor) && vendor == 0x10de);
    CHECK(mtvk_probe_parse_vendor("0x1002", &vendor) && vendor == 0x1002);
    CHECK(mtvk_probe_parse_vendor("65535", &vendor) && vendor == UINT16_MAX);
    CHECK(mtvk_probe_parse_vendor("0", &vendor) && vendor == 0);
    return true;
}
static bool test_spirv_rejects_bad_headers_and_truncated_instructions(void) {
    /* OpNop tests structural parsing only, not semantic shader validity. */
    uint32_t code[] = {UINT32_C(0x07230203), UINT32_C(0x00010500), 0, 1, 0, UINT32_C(0x00010000)};
    CHECK(mtvk_probe_spirv_valid(code, sizeof(code)));
    CHECK(!mtvk_probe_spirv_valid(NULL, sizeof(code)));
    CHECK(!mtvk_probe_spirv_valid(code, 0));
    CHECK(!mtvk_probe_spirv_valid(code, sizeof(code) - 1));
    CHECK(!mtvk_probe_spirv_valid(code, MTVK_PROBE_SHADER_LIMIT + 4u));
    for (size_t i = 0; i < 5; ++i) {
        const uint32_t saved = code[i];
        if (i == 2) continue; // Generator identifier is unconstrained.
        code[i] = UINT32_MAX;
        CHECK(!mtvk_probe_spirv_valid(code, sizeof(code)));
        code[i] = saved;
    }
    code[3] = 0;
    CHECK(!mtvk_probe_spirv_valid(code, sizeof(code)));
    code[3] = 1;
    for (uint32_t length = 0; length <= UINT16_MAX; ++length) {
        code[5] = length << 16;
        CHECK(mtvk_probe_spirv_valid(code, sizeof(code)) == (length == 1));
    }
    return true;
}
static bool test_pattern_detects_every_corrupted_pixel_and_channel(void) {
    enum { Width = 8, Height = 6, Bytes = Width * Height * MTVK_PATTERN_CHANNELS };
    uint8_t rgba[Bytes];
    for (size_t i = 0; i < Width * Height; ++i) {
        rgba[i * 4] = i % Width >= Width / 2 ? UINT8_MAX : 0;
        rgba[i * 4 + 1] = i / Width >= Height / 2 ? UINT8_MAX : 0;
        rgba[i * 4 + 2] = 0;
        rgba[i * 4 + 3] = UINT8_MAX;
    }
    size_t mismatch = 0;
    CHECK(mtvk_probe_check_pattern(rgba, sizeof(rgba), Width, Height, &mismatch));
    CHECK(mismatch == SIZE_MAX);
    for (size_t i = 0; i < sizeof(rgba); ++i) {
        rgba[i] ^= 1;
        CHECK(!mtvk_probe_check_pattern(rgba, sizeof(rgba), Width, Height, &mismatch));
        CHECK(mismatch == i / MTVK_PATTERN_CHANNELS);
        rgba[i] ^= 1;
    }
    CHECK(!mtvk_probe_check_pattern(NULL, sizeof(rgba), Width, Height, &mismatch));
    CHECK(!mtvk_probe_check_pattern(rgba, sizeof(rgba) - 1, Width, Height, NULL));
    CHECK(!mtvk_probe_check_pattern(rgba, sizeof(rgba), 0, Height, NULL));
    CHECK(!mtvk_probe_check_pattern(rgba, sizeof(rgba), Width - 1, Height, NULL));
    CHECK(!mtvk_probe_check_pattern(rgba, sizeof(rgba), UINT32_MAX - 1u, UINT32_MAX - 1u, NULL));
    return true;
}
static bool test_shader_file_ownership_and_validation(const char *valid, const char *invalid) {
    size_t size = SIZE_MAX;
    CHECK(!mtvk_probe_read_shader(NULL, &size) && size == 0);
    CHECK(!mtvk_probe_read_shader("", &size) && size == 0);
    CHECK(!mtvk_probe_read_shader(valid, NULL));
    CHECK(!mtvk_probe_read_shader(invalid, &size) && size == 0);
    uint32_t *words = mtvk_probe_read_shader(valid, &size);
    CHECK(words && size > 20);
    const bool good = mtvk_probe_spirv_valid(words, size);
    free(words);
    CHECK(good);
    return true;
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (!test_vendor_rejects_invalid_or_out_of_range_input() ||
        !test_spirv_rejects_bad_headers_and_truncated_instructions() ||
        !test_pattern_detects_every_corrupted_pixel_and_channel() ||
        !test_shader_file_ownership_and_validation(argv[1], argv[0])) return 1;
    puts("PASS Vulkan input, SPIR-V structure, file ownership and full-image regression tests");
    return 0;
}
