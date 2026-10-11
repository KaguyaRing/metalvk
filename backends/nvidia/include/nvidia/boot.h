// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_NVIDIA_BOOT_H
#define MTVK_NVIDIA_BOOT_H
#include "base/status.h"
#define MTVK_NV_VENDOR_ID UINT16_C(0x10de)
#define MTVK_NV_INITIAL_DEVICE_ID UINT16_C(0x28e0)
#define MTVK_NV_PCI_CONFIG_BYTES 256u
#define MTVK_NV_BOOT_0_OFFSET 0u
#define MTVK_NV_BOOT_1_OFFSET 4u
#define MTVK_NV_BOOT_42_OFFSET 0xa00u
#define MTVK_NV_PROBE_MAP_BYTES 4096u
#define MTVK_NV_ADA_ARCHITECTURE 0x19u
#define MTVK_NV_AD107_IMPLEMENTATION 7u
#define MTVK_NV_RM_VERSION "610.57.04"
#define MTVK_NV_GSP_IMAGE "gsp_ga10x.bin"
#define MTVK_NV_GSP_SIGNATURE ".fwsignature_ad10x"
typedef struct mtvk_nv_boot_registers {
    uint32_t boot_0, boot_1, boot_42;
} mtvk_nv_boot_registers;
typedef struct mtvk_nv_identity {
    uint8_t architecture, implementation, major_revision, minor_revision, extended_revision;
} mtvk_nv_identity;
MTVK_EXTERN_C_BEGIN
/* Internal source API, not a submission/plugin ABI. Fixed little-endian PCI
 * snapshot. Exact initial hardware policy; requires enabled memory decoding,
 * memory BAR0, valid capability chain, and D0 if a PM capability is present.
 * No config writes, address export, power transition or device authorization. */
mtvk_status mtvk_nv_validate_pci(const void *config, size_t size, uint64_t bar0_length);
/* Consistent physical AD107 identification from independent read-only boot
 * registers. Failure preserves out. Does not authenticate firmware or start RM. */
mtvk_status mtvk_nv_identify(const mtvk_nv_boot_registers *registers, mtvk_nv_identity *out);
MTVK_EXTERN_C_END
#endif
