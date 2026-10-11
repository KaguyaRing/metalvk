// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
/* Dependency-free discovery initialization and capability projection.
 * Session dispatch and the user-space client use the same policy. */
#include "nvidia/discovery.h"
#include "nvidia/transport.h"

void mtvk_nv_device_info_init(mtvk_nv_device_info *info) {
    if (!info) return;
    uint8_t *bytes = (uint8_t *)info;
    for (size_t index = 0; index < sizeof(*info); ++index) bytes[index] = 0;
}

mtvk_nv_device_info mtvk_nv_effective_device_info(const mtvk_nv_device_info *device,
                                                uint32_t backed_op_mask) {
    mtvk_nv_device_info out;
    mtvk_nv_device_info_init(&out);
    if (!device) return out;
    out = *device;
    if (!(backed_op_mask & MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_ALLOC_TILED))) out.has_alloc_tiled = 0u;
    if (!(backed_op_mask & MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_MAP))) out.has_map_fixed = 0u;
    if (!(backed_op_mask & MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_OVERMAP))) out.has_overmap = 0u;
    if (!(backed_op_mask & MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_ALLOC_TILED))) out.has_compression = 0u;
    const uint32_t dma_buf_ops = MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_IMPORT) |
                                MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_EXPORT);
    if ((backed_op_mask & dma_buf_ops) != dma_buf_ops) out.has_dma_buf = 0u;
    return out;
}
