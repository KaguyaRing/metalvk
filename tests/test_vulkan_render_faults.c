// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "vulkan_render_faults.h"
#include <stdio.h>
#include <string.h>
#undef vkCreateImage
#undef vkQueueSubmit
#undef vkWaitForFences
#undef vkDeviceWaitIdle
#undef vkDestroyFence
#undef mtvk_vk_close
#undef mtvk_vk_open_for_queues
#undef vkAllocateMemory
#undef vkMapMemory
#undef vkInvalidateMappedMemoryRanges
#undef mtvk_vk_memory

enum fault_mode { AllocationFailure, WaitTimeout, DrainFailure, SubmitFailure, MemoryFailure, MapFailure, InvalidateFailure };
static enum fault_mode mode;
// Single-threaded test entrypoints; validation callbacks do not touch this state.
static bool submitted, pending, unsafe_cleanup, cleanup_failed;
static mtvk_vk_context *observed_context;
static unsigned drains, destroyed_fences, closes;
static unsigned allocations, invalidations;
static VkPhysicalDeviceMemoryProperties noncoherent;
int mtvk_render_cli_main(int argc, char **argv);

VkResult mtvk_fault_open(uint32_t vendor, bool validation, VkQueueFlags required,
                        mtvk_vk_context **out) {
    const VkResult result = mtvk_vk_open_for_queues(vendor, validation, required, out);
    if (result == VK_SUCCESS) observed_context = *out;
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL mtvk_fault_create_image(VkDevice device,
    const VkImageCreateInfo *info, const VkAllocationCallbacks *allocator, VkImage *out) {
    if (mode == AllocationFailure) {
        *out = (VkImage)(uintptr_t)UINTPTR_MAX; // Failed-call outputs must not become owned handles.
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    return vkCreateImage(device, info, allocator, out);
}
VKAPI_ATTR VkResult VKAPI_CALL mtvk_fault_submit(VkQueue queue, uint32_t count,
    const VkSubmitInfo *submits, VkFence fence) {
    if (mode == SubmitFailure) return VK_ERROR_OUT_OF_HOST_MEMORY;
    const VkResult result = vkQueueSubmit(queue, count, submits, fence);
    if (result == VK_SUCCESS) { submitted = true; pending = true; }
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL mtvk_fault_wait(VkDevice device, uint32_t count,
    const VkFence *fences, VkBool32 all, uint64_t timeout) {
    if (mode == MapFailure || mode == InvalidateFailure) {
        const VkResult result = vkWaitForFences(device, count, fences, all, timeout);
        if (result == VK_SUCCESS || result == VK_ERROR_DEVICE_LOST) pending = false;
        return result;
    }
    return VK_TIMEOUT; // A timeout is never reinterpreted as completion.
}
VKAPI_ATTR VkResult VKAPI_CALL mtvk_fault_allocate(VkDevice device,
    const VkMemoryAllocateInfo *info, const VkAllocationCallbacks *allocator, VkDeviceMemory *out) {
    ++allocations;
    if (mode == MemoryFailure && allocations == 2) {
        *out = (VkDeviceMemory)(uintptr_t)UINTPTR_MAX;
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    return vkAllocateMemory(device, info, allocator, out);
}
VKAPI_ATTR VkResult VKAPI_CALL mtvk_fault_map(VkDevice device, VkDeviceMemory memory,
    VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void **out) {
    if (mode == MapFailure) return VK_ERROR_MEMORY_MAP_FAILED;
    return vkMapMemory(device, memory, offset, size, flags, out);
}
const VkPhysicalDeviceMemoryProperties *mtvk_fault_memory(const mtvk_vk_context *context) {
    const VkPhysicalDeviceMemoryProperties *properties = mtvk_vk_memory(context);
    if (mode != InvalidateFailure) return properties;
    noncoherent = *properties;
    // Exercise the invalidate branch even if the ICD's host memory is coherent.
    // No physical memory or successful GPU result is simulated by this override.
    for (uint32_t i = 0; i < noncoherent.memoryTypeCount; ++i)
        noncoherent.memoryTypes[i].propertyFlags &= ~(VkMemoryPropertyFlags)VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    return &noncoherent;
}
VKAPI_ATTR VkResult VKAPI_CALL mtvk_fault_invalidate(VkDevice device, uint32_t count,
    const VkMappedMemoryRange *ranges) {
    ++invalidations;
    if (mode == InvalidateFailure) return VK_ERROR_OUT_OF_HOST_MEMORY;
    return vkInvalidateMappedMemoryRanges(device, count, ranges);
}
VKAPI_ATTR VkResult VKAPI_CALL mtvk_fault_drain(VkDevice device) {
    ++drains;
    if (mode == DrainFailure) return VK_ERROR_OUT_OF_HOST_MEMORY;
    const VkResult result = vkDeviceWaitIdle(device);
    if (result == VK_SUCCESS || result == VK_ERROR_DEVICE_LOST) pending = false;
    return result;
}
VKAPI_ATTR void VKAPI_CALL mtvk_fault_destroy_fence(VkDevice device, VkFence fence,
    const VkAllocationCallbacks *allocator) {
    ++destroyed_fences;
    if (pending) { unsafe_cleanup = true; return; }
    vkDestroyFence(device, fence, allocator);
}
VkResult mtvk_fault_close(mtvk_vk_context *context) {
    if (pending) { unsafe_cleanup = true; return VK_ERROR_OUT_OF_HOST_MEMORY; }
    const VkResult result = mtvk_vk_close(context);
    if (result == VK_NOT_READY) return result; // Expected live-child ownership check.
    ++closes;
    cleanup_failed |= result != VK_SUCCESS;
    if (result == VK_SUCCESS || result == VK_ERROR_DEVICE_LOST || result == VK_ERROR_VALIDATION_FAILED_EXT)
        observed_context = NULL;
    return result;
}
int main(int argc, char **argv) {
    if (argc != 5) return 64;
    if (strcmp(argv[4], "allocation-failure") == 0) mode = AllocationFailure;
    else if (strcmp(argv[4], "wait-timeout") == 0) mode = WaitTimeout;
    else if (strcmp(argv[4], "drain-failure") == 0) mode = DrainFailure;
    else if (strcmp(argv[4], "submit-failure") == 0) mode = SubmitFailure;
    else if (strcmp(argv[4], "memory-failure") == 0) mode = MemoryFailure;
    else if (strcmp(argv[4], "map-failure") == 0) mode = MapFailure;
    else if (strcmp(argv[4], "invalidate-failure") == 0) mode = InvalidateFailure;
    else return 64;
    const int result = mtvk_render_cli_main(4, argv);
    bool passed = result == 1 && !unsafe_cleanup && !cleanup_failed;
    if (observed_context)
        passed &= !mtvk_vk_validation_errors(observed_context) && !mtvk_vk_validation_warnings(observed_context);
    if (mode == AllocationFailure || mode == MemoryFailure)
        passed &= !submitted && !pending && !drains && !destroyed_fences && closes == 1;
    else if (mode == WaitTimeout)
        passed &= submitted && !pending && drains == 1 && destroyed_fences == 1 && closes == 1;
    else if (mode == DrainFailure)
        passed &= submitted && pending && drains == 1 && !destroyed_fences && !closes;
    else if (mode == SubmitFailure)
        passed &= !submitted && !pending && drains == 1 && destroyed_fences == 1 && closes == 1;
    else
        passed &= submitted && !pending && !drains && destroyed_fences == 1 && closes == 1;
    if (mode == MemoryFailure) passed &= allocations == 2;
    if (mode == InvalidateFailure) passed &= invalidations == 1;
    if (!passed) {
        fprintf(stderr, "FAIL fault cleanup: mode=%s result=%d submit=%d pending=%d drain=%u fence=%u close=%u unsafe=%d\n",
            argv[4], result, submitted, pending, drains, destroyed_fences, closes, unsafe_cleanup);
        return 1;
    }
    printf("PASS injected %s cleanup (not rendering evidence)\n", argv[4]);
    // DrainFailure intentionally preserves driver objects until process exit.
    return 0;
}
