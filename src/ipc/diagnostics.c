// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "ipc/init.h"

enum { HEADER_VERSION = 4, HEADER_SIZE = 6, HEADER_SELECTOR = 8,
       HEADER_RESERVED = 12, SNAPSHOT_BASE = 16, BAR_BASE = 48, BAR_SIZE = 16,
       PCI_BAR_BASE = 0x10, PCI_BAR_STRIDE = 4 };
static const uint8_t wire_magic[4] = { 'M', 'T', 'V', 'K' };

static uint64_t read_le(const uint8_t *p, size_t n) {
    uint64_t value = 0;
    for (size_t i = 0; i < n; ++i) value |= (uint64_t)p[i] << (i * 8u);
    return value;
}
static void write_le(uint8_t *p, uint64_t value, size_t n) {
    for (size_t i = 0; i < n; ++i) p[i] = (uint8_t)(value >> (i * 8u));
}
static void header(uint8_t *p, size_t size) {
    for (size_t i = 0; i < size; ++i) p[i] = 0;
    for (size_t i = 0; i < sizeof(wire_magic); ++i) p[i] = wire_magic[i];
    write_le(p + HEADER_VERSION, MTVK_DIAG_VERSION, 2);
    write_le(p + HEADER_SIZE, size, 2);
    write_le(p + HEADER_SELECTOR, MTVK_DIAG_SELECTOR, 4);
}
static mtvk_status check_header(const void *data, size_t size, size_t expected) {
    const uint8_t *p = data;
    if (!p || size != expected) return MTVK_INVALID_ARGUMENT;
    for (size_t i = 0; i < sizeof(wire_magic); ++i)
        if (p[i] != wire_magic[i]) return MTVK_INVALID_ARGUMENT;
    if (read_le(p + HEADER_VERSION, 2) != MTVK_DIAG_VERSION) return MTVK_NOT_SUPPORTED;
    if (read_le(p + HEADER_SIZE, 2) != expected ||
        read_le(p + HEADER_SELECTOR, 4) != MTVK_DIAG_SELECTOR ||
        read_le(p + HEADER_RESERVED, 4) != 0) return MTVK_INVALID_ARGUMENT;
    return MTVK_OK;
}
static mtvk_status check_snapshot(const mtvk_diag_snapshot *s) {
    if (!s || s->phase > MTVK_DIAG_PCI_READY ||
        (s->capabilities & ~MTVK_DIAG_CAP_PCI) != 0 ||
        (s->pci_address & UINT32_C(0xff00e0f8)) != 0 ||
        (s->phase == MTVK_DIAG_PCI_READY &&
         (s->capabilities != MTVK_DIAG_CAP_PCI || !s->registry_id ||
          !s->vendor_id || s->vendor_id == UINT16_MAX || s->device_id == UINT16_MAX)) ||
        (s->phase == MTVK_DIAG_OFFLINE && s->capabilities != 0)) return MTVK_INVALID_ARGUMENT;
    for (size_t i = 0; i < MTVK_DIAG_BAR_COUNT; ++i) {
        const mtvk_diag_bar *bar = &s->bars[i];
        if (bar->config_register != PCI_BAR_BASE + i * PCI_BAR_STRIDE ||
            (bar->flags & ~MTVK_DIAG_BAR_PRESENT) != 0 ||
            ((bar->flags == MTVK_DIAG_BAR_PRESENT) != (bar->length != 0)))
            return MTVK_INVALID_ARGUMENT;
    }
    return MTVK_OK;
}
mtvk_status mtvk_diag_make_request(void *out, size_t size) {
    if (!out || size != MTVK_DIAG_REQUEST_SIZE) return MTVK_INVALID_ARGUMENT;
    header(out, size);
    return MTVK_OK;
}
mtvk_status mtvk_diag_validate_request(const void *data, size_t size) {
    return check_header(data, size, MTVK_DIAG_REQUEST_SIZE);
}
mtvk_status mtvk_diag_encode(const mtvk_diag_snapshot *s, void *out, size_t size) {
    uint8_t *p = out;
    if (!p || size != MTVK_DIAG_RESPONSE_SIZE || check_snapshot(s) != MTVK_OK)
        return MTVK_INVALID_ARGUMENT;
    header(p, size);
    write_le(p + SNAPSHOT_BASE, s->vendor_id, 2);
    write_le(p + 18, s->device_id, 2);
    write_le(p + 20, s->subsystem_vendor_id, 2);
    write_le(p + 22, s->subsystem_device_id, 2);
    write_le(p + 24, s->pci_address, 4);
    write_le(p + 28, s->class_revision, 4);
    write_le(p + 32, s->phase, 4);
    write_le(p + 36, s->capabilities, 4);
    write_le(p + 40, s->registry_id, 8);
    for (size_t i = 0; i < MTVK_DIAG_BAR_COUNT; ++i) {
        uint8_t *bar = p + BAR_BASE + i * BAR_SIZE;
        write_le(bar, s->bars[i].config_register, 4);
        write_le(bar + 4, s->bars[i].flags, 4);
        write_le(bar + 8, s->bars[i].length, 8);
    }
    return MTVK_OK;
}
mtvk_status mtvk_diag_decode(const void *data, size_t size, mtvk_diag_snapshot *out) {
    const uint8_t *p = data;
    mtvk_diag_snapshot s = {0};
    mtvk_status status = check_header(data, size, MTVK_DIAG_RESPONSE_SIZE);
    if (!out) return MTVK_INVALID_ARGUMENT;
    if (status != MTVK_OK) return status;
    s.vendor_id = (uint16_t)read_le(p + SNAPSHOT_BASE, 2);
    s.device_id = (uint16_t)read_le(p + 18, 2);
    s.subsystem_vendor_id = (uint16_t)read_le(p + 20, 2);
    s.subsystem_device_id = (uint16_t)read_le(p + 22, 2);
    s.pci_address = (uint32_t)read_le(p + 24, 4);
    s.class_revision = (uint32_t)read_le(p + 28, 4);
    s.phase = (uint32_t)read_le(p + 32, 4);
    s.capabilities = (uint32_t)read_le(p + 36, 4);
    s.registry_id = read_le(p + 40, 8);
    for (size_t i = 0; i < MTVK_DIAG_BAR_COUNT; ++i) {
        const uint8_t *bar = p + BAR_BASE + i * BAR_SIZE;
        s.bars[i].config_register = (uint32_t)read_le(bar, 4);
        s.bars[i].flags = (uint32_t)read_le(bar + 4, 4);
        s.bars[i].length = read_le(bar + 8, 8);
    }
    status = check_snapshot(&s);
    if (status == MTVK_OK) *out = s;
    return status;
}
