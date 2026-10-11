// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_NVIDIA_DISCOVERY_H
#define MTVK_NVIDIA_DISCOVERY_H
#include "platform.h"
#include <stdint.h>

/* Device discovery payload for the MTVK_NV_OP_DEVICE_QUERY response.
 *
 * Mirrors the fields NVK reads from nvkmd_pdev so the two sides agree on one
 * fixed layout. Any field the kernel cannot yet supply stays zero with its
 * capability flag clear: a zero here means "unknown", never a fabricated
 * hardware value, and a set capability flag means the matching transport op is
 * genuinely backed. This is a private NVIDIA backend standard, not an
 * application ABI. */
#define MTVK_NV_DEVICE_NAME_BYTES 64u
#define MTVK_NV_CHIPSET_NAME_BYTES 16u
#define MTVK_NV_SMEM_CONFIGS 10u
#define MTVK_NV_ZCULL_FIELDS 12u

MTVK_EXTERN_C_BEGIN

typedef enum mtvk_nv_device_type {
    MTVK_NV_DEVICE_IGP = 0u,
    MTVK_NV_DEVICE_DISCRETE = 1u,
    MTVK_NV_DEVICE_SOC = 2u
} mtvk_nv_device_type;

typedef struct mtvk_nv_device_info {
    uint32_t device_type;
    uint16_t device_id;
    uint16_t chipset;
    uint16_t pci_domain;
    uint8_t pci_bus;
    uint8_t pci_device;
    uint8_t pci_function;
    uint8_t pci_revision;
    uint8_t sm;
    uint8_t gpc_count;
    uint16_t tpc_count;
    uint8_t mp_per_tpc;
    uint8_t max_warps_per_mp;
    uint8_t has_transfer_queue;
    uint8_t has_zcull_info;
    uint16_t nc_atom_size_B;
    uint16_t cls_copy;
    uint16_t cls_eng2d;
    uint16_t cls_eng3d;
    uint16_t cls_m2mf;
    uint16_t cls_compute;
    uint16_t cls_gpfifo;
    uint64_t vram_size_B;
    uint64_t bar_size_B;
    uint16_t max_smem_per_wg_kB;
    uint16_t sm_smem_sizes_kB[MTVK_NV_SMEM_CONFIGS];
    uint8_t sm_smem_size_count;
    uint32_t zcull_info[MTVK_NV_ZCULL_FIELDS];
    char device_name[MTVK_NV_DEVICE_NAME_BYTES];
    char chipset_name[MTVK_NV_CHIPSET_NAME_BYTES];
    /* nvkmd_info capability flags: only set when the matching op is backed. */
    uint8_t has_alloc_tiled;
    uint8_t has_map_fixed;
    uint8_t has_overmap;
    uint8_t has_compression;
    uint8_t has_dma_buf;
    /* Device-wide bind alignment and usable GPU VA range. */
    uint32_t bind_align_B;
    uint64_t va_start;
    uint64_t va_end;
} mtvk_nv_device_info;

MTVK_EXTERN_C_END
#endif
