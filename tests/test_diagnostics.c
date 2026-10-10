// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "ipc/init.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    uint8_t request[MTVK_DIAG_REQUEST_SIZE];
    uint8_t wire[MTVK_DIAG_RESPONSE_SIZE], damaged[MTVK_DIAG_RESPONSE_SIZE];
    mtvk_diag_snapshot s = {0}, result = {0}, before;
    assert(mtvk_diag_make_request(request, sizeof(request)) == MTVK_OK);
    assert(mtvk_diag_validate_request(request, sizeof(request)) == MTVK_OK);
    request[12] = 1;
    assert(mtvk_diag_validate_request(request, sizeof(request)) == MTVK_INVALID_ARGUMENT);
    request[12] = 0; request[4] = 2;
    assert(mtvk_diag_validate_request(request, sizeof(request)) == MTVK_NOT_SUPPORTED);
    s.vendor_id = 0x10de; s.device_id = 0x28e0;
    s.subsystem_vendor_id = 0x17aa; s.subsystem_device_id = 0x3c51;
    s.phase = MTVK_DIAG_PCI_READY; s.capabilities = MTVK_DIAG_CAP_PCI;
    s.registry_id = UINT64_C(0x1020304050607080); s.pci_address = 0x010203;
    s.class_revision = 0x030000a1;
    for (size_t i = 0; i < MTVK_DIAG_BAR_COUNT; ++i) s.bars[i].config_register = (uint32_t)(0x10u + i * 4u);
    s.bars[0].flags = MTVK_DIAG_BAR_PRESENT; s.bars[0].length = UINT64_C(1) << 33;
    assert(mtvk_diag_encode(&s, wire, sizeof(wire)) == MTVK_OK);
    assert(wire[16] == 0xde && wire[17] == 0x10 && wire[40] == 0x80);
    assert(mtvk_diag_decode(wire, sizeof(wire), &result) == MTVK_OK);
    assert(result.registry_id == s.registry_id && result.bars[0].length == s.bars[0].length);
    before = result;
    for (size_t n = 0; n < sizeof(wire); ++n) {
        assert(mtvk_diag_decode(wire, n, &result) != MTVK_OK);
        assert(memcmp(&before, &result, sizeof(result)) == 0);
    }
    const size_t rejected_offsets[] = {0, 6, 8, 12, 27, 32, 36, 48, 52, 76};
    for (size_t i = 0; i < sizeof(rejected_offsets) / sizeof(rejected_offsets[0]); ++i) {
        memcpy(damaged, wire, sizeof(wire)); damaged[rejected_offsets[i]] ^= 0x80;
        assert(mtvk_diag_decode(damaged, sizeof(damaged), &result) != MTVK_OK);
        assert(memcmp(&before, &result, sizeof(result)) == 0);
    }
    s.bars[0].length = 0;
    assert(mtvk_diag_encode(&s, wire, sizeof(wire)) == MTVK_INVALID_ARGUMENT);
    assert(mtvk_diag_decode(NULL, sizeof(wire), &result) == MTVK_INVALID_ARGUMENT);
    assert(mtvk_diag_make_request(NULL, sizeof(request)) == MTVK_INVALID_ARGUMENT);
    puts("Diagnostic wire protocol tests passed");
    return 0;
}
