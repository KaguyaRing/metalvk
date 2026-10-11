// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
// Constructed wire fixtures, not evidence of an operating display link.
#include "display-fixture.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
static void checksum(uint8_t *p, size_t size) {
    unsigned sum = 0;
    p[size - 1] = 0;
    for (size_t i = 0; i < size - 1; ++i) sum += p[i];
    p[size - 1] = (uint8_t)(0u - sum);
}
static void le16(uint8_t *p, uint32_t value) { p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8); }
static void dtd(uint8_t *p, mtvk_display_mode m) {
    memset(p, 0, 18);
    le16(p, m.pixel_clock_khz / 10);
    const uint32_t hb = m.htotal - m.width, vb = m.vtotal - m.height;
    const uint32_t hf = m.hsync_start - m.width, hs = m.hsync_end - m.hsync_start;
    const uint32_t vf = m.vsync_start - m.height, vs = m.vsync_end - m.vsync_start;
    p[2] = (uint8_t)m.width; p[3] = (uint8_t)hb;
    p[4] = (uint8_t)(((m.width >> 8) << 4) | (hb >> 8));
    p[5] = (uint8_t)m.height; p[6] = (uint8_t)vb;
    p[7] = (uint8_t)(((m.height >> 8) << 4) | (vb >> 8));
    p[8] = (uint8_t)hf; p[9] = (uint8_t)hs;
    p[10] = (uint8_t)((vf << 4) | vs);
    p[11] = (uint8_t)(((hf >> 8) << 6) | ((hs >> 8) << 4) | ((vf >> 4) << 2) | (vs >> 4));
    p[17] = (uint8_t)(0x18u | (m.hsync_positive ? 2u : 0u) | (m.vsync_positive ? 4u : 0u));
}
static void base(uint8_t *p, uint8_t extensions) {
    memset(p, 0, 128);
    memset(p + 1, 255, 6);
    p[8] = 0x52; p[9] = 0x74; // TST
    p[10] = 0x34; p[11] = 0x12; p[17] = 36;
    p[18] = 1; p[19] = 4; p[20] = 0xb5; p[21] = 52; p[22] = 29; p[24] = 2;
    memset(p + 38, 1, 16);
    dtd(p + 54, display_caps().modes[0]);
    p[75] = 0xfc; memcpy(p + 77, "Test Monitor\n", 13);
    p[126] = extensions;
    checksum(p, 128);
}
static void cta(uint8_t *p) {
    memset(p, 0, 128);
    p[0] = 2; p[1] = 3; p[2] = 26;
    const uint8_t blocks[] = {0x43, 16, 4, 1, 0xe3, 5, 0x80, 0,
        0xe6, 6, 5, 1, 80, 40, 1, 0x66, 3, 12, 0, 16, 0, 0x30};
    memcpy(p + 4, blocks, sizeof(blocks)); checksum(p, 128);
}
static void displayid(uint8_t *p, bool type7) {
    memset(p, 0, 128);
    p[0] = 0x70; p[1] = type7 ? 0x20 : 0x13; p[2] = 23; p[3] = 2;
    p[5] = type7 ? 0x22 : 3; p[7] = 20;
    uint8_t *t = p + 8;
    const uint32_t clock = (type7 ? 594000u : 59400u) - 1u;
    le16(t, clock); t[2] = (uint8_t)(clock >> 16); t[3] = 0x84;
    le16(t + 4, 3839); le16(t + 6, 559); le16(t + 8, 0x8000u | 175u); le16(t + 10, 87);
    le16(t + 12, 2159); le16(t + 14, 89); le16(t + 16, 0x8000u | 7u); le16(t + 18, 9);
    checksum(p + 1, 28); checksum(p, 128);
}
int main(void) {
    uint8_t bytes[384]; memset(bytes, 0, sizeof(bytes));
    base(bytes, 1); cta(bytes + 128);
    mtvk_edid_info info, before;
    CHECK(mtvk_edid_parse(bytes, 256, &info) == MTVK_OK);
    CHECK(strcmp(info.manufacturer, "TST") == 0 && strcmp(info.name, "Test Monitor") == 0);
    CHECK(info.product == 0x1234 && info.year == 2026 && info.width_mm == 520);
    CHECK(info.mode_count == 2 && info.preferred_mode_id == 1 && info.omitted_timings == 1);
    CHECK(info.hdr_eotfs == 5 && info.hdr_descriptors == 1 && info.bt2020_rgb);
    CHECK(info.luminance_present == 7 && info.luminance_max == 80 && info.luminance_min == 1);
    CHECK(info.modes[0].hsync_start == 2008 && info.modes[0].pixel_clock_khz == 148500);
    const mtvk_display_capabilities hardware = display_caps();
    mtvk_display_capabilities selected;
    CHECK(mtvk_edid_select_capabilities(&info, &hardware, &selected) == MTVK_OK);
    CHECK(selected.mode_count == 1 && selected.modes[0].id == hardware.modes[0].id && selected.static_metadata);
    CHECK(selected.depth_mask == (MTVK_DISPLAY_DEPTH_BIT(8) | MTVK_DISPLAY_DEPTH_BIT(10) | MTVK_DISPLAY_DEPTH_BIT(12)));
    mtvk_display_config config = display_hdr();
    CHECK(mtvk_display_validate_config(&selected, &config) == MTVK_OK);
    config = display_sdr(); config.depth = 9;
    CHECK(mtvk_display_validate_config(&selected, &config) == MTVK_NOT_SUPPORTED);
    mtvk_display_capabilities restricted = hardware;
    restricted.modes[0].id = 42;
    CHECK(mtvk_edid_select_capabilities(&info, &restricted, &selected) == MTVK_OK);
    CHECK(selected.modes[0].id == 42);
    restricted.modes[0].pixel_clock_khz += 1;
    CHECK(mtvk_edid_select_capabilities(&info, &restricted, &selected) == MTVK_NOT_FOUND);
    restricted = hardware;
    restricted.depth_mask = MTVK_DISPLAY_DEPTH_BIT(8);
    CHECK(mtvk_display_validate_capabilities(&restricted) == MTVK_INVALID_ARGUMENT);
    restricted.transfers = MTVK_DISPLAY_TRANSFER_BIT(MTVK_DISPLAY_SDR);
    restricted.static_metadata = false;
    CHECK(mtvk_edid_select_capabilities(&info, &restricted, &selected) == MTVK_OK);
    CHECK(selected.min_depth == 8 && selected.max_depth == 8 && !selected.static_metadata &&
        selected.transfers == MTVK_DISPLAY_TRANSFER_BIT(MTVK_DISPLAY_SDR));
    info.bt2020_rgb = false;
    CHECK(mtvk_edid_select_capabilities(&info, &hardware, &selected) == MTVK_OK);
    CHECK(selected.gamuts == MTVK_DISPLAY_GAMUT_BIT(MTVK_DISPLAY_BT709) &&
        selected.transfers == MTVK_DISPLAY_TRANSFER_BIT(MTVK_DISPLAY_SDR) && !selected.static_metadata);
    CHECK(mtvk_edid_select_capabilities(&info, &hardware, NULL) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_edid_select_capabilities(&info, &selected, &selected) == MTVK_INVALID_ARGUMENT);
    info.depth_mask = 0;
    CHECK(mtvk_edid_select_capabilities(&info, &hardware, &selected) == MTVK_NOT_SUPPORTED);
    for (unsigned version = 0; version < 2; ++version) {
        base(bytes, 1); displayid(bytes + 128, version != 0);
        CHECK(mtvk_edid_parse(bytes, 256, &info) == MTVK_OK && info.mode_count == 2);
        CHECK(info.modes[1].width == 3840 && info.modes[1].htotal == 4400 && info.modes[1].pixel_clock_khz == 594000);
        uint64_t hz = 0;
        CHECK(mtvk_display_refresh_millihz(&info.modes[1], &hz) == MTVK_OK && hz == 60000);
    }
    base(bytes, 1); cta(bytes + 128);
    CHECK(mtvk_edid_parse(bytes, 256, &info) == MTVK_OK);
    memcpy(&before, &info, sizeof(info));
    for (size_t size = 0; size < 256; ++size) {
        CHECK(mtvk_edid_parse(bytes, size, &info) != MTVK_OK);
        CHECK(memcmp(&info, &before, sizeof(info)) == 0);
    }
    bytes[130] = 3; checksum(bytes + 128, 128);
    CHECK(mtvk_edid_parse(bytes, 256, &info) == MTVK_INVALID_ARGUMENT);
    CHECK(memcmp(&info, &before, sizeof(info)) == 0);
    base(bytes, 1); displayid(bytes + 128, true);
    bytes[130] = 122; checksum(bytes + 128, 128);
    CHECK(mtvk_edid_parse(bytes, 256, &info) == MTVK_INVALID_ARGUMENT);
    base(bytes, 1); displayid(bytes + 128, true);
    bytes[136] ^= 1; checksum(bytes + 128, 128); // Outer checksum valid, inner invalid.
    CHECK(mtvk_edid_parse(bytes, 256, &info) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_edid_parse(NULL, 256, &info) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_edid_parse(bytes, 256, NULL) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_edid_parse(bytes, MTVK_EDID_MAX_BYTES + 1, &info) == MTVK_LIMIT_EXCEEDED);
    base(bytes, 1); cta(bytes + 128); bytes[132] = 0xff; checksum(bytes + 128, 128);
    CHECK(mtvk_edid_parse(bytes, 256, &info) == MTVK_INVALID_ARGUMENT);
    base(bytes, 1); displayid(bytes + 128, true); bytes[132] = 1;
    checksum(bytes + 129, 28); checksum(bytes + 128, 128);
    CHECK(mtvk_edid_parse(bytes, 256, &info) == MTVK_INVALID_ARGUMENT);
    base(bytes, 0); bytes[71] |= 0x80; checksum(bytes, 128);
    CHECK(mtvk_edid_parse(bytes, 128, &info) == MTVK_OK && !info.mode_count && info.omitted_timings == 1);
    CHECK(mtvk_edid_select_capabilities(&info, &hardware, &selected) == MTVK_NOT_FOUND);
    union { mtvk_edid_info output; uint8_t input[384]; } alias;
    base(alias.input, 1); cta(alias.input + 128);
    CHECK(mtvk_edid_parse(alias.input, 256, &alias.output) == MTVK_INVALID_ARGUMENT);
    // Checksummed mutation corpus exercises parsers beyond the outer checksum.
    uint32_t random = UINT32_C(0x46127abc);
    for (unsigned iteration = 0; iteration < 20000; ++iteration) {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        base(bytes, 1);
        if (iteration & 1u) cta(bytes + 128); else displayid(bytes + 128, true);
        const size_t offset = random % 256u;
        bytes[offset] ^= (uint8_t)(1u << ((random >> 16) % 8u));
        checksum(bytes, 128); checksum(bytes + 128, 128);
        memcpy(&before, &info, sizeof(info));
        const mtvk_status result = mtvk_edid_parse(bytes, 256, &info);
        if (result != MTVK_OK) CHECK(memcmp(&info, &before, sizeof(info)) == 0);
        else {
            CHECK(info.mode_count <= MTVK_DISPLAY_MAX_MODES);
            for (uint32_t i = 0; i < info.mode_count; ++i) {
                uint64_t rate = 0;
                CHECK(mtvk_display_refresh_millihz(&info.modes[i], &rate) == MTVK_OK && rate);
                CHECK(info.modes[i].id == i + 1);
            }
        }
    }
    // Oversized result is rejected, never silently truncated to 32 modes.
    uint8_t many[128 * 9]; base(many, 8);
    for (unsigned block = 0; block < 8; ++block) {
        uint8_t *p = many + 128 * (block + 1);
        memset(p, 0, 128); p[0] = 2; p[1] = 3; p[2] = 4;
        for (unsigned i = 0; i < 5; ++i) {
            mtvk_display_mode m = display_caps().modes[0];
            m.pixel_clock_khz += (block * 5 + i + 1) * 10;
            dtd(p + 4 + i * 18, m);
        }
        checksum(p, 128);
    }
    CHECK(mtvk_edid_parse(many, sizeof(many), &info) == MTVK_LIMIT_EXCEEDED);
    puts("PASS EDID/CTA/DisplayID bytes, checksum, timing/HDR and hardware intersection (constructed fixtures)");
    return 0;
}
