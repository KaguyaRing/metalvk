// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_VK_OFFSCREEN_H
#define MTVK_VK_OFFSCREEN_H
#include "vk/context.h"
#define MTVK_VK_OFFSCREEN_MAX_EXTENT 4096u
#define MTVK_VK_OFFSCREEN_MAX_TARGETS 8u
#define MTVK_VK_OFFSCREEN_CHANNELS 4u
#define MTVK_VK_OFFSCREEN_MAX_WAIT_NS UINT64_C(30000000000)
#define MTVK_VK_OFFSCREEN_MAX_ALLOCATION UINT64_C(134217728)
#define MTVK_VK_OFFSCREEN_FORMAT VK_FORMAT_R8G8B8A8_UNORM
typedef struct mtvk_vk_offscreen mtvk_vk_offscreen;
MTVK_EXTERN_C_BEGIN
/* User-space Vulkan resource helper, NOT a GPU submission ABI or Metal shim.
 * Context/target/queue access must be serialized by the owner. Context must
 * have a graphics queue. Caller creates compatible native Vulkan pipelines.
 * A live target prevents context close. No shader/file/ICD emulation here. */
VkResult mtvk_vk_offscreen_open(mtvk_vk_context *context, uint32_t width, uint32_t height,
                              mtvk_vk_offscreen **out);
VkRenderPass mtvk_vk_offscreen_render_pass(const mtvk_vk_offscreen *target);
/* Starts a clear/store render pass. Caller may bind its Vulkan pipeline and
 * record graphics commands, but must not end/submit/reset this command buffer
 * or start another pass. begin can reuse a completed target. */
VkResult mtvk_vk_offscreen_begin(mtvk_vk_offscreen *target, const VkClearColorValue *clear,
                               VkCommandBuffer *out);
/* Ends the pass, records image->buffer copy + host barrier, submits with fence. */
VkResult mtvk_vk_offscreen_submit(mtvk_vk_offscreen *target);
/* Fence wait, 0..MAX_WAIT_NS (30 seconds); timeout never authorizes destruction
 * or readback. The driver must honor Vulkan's timeout contract. */
VkResult mtvk_vk_offscreen_wait(mtvk_vk_offscreen *target, uint64_t timeout_ns);
/* Complete targets only. Dense RGBA8 rows, width*height*4 bytes. Failure leaves
 * caller bytes unchanged. No CPU-generated fallback pixels. */
VkResult mtvk_vk_offscreen_copy(mtvk_vk_offscreen *target, void *bytes, size_t capacity);
/* Explicit device-wide drain for error recovery. Vulkan deviceWaitIdle has NO
 * deadline guarantee; caller must not treat this as a bounded wait. Failure
 * retains all objects. Device loss permits teardown, never successful readback. */
VkResult mtvk_vk_offscreen_drain(mtvk_vk_offscreen *target);
/* No implicit wait: pending/uncertain submission -> VK_NOT_READY, ownership
 * preserved. On success frees target; recording without submission is safe. */
VkResult mtvk_vk_offscreen_close(mtvk_vk_offscreen *target);
MTVK_EXTERN_C_END
#endif
