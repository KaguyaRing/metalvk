// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_VULKAN_CONTEXT_H
#define MTVK_VULKAN_CONTEXT_H
#include <vulkan/vulkan.h>
#include <stdbool.h>
#include "platform.h"

#define MTVK_VK_PHYSICAL_LIMIT 32u
#define MTVK_VK_QUEUE_LIMIT 64u
#define MTVK_VK_LAYER_LIMIT 64u
#define MTVK_VK_EXTENSION_LIMIT 256u
#define MTVK_VK_API_VERSION VK_API_VERSION_1_2

typedef struct mtvk_vk_context mtvk_vk_context;
MTVK_EXTERN_C_BEGIN
// Actual Vulkan handles, not a custom GPU-command API. The owner thread must
// externally synchronize access to the queue and all objects it submits.
// This hardware context deliberately rejects CPU/software devices and
// MoltenVK (avoids a circular Metal -> Vulkan -> Metal execution route).
VkResult mtvk_vk_open(uint32_t vendor, bool validation, mtvk_vk_context **out);
/* Hardware-only context with explicit queue requirements. Use vendor=0 for any
 * supported vendor. Invalid flags are rejected before touching the loader.
 * Vulkan handles are the execution interface; this is not a custom GPU ABI. */
VkResult mtvk_vk_open_for_queues(uint32_t vendor, bool validation,
                               VkQueueFlags required, mtvk_vk_context **out);
// Caller must complete all submitted work before destroying its child objects.
// close drains the device; errors (including device loss) are returned.
// An unexpected drain error preserves ownership; caller may retry close.
VkResult mtvk_vk_close(mtvk_vk_context *context);
VkDevice mtvk_vk_device(const mtvk_vk_context *context);
VkPhysicalDevice mtvk_vk_physical(const mtvk_vk_context *context);
VkQueue mtvk_vk_queue(const mtvk_vk_context *context);
uint32_t mtvk_vk_queue_family(const mtvk_vk_context *context);
const VkPhysicalDeviceProperties *mtvk_vk_properties(const mtvk_vk_context *context);
const VkPhysicalDeviceDriverProperties *mtvk_vk_driver(const mtvk_vk_context *context);
const VkPhysicalDeviceMemoryProperties *mtvk_vk_memory(const mtvk_vk_context *context);
uint32_t mtvk_vk_validation_errors(const mtvk_vk_context *context);
uint32_t mtvk_vk_validation_warnings(const mtvk_vk_context *context);
MTVK_EXTERN_C_END
#endif
