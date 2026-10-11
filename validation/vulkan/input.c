// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "input.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

bool mtvk_probe_parse_vendor(const char *text, uint32_t *vendor) {
    if (!vendor) return false;
    *vendor = 0;
    if (!text || text[0] < '0' || text[0] > '9') return false;
    char *end = NULL;
    errno = 0;
    const unsigned long value = strtoul(text, &end, 0);
    if (errno || end == text || *end || value > UINT16_MAX) return false;
    *vendor = (uint32_t)value;
    return true;
}
bool mtvk_probe_spirv_valid(const uint32_t *words, size_t size) {
    const size_t header_words = 5;
    if (!words || size <= header_words * sizeof(*words) || size > MTVK_PROBE_SHADER_LIMIT ||
        size % sizeof(*words)) return false;
    if (words[0] != UINT32_C(0x07230203) || words[1] < UINT32_C(0x00010000) ||
        words[1] > UINT32_C(0x00010500) || (words[1] & UINT32_C(0xff0000ff)) ||
        !words[3] || words[3] > MTVK_PROBE_SPIRV_BOUND_LIMIT || words[4]) return false;
    const size_t count = size / sizeof(*words);
    for (size_t i = header_words; i < count;) {
        const size_t length = words[i] >> 16;
        if (!length || length > count - i) return false;
        i += length;
    }
    return true;
}
uint32_t *mtvk_probe_read_shader(const char *path, size_t *size) {
    if (!size) return NULL;
    *size = 0;
    if (!path || !*path) return NULL;
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    uint32_t *words = NULL;
    size_t bytes = 0;
    if (fseek(file, 0, SEEK_END) != 0) goto done;
    const long length = ftell(file);
    if (length < 0 || (unsigned long)length > MTVK_PROBE_SHADER_LIMIT ||
        length <= 20 || length % 4 != 0 || fseek(file, 0, SEEK_SET) != 0) goto done;
    bytes = (size_t)length;
    words = malloc(bytes);
    if (!words || fread(words, 1, bytes, file) != bytes || fgetc(file) != EOF ||
        ferror(file) || !mtvk_probe_spirv_valid(words, bytes)) {
        free(words); words = NULL;
    }
done:
    if (fclose(file) != 0) { free(words); words = NULL; }
    if (words) *size = bytes;
    return words;
}
