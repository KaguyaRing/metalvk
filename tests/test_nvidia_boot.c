// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
// Register/config fixtures only: NOT NVIDIA hardware execution evidence.
#include "nvidia/init.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
static void pci_fixture(uint8_t *p) {
    memset(p, 0, MTVK_NV_PCI_CONFIG_BYTES);
    p[0] = 0xde; p[1] = 0x10; p[2] = 0xe0; p[3] = 0x28;
    p[4] = 2; p[6] = 16; p[11] = 3;
    p[16] = 4; p[19] = 0x80; // assigned non-prefetchable 64-bit memory BAR
    p[52] = 64; p[64] = 1; p[66] = 3; // PM v3, D0
}
int main(void) {
    uint8_t config[MTVK_NV_PCI_CONFIG_BYTES];
    pci_fixture(config);
    CHECK(mtvk_nv_validate_pci(config, sizeof(config), MTVK_NV_PROBE_MAP_BYTES) == MTVK_OK);
    CHECK(mtvk_nv_validate_pci(NULL, sizeof(config), MTVK_NV_PROBE_MAP_BYTES) == MTVK_INVALID_ARGUMENT);
    for (size_t i = 0; i < sizeof(config); ++i)
        CHECK(mtvk_nv_validate_pci(config, i, MTVK_NV_PROBE_MAP_BYTES) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_nv_validate_pci(config, sizeof(config), MTVK_NV_PROBE_MAP_BYTES - 1) == MTVK_NOT_SUPPORTED);
    config[4] = 0;
    CHECK(mtvk_nv_validate_pci(config, sizeof(config), MTVK_NV_PROBE_MAP_BYTES) == MTVK_NOT_SUPPORTED);
    pci_fixture(config); config[68] = 3;
    CHECK(mtvk_nv_validate_pci(config, sizeof(config), MTVK_NV_PROBE_MAP_BYTES) == MTVK_NOT_SUPPORTED);
    pci_fixture(config); config[16] = 1;
    CHECK(mtvk_nv_validate_pci(config, sizeof(config), MTVK_NV_PROBE_MAP_BYTES) == MTVK_NOT_SUPPORTED);
    pci_fixture(config); config[16] |= 8;
    CHECK(mtvk_nv_validate_pci(config, sizeof(config), MTVK_NV_PROBE_MAP_BYTES) == MTVK_NOT_SUPPORTED);
    pci_fixture(config); config[65] = 64;
    CHECK(mtvk_nv_validate_pci(config, sizeof(config), MTVK_NV_PROBE_MAP_BYTES) == MTVK_INVALID_ARGUMENT);
    pci_fixture(config); config[65] = 68; config[68] = 4;
    CHECK(mtvk_nv_validate_pci(config, sizeof(config), MTVK_NV_PROBE_MAP_BYTES) == MTVK_INVALID_ARGUMENT);
    pci_fixture(config); config[52] = 0;
    CHECK(mtvk_nv_validate_pci(config, sizeof(config), MTVK_NV_PROBE_MAP_BYTES) == MTVK_INVALID_ARGUMENT);
    pci_fixture(config); config[64] = 255;
    CHECK(mtvk_nv_validate_pci(config, sizeof(config), MTVK_NV_PROBE_MAP_BYTES) == MTVK_INVALID_ARGUMENT);
    pci_fixture(config); config[52] = 65;
    CHECK(mtvk_nv_validate_pci(config, sizeof(config), MTVK_NV_PROBE_MAP_BYTES) == MTVK_INVALID_ARGUMENT);
    pci_fixture(config); config[52] = 252; config[252] = 1;
    CHECK(mtvk_nv_validate_pci(config, sizeof(config), MTVK_NV_PROBE_MAP_BYTES) == MTVK_INVALID_ARGUMENT);
    pci_fixture(config); config[65] = 72; config[72] = 1; config[74] = 3;
    CHECK(mtvk_nv_validate_pci(config, sizeof(config), MTVK_NV_PROBE_MAP_BYTES) == MTVK_INVALID_ARGUMENT);
    pci_fixture(config); config[0] = 0xff; config[1] = 0xff;
    CHECK(mtvk_nv_validate_pci(config, sizeof(config), MTVK_NV_PROBE_MAP_BYTES) == MTVK_NOT_FOUND);
    const mtvk_nv_boot_registers valid = {UINT32_C(0x197000a1), 0, UINT32_C(0x197a1200)};
    mtvk_nv_boot_registers registers = valid;
    mtvk_nv_identity identity;
    CHECK(mtvk_nv_identify(&registers, &identity) == MTVK_OK);
    CHECK(identity.architecture == MTVK_NV_ADA_ARCHITECTURE && identity.implementation == 7 &&
        identity.major_revision == 10 && identity.minor_revision == 1 && identity.extended_revision == 2);
    mtvk_nv_identity saved; memcpy(&saved, &identity, sizeof(identity));
    for (unsigned i = 0; i < 32; ++i) {
        registers = valid; registers.boot_42 ^= UINT32_C(1) << i;
        const mtvk_status result = mtvk_nv_identify(&registers, &identity);
        if (result != MTVK_OK) CHECK(memcmp(&identity, &saved, sizeof(identity)) == 0);
        else memcpy(&identity, &saved, sizeof(identity));
    }
    registers = valid; registers.boot_1 = UINT32_C(0x100);
    CHECK(mtvk_nv_identify(&registers, &identity) == MTVK_NOT_SUPPORTED);
    registers = valid; registers.boot_1 = UINT32_C(0x10000);
    CHECK(mtvk_nv_identify(&registers, &identity) == MTVK_NOT_SUPPORTED);
    registers = valid; registers.boot_0 = 0;
    CHECK(mtvk_nv_identify(&registers, &identity) == MTVK_NOT_FOUND);
    registers = valid; registers.boot_42 = UINT32_MAX;
    CHECK(mtvk_nv_identify(&registers, &identity) == MTVK_NOT_FOUND);
    registers = valid; registers.boot_0 = UINT32_C(0x196000a1); registers.boot_42 = UINT32_C(0x196a1200);
    CHECK(mtvk_nv_identify(&registers, &identity) == MTVK_NOT_SUPPORTED);
    registers = valid; registers.boot_0 ^= 1;
    CHECK(mtvk_nv_identify(&registers, &identity) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_nv_identify(NULL, &identity) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_nv_identify(&valid, NULL) == MTVK_INVALID_ARGUMENT);
    union { mtvk_nv_identity output; mtvk_nv_boot_registers input; } alias;
    alias.input = valid;
    CHECK(mtvk_nv_identify(&alias.input, &alias.output) == MTVK_INVALID_ARGUMENT);
    uint32_t random = UINT32_C(0x406028e0);
    for (unsigned i = 0; i < 20000; ++i) {
        pci_fixture(config);
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        config[random % sizeof(config)] ^= (uint8_t)(1u << ((random >> 16) & 7u));
        (void)mtvk_nv_validate_pci(config, sizeof(config), MTVK_NV_PROBE_MAP_BYTES);
        registers.boot_0 = random; registers.boot_1 = random >> 3; registers.boot_42 = random << 1;
        memcpy(&saved, &identity, sizeof(identity));
        if (mtvk_nv_identify(&registers, &identity) != MTVK_OK)
            CHECK(memcmp(&identity, &saved, sizeof(identity)) == 0);
        else CHECK(identity.architecture == MTVK_NV_ADA_ARCHITECTURE &&
            identity.implementation == MTVK_NV_AD107_IMPLEMENTATION);
    }
    puts("PASS NVIDIA PCI/D0/capability-chain and AD107 boot decode (fixtures; no MMIO hardware test)");
    return 0;
}
