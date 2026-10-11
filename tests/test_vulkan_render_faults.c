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

enum fault_mode { AllocationFailure, WaitTimeout, DrainFailure };
static enum fault_mode mode;
// Single-threaded test entrypoints; validation callbacks do not touch this state.
static bool submitted, pending, unsafe_cleanup, cleanup_failed;
static mtvk_vk_context *observed_context;
static unsigned drains, destroyed_fences, closes;
int mtvk_render_cli_main(int argc, char **argv);

VkResult mtvk_fault_open(uint32_t vendor, bool validation, VkQueueFlags required,
                        mtvk_vk_context **out) {
    const VkResult result = mtvk_vk_open_for_queues(vendor, validation, required, out);
    if (result == VK_SUCCESS) observed_context = *out;
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL mtvk_fault_create_image(VkDevice device,
    const VkImageCreateInfo *info, const VkAllocationCallbacks *allocator, VkImage *out) {
    if (mode == AllocationFailure) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    return vkCreateImage(device, info, allocator, out);
}
VKAPI_ATTR VkResult VKAPI_CALL mtvk_fault_submit(VkQueue queue, uint32_t count,
    const VkSubmitInfo *submits, VkFence fence) {
    const VkResult result = vkQueueSubmit(queue, count, submits, fence);
    if (result == VK_SUCCESS) { submitted = true; pending = true; }
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL mtvk_fault_wait(VkDevice device, uint32_t count,
    const VkFence *fences, VkBool32 all, uint64_t timeout) {
    (void)device; (void)count; (void)fences; (void)all; (void)timeout;
    return VK_TIMEOUT; // A timeout is never reinterpreted as completion.
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
    ++closes;
    if (pending) { unsafe_cleanup = true; return VK_ERROR_OUT_OF_HOST_MEMORY; }
    cleanup_failed |= mtvk_vk_validation_errors(context) != 0 || mtvk_vk_validation_warnings(context) != 0;
    const VkResult result = mtvk_vk_close(context);
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
    else return 64;
    const int result = mtvk_render_cli_main(4, argv);
    bool passed = result == 1 && !unsafe_cleanup && !cleanup_failed;
    if (observed_context)
        passed &= !mtvk_vk_validation_errors(observed_context) && !mtvk_vk_validation_warnings(observed_context);
    if (mode == AllocationFailure)
        passed &= !submitted && !pending && !drains && !destroyed_fences && closes == 1;
    else if (mode == WaitTimeout)
        passed &= submitted && !pending && drains == 1 && destroyed_fences == 1 && closes == 1;
    else
        passed &= submitted && pending && drains == 1 && !destroyed_fences && !closes;
    if (!passed) {
        fprintf(stderr, "FAIL fault cleanup: mode=%s result=%d submit=%d pending=%d drain=%u fence=%u close=%u unsafe=%d\n",
            argv[4], result, submitted, pending, drains, destroyed_fences, closes, unsafe_cleanup);
        return 1;
    }
    printf("PASS injected %s cleanup (not rendering evidence)\n", argv[4]);
    // DrainFailure intentionally preserves driver objects until process exit.
    return 0;
}
