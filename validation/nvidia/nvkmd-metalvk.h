// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_NVKMD_METALVK_H
#define MTVK_NVKMD_METALVK_H
#include "platform.h"
#include <vulkan/vulkan.h>
struct nvkmd_pdev;
struct vk_sync_type;
MTVK_EXTERN_C_BEGIN
VkResult mtvk_nvkmd_open(const void *response, size_t size,
    const struct vk_sync_type *const *sync_types, struct nvkmd_pdev **out);
#if defined(__APPLE__) || defined(MTVK_NVKMD_NATIVE_INGRESS_TEST)
/* Explicit native service ingress. Must link libmetalvk-nvidia-darwin and the
 * adapter; not a physical-device enumerator or an executable GPU device. */
VkResult mtvk_nvkmd_open_registry(uint64_t registry_id,
    const struct vk_sync_type *const *sync_types, struct nvkmd_pdev **out);
#endif
MTVK_EXTERN_C_END
#endif
