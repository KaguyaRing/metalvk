// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "nvidia/init.h"
#include "base/span.h"
enum {
    PCI_VENDOR = 0, PCI_DEVICE = 2, PCI_COMMAND = 4, PCI_STATUS = 6,
    PCI_CLASS = 9, PCI_HEADER = 14, PCI_BAR0 = 16, PCI_BAR1 = 20, PCI_CAP_HEAD = 52,
    PCI_MEMORY_ENABLE = 2, PCI_CAP_PRESENT = 16, PCI_PM_CAP = 1,
    PCI_CAP_MIN = 64, PCI_CAP_MAX = 252, PCI_PM_BYTES = 8,
    BOOT_ARCH_SHIFT = 24, BOOT_IMPL_SHIFT = 20, BOOT_MAJOR_SHIFT = 16,
    BOOT_MINOR_SHIFT = 12, BOOT_EXTENDED_SHIFT = 8,
    BOOT_1_VIRTUAL_MASK = 0x30100
};
static uint32_t le16(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return le16(p) | (le16(p + 2) << 16); }
mtvk_status mtvk_nv_validate_pci(const void *config, size_t size, uint64_t bar0_length) {
    if (!config || size != MTVK_NV_PCI_CONFIG_BYTES || !mtvk_span_valid(config, size))
        return MTVK_INVALID_ARGUMENT;
    const uint8_t *p = config;
    if (le16(p + PCI_VENDOR) == UINT16_MAX || le16(p + PCI_DEVICE) == UINT16_MAX)
        return MTVK_NOT_FOUND;
    if (le16(p + PCI_VENDOR) != MTVK_NV_VENDOR_ID || le16(p + PCI_DEVICE) != MTVK_NV_INITIAL_DEVICE_ID ||
        p[PCI_CLASS + 2] != 3 || (p[PCI_CLASS + 1] != 0 && p[PCI_CLASS + 1] != 2) ||
        p[PCI_CLASS] != 0 || (p[PCI_HEADER] & 127u) != 0) return MTVK_NOT_SUPPORTED;
    const uint32_t bar = le32(p + PCI_BAR0), type = (bar >> 1) & 3u;
    if (bar == UINT32_MAX || (bar & 1u) || (type != 0 && type != 2) || (bar & 8u))
        return MTVK_NOT_SUPPORTED;
    if (!(bar & ~UINT32_C(15)) && (type != 2 || !le32(p + PCI_BAR1))) return MTVK_NOT_SUPPORTED;
    if (bar0_length < MTVK_NV_PROBE_MAP_BYTES || !(le16(p + PCI_COMMAND) & PCI_MEMORY_ENABLE))
        return MTVK_NOT_SUPPORTED;
    if (!(le16(p + PCI_STATUS) & PCI_CAP_PRESENT)) return MTVK_OK;
    uint64_t visited = 0, pm_range = 0;
    bool found_pm = false;
    unsigned next = p[PCI_CAP_HEAD];
    if (!next) return MTVK_INVALID_ARGUMENT;
    while (next) {
        if (next < PCI_CAP_MIN || next > PCI_CAP_MAX || (next & 3u)) return MTVK_INVALID_ARGUMENT;
        const uint64_t bit = UINT64_C(1) << (next / 4u);
        if ((visited | pm_range) & bit || !p[next] || p[next] == UINT8_MAX) return MTVK_INVALID_ARGUMENT;
        visited |= bit;
        if (p[next] == PCI_PM_CAP) {
            if (found_pm || next > MTVK_NV_PCI_CONFIG_BYTES - PCI_PM_BYTES) return MTVK_INVALID_ARGUMENT;
            pm_range = bit | (bit << 1);
            if (visited & (bit << 1)) return MTVK_INVALID_ARGUMENT;
            found_pm = true;
            const unsigned version = le16(p + next + 2) & 7u;
            if (!version || version > 3 || (le16(p + next + 4) & 3u)) return MTVK_NOT_SUPPORTED;
        }
        next = p[next + 1];
    }
    return MTVK_OK;
}
mtvk_status mtvk_nv_identify(const mtvk_nv_boot_registers *registers, mtvk_nv_identity *out) {
    if (!registers || !out || !mtvk_span_valid(registers, sizeof(*registers)) ||
        !mtvk_span_valid(out, sizeof(*out)) ||
        mtvk_spans_overlap(registers, sizeof(*registers), out, sizeof(*out))) return MTVK_INVALID_ARGUMENT;
    const uint32_t boot0 = registers->boot_0, boot1 = registers->boot_1, boot42 = registers->boot_42;
    if (!boot0 || boot0 == UINT32_MAX || boot1 == UINT32_MAX || !boot42 || boot42 == UINT32_MAX)
        return MTVK_NOT_FOUND;
    if (boot1 & BOOT_1_VIRTUAL_MASK) return MTVK_NOT_SUPPORTED;
    const uint32_t arch0 = ((boot0 >> BOOT_ARCH_SHIFT) & 31u) | (((boot0 >> 8) & 1u) << 5);
    const uint32_t arch42 = (boot42 >> BOOT_ARCH_SHIFT) & 63u;
    const uint32_t impl0 = (boot0 >> BOOT_IMPL_SHIFT) & 15u;
    const uint32_t impl42 = (boot42 >> BOOT_IMPL_SHIFT) & 15u;
    if (arch0 != arch42 || impl0 != impl42 || ((boot0 >> 4) & 15u) != ((boot42 >> BOOT_MAJOR_SHIFT) & 15u) ||
        (boot0 & 15u) != ((boot42 >> BOOT_MINOR_SHIFT) & 15u)) return MTVK_INVALID_ARGUMENT;
    if (arch42 != MTVK_NV_ADA_ARCHITECTURE || impl42 != MTVK_NV_AD107_IMPLEMENTATION)
        return MTVK_NOT_SUPPORTED;
    const mtvk_nv_identity identity = {(uint8_t)arch42, (uint8_t)impl42, (uint8_t)((boot42 >> BOOT_MAJOR_SHIFT) & 15u),
        (uint8_t)((boot42 >> BOOT_MINOR_SHIFT) & 15u), (uint8_t)((boot42 >> BOOT_EXTENDED_SHIFT) & 15u)};
    *out = identity;
    return MTVK_OK;
}
