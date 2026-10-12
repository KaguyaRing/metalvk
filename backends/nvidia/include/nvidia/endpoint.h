// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_NVIDIA_ENDPOINT_H
#define MTVK_NVIDIA_ENDPOINT_H
#include "nvidia/discovery.h"
#include "nvidia/transport.h"
#include "ipc/diagnostics.h"
#define MTVK_NV_CONNECT_TYPE 1u
#define MTVK_NV_QUERY_SELECTOR 0u
#define MTVK_NV_QUERY_REQUEST_BYTES 24u
#define MTVK_NV_QUERY_RESPONSE_BYTES (24u + MTVK_NV_DEVICE_WIRE_BYTES)
MTVK_EXTERN_C_BEGIN
/* Convert a captured, validated PCI snapshot, not live MMIO. Only the exact
 * diagnostic target is supported. Unknown RM/GPU fields remain zero. No name,
 * chipset, SM, VRAM or execution capabilities are inferred from PCI IDs. */
mtvk_status mtvk_nv_discovery_from_pci(const mtvk_diag_snapshot *snapshot,
                                      mtvk_nv_device_info *out);
/* Validate query envelope, semantic status, supported operation set and device
 * policy before decoding. Failed calls preserve out; successful capability
 * projection matches the session policy. No IOKit dependency. */
mtvk_status mtvk_nv_discovery_response_decode(const void *response, size_t size,
                                             mtvk_nv_device_info *out);
MTVK_EXTERN_C_END
#endif
