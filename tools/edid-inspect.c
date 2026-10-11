// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
// Read-only bounded snapshot inspector; no hardware control or EDID override.
#include "display/init.h"
#include <inttypes.h>
#include <stdio.h>
int main(int argc, char **argv) {
    if (argc != 2) { fputs("Usage: edid-inspect <raw-edid-file>\n", stderr); return 64; }
    FILE *file = fopen(argv[1], "rb");
    if (!file) { fputs("Cannot open EDID input\n", stderr); return 1; }
    uint8_t bytes[MTVK_EDID_MAX_BYTES + 1];
    const size_t count = fread(bytes, 1, sizeof(bytes), file);
    const bool read_error = ferror(file) != 0;
    const int close_result = fclose(file);
    if (read_error || close_result != 0) { fputs("Cannot read/close EDID snapshot\n", stderr); return 1; }
    mtvk_edid_info info;
    const mtvk_status result = mtvk_edid_parse(bytes, count, &info);
    if (result != MTVK_OK) { fprintf(stderr, "EDID parse failed: %s\n", mtvk_status_name(result)); return 1; }
    printf("EDID 1.%u: %s product %04x; name %s; %ux%u mm; digital=%u\n",
        (unsigned)info.revision, info.manufacturer, (unsigned)info.product, info.name,
        (unsigned)info.width_mm, (unsigned)info.height_mm, (unsigned)info.digital);
    printf("modes=%" PRIu32 " preferred=%" PRIu32 " depth_mask=%08" PRIx32
        " omitted_timings=%" PRIu32 " unhandled_blocks=%" PRIu32 "\n",
        info.mode_count, info.preferred_mode_id, info.depth_mask, info.omitted_timings, info.unhandled_blocks);
    printf("HDR eotfs=%02x descriptors=%02x BT2020_RGB=%u; encoded_luminance=%u/%u/%u present=%u\n",
        (unsigned)info.hdr_eotfs, (unsigned)info.hdr_descriptors, (unsigned)info.bt2020_rgb,
        (unsigned)info.luminance_max, (unsigned)info.luminance_average, (unsigned)info.luminance_min,
        (unsigned)info.luminance_present);
    for (uint32_t i = 0; i < info.mode_count; ++i) {
        const mtvk_display_mode *m = &info.modes[i];
        uint64_t rate = 0;
        if (mtvk_display_refresh_millihz(m, &rate) != MTVK_OK) return 1;
        printf("mode=%" PRIu32 " %" PRIu32 "x%" PRIu32 " refresh_millihz=%" PRIu64
            " clock_khz=%" PRIu32 " totals=%" PRIu32 "x%" PRIu32 "\n",
            m->id, m->width, m->height, rate, m->pixel_clock_khz, m->htotal, m->vtotal);
    }
    return 0;
}
