// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_TEST_VULKAN_RENDER_FAULTS_H
#define MTVK_TEST_VULKAN_RENDER_FAULTS_H
/* Test-only interception; never included in the production context/library.
 * The submission still uses a real hardware ICD. Only failure results are
 * injected; these tests are not successful rendering evidence. */
#include "vk/init.h"
VKAPI_ATTR VkResult VKAPI_CALL mtvk_fault_create_image(VkDevice device,
    const VkImageCreateInfo *info, const VkAllocationCallbacks *allocator, VkImage *out);
VKAPI_ATTR VkResult VKAPI_CALL mtvk_fault_submit(VkQueue queue, uint32_t count,
    const VkSubmitInfo *submits, VkFence fence);
VKAPI_ATTR VkResult VKAPI_CALL mtvk_fault_wait(VkDevice device, uint32_t count,
    const VkFence *fences, VkBool32 all, uint64_t timeout);
VKAPI_ATTR VkResult VKAPI_CALL mtvk_fault_drain(VkDevice device);
VKAPI_ATTR void VKAPI_CALL mtvk_fault_destroy_fence(VkDevice device, VkFence fence,
    const VkAllocationCallbacks *allocator);
VKAPI_ATTR VkResult VKAPI_CALL mtvk_fault_allocate(VkDevice device,
    const VkMemoryAllocateInfo *info, const VkAllocationCallbacks *allocator, VkDeviceMemory *out);
VKAPI_ATTR VkResult VKAPI_CALL mtvk_fault_map(VkDevice device, VkDeviceMemory memory,
    VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void **out);
VKAPI_ATTR VkResult VKAPI_CALL mtvk_fault_invalidate(VkDevice device, uint32_t count,
    const VkMappedMemoryRange *ranges);
const VkPhysicalDeviceMemoryProperties *mtvk_fault_memory(const mtvk_vk_context *context);
VkResult mtvk_fault_close(mtvk_vk_context *context);
VkResult mtvk_fault_open(uint32_t vendor, bool validation, VkQueueFlags required,
                        mtvk_vk_context **out);
#define vkCreateImage mtvk_fault_create_image
#define vkQueueSubmit mtvk_fault_submit
#define vkWaitForFences mtvk_fault_wait
#define vkDeviceWaitIdle mtvk_fault_drain
#define vkDestroyFence mtvk_fault_destroy_fence
#define mtvk_vk_close mtvk_fault_close
#define mtvk_vk_open_for_queues mtvk_fault_open
#define vkAllocateMemory mtvk_fault_allocate
#define vkMapMemory mtvk_fault_map
#define vkInvalidateMappedMemoryRanges mtvk_fault_invalidate
#define mtvk_vk_memory mtvk_fault_memory
#endif
