// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "vk/init.h"
#include "context-internal.h"
#include "base/span.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
typedef enum target_phase { TargetReady, TargetRecording, TargetPending, TargetUncertain,
    TargetComplete, TargetFailed, TargetLost } target_phase;
struct mtvk_vk_offscreen {
    mtvk_vk_context *context;
    VkDevice device;
    uint32_t width, height;
    size_t bytes;
    bool coherent;
    target_phase phase;
    VkImage image;
    VkDeviceMemory image_memory, readback_memory;
    VkImageView view;
    VkRenderPass pass;
    VkFramebuffer framebuffer;
    VkBuffer readback;
    VkCommandPool pool;
    VkCommandBuffer commands;
    VkFence fence;
};
static VkResult remember_loss(mtvk_vk_offscreen *t, VkResult result) {
    if (result == VK_ERROR_DEVICE_LOST) t->phase = TargetLost;
    return result;
}
static bool memory_type(const VkPhysicalDeviceMemoryProperties *memory, uint32_t bits,
                        VkMemoryPropertyFlags want, uint32_t *index) {
    for (uint32_t i = 0; i < memory->memoryTypeCount; ++i)
        if ((bits & (UINT32_C(1) << i)) && (memory->memoryTypes[i].propertyFlags & want) == want) {
            *index = i; return true;
        }
    return false;
}
static void destroy_target(mtvk_vk_offscreen *t) {
    if (t->fence) vkDestroyFence(t->device, t->fence, NULL);
    if (t->pool) vkDestroyCommandPool(t->device, t->pool, NULL);
    if (t->readback) vkDestroyBuffer(t->device, t->readback, NULL);
    if (t->readback_memory) vkFreeMemory(t->device, t->readback_memory, NULL);
    if (t->framebuffer) vkDestroyFramebuffer(t->device, t->framebuffer, NULL);
    if (t->pass) vkDestroyRenderPass(t->device, t->pass, NULL);
    if (t->view) vkDestroyImageView(t->device, t->view, NULL);
    if (t->image) vkDestroyImage(t->device, t->image, NULL);
    if (t->image_memory) vkFreeMemory(t->device, t->image_memory, NULL);
    mtvk_vk_context_release_target(t->context);
    free(t);
}
#define TRY(call) do { result = (call); if (result != VK_SUCCESS) goto failure; } while (0)
#define CREATE(type, member, call) do { type handle = VK_NULL_HANDLE; TRY(call); \
    if (handle == VK_NULL_HANDLE) { result = VK_ERROR_INITIALIZATION_FAILED; goto failure; } \
    t->member = handle; } while (0)
VkResult mtvk_vk_offscreen_open(mtvk_vk_context *context, uint32_t width, uint32_t height,
                              mtvk_vk_offscreen **out) {
    if (!mtvk_vk_context_output_valid(context, out, sizeof(*out))) return VK_ERROR_INITIALIZATION_FAILED;
    *out = NULL;
    if (!context || !width || !height || width > MTVK_VK_OFFSCREEN_MAX_EXTENT ||
        height > MTVK_VK_OFFSCREEN_MAX_EXTENT) return VK_ERROR_INITIALIZATION_FAILED;
    const VkPhysicalDeviceProperties *properties = mtvk_vk_properties(context);
    if (width > properties->limits.maxFramebufferWidth || height > properties->limits.maxFramebufferHeight ||
        width > properties->limits.maxImageDimension2D || height > properties->limits.maxImageDimension2D)
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    VkFormatProperties format;
    vkGetPhysicalDeviceFormatProperties(mtvk_vk_physical(context), MTVK_VK_OFFSCREEN_FORMAT, &format);
    const VkFormatFeatureFlags features = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    if ((format.optimalTilingFeatures & features) != features) return VK_ERROR_FORMAT_NOT_SUPPORTED;
    mtvk_vk_offscreen *t = calloc(1, sizeof(*t));
    if (!t) return VK_ERROR_OUT_OF_HOST_MEMORY;
    VkResult result = mtvk_vk_context_hold_target(context);
    if (result != VK_SUCCESS) { free(t); return result; }
    t->context = context; t->device = mtvk_vk_device(context);
    t->width = width; t->height = height; t->bytes = (size_t)width * height * MTVK_VK_OFFSCREEN_CHANNELS;
    const VkPhysicalDeviceMemoryProperties *memory = mtvk_vk_memory(context);
    uint32_t type = 0;
    VkImageCreateInfo image_info = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = MTVK_VK_OFFSCREEN_FORMAT,
        .extent = {width, height, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
    CREATE(VkImage, image, vkCreateImage(t->device, &image_info, NULL, &handle));
    VkMemoryRequirements image_req;
    vkGetImageMemoryRequirements(t->device, t->image, &image_req);
    if (!image_req.size || image_req.size > MTVK_VK_OFFSCREEN_MAX_ALLOCATION ||
        !memory_type(memory, image_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &type)) {
        result = VK_ERROR_OUT_OF_DEVICE_MEMORY; goto failure;
    }
    VkMemoryAllocateInfo image_alloc = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = image_req.size, .memoryTypeIndex = type };
    CREATE(VkDeviceMemory, image_memory, vkAllocateMemory(t->device, &image_alloc, NULL, &handle));
    TRY(vkBindImageMemory(t->device, t->image, t->image_memory, 0));
    VkImageViewCreateInfo view_info = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = t->image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = MTVK_VK_OFFSCREEN_FORMAT,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1} };
    CREATE(VkImageView, view, vkCreateImageView(t->device, &view_info, NULL, &handle));
    VkAttachmentDescription attachment = { .format = MTVK_VK_OFFSCREEN_FORMAT,
        .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL };
    VkAttachmentReference reference = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1, .pColorAttachments = &reference };
    VkSubpassDependency dependency = { .srcSubpass = 0, .dstSubpass = VK_SUBPASS_EXTERNAL,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, .dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT };
    VkRenderPassCreateInfo pass_info = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &attachment, .subpassCount = 1, .pSubpasses = &subpass,
        .dependencyCount = 1, .pDependencies = &dependency };
    CREATE(VkRenderPass, pass, vkCreateRenderPass(t->device, &pass_info, NULL, &handle));
    VkFramebufferCreateInfo framebuffer_info = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = t->pass, .attachmentCount = 1, .pAttachments = &t->view,
        .width = width, .height = height, .layers = 1 };
    CREATE(VkFramebuffer, framebuffer, vkCreateFramebuffer(t->device, &framebuffer_info, NULL, &handle));
    VkBufferCreateInfo buffer_info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = t->bytes, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    CREATE(VkBuffer, readback, vkCreateBuffer(t->device, &buffer_info, NULL, &handle));
    VkMemoryRequirements buffer_req;
    vkGetBufferMemoryRequirements(t->device, t->readback, &buffer_req);
    t->coherent = memory_type(memory, buffer_req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &type);
    if (buffer_req.size < t->bytes || buffer_req.size > MTVK_VK_OFFSCREEN_MAX_ALLOCATION ||
        (!t->coherent && !memory_type(memory, buffer_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &type))) {
        result = VK_ERROR_OUT_OF_DEVICE_MEMORY; goto failure;
    }
    VkMemoryAllocateInfo buffer_alloc = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = buffer_req.size, .memoryTypeIndex = type };
    CREATE(VkDeviceMemory, readback_memory, vkAllocateMemory(t->device, &buffer_alloc, NULL, &handle));
    TRY(vkBindBufferMemory(t->device, t->readback, t->readback_memory, 0));
    VkCommandPoolCreateInfo pool_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, .queueFamilyIndex = mtvk_vk_queue_family(context) };
    CREATE(VkCommandPool, pool, vkCreateCommandPool(t->device, &pool_info, NULL, &handle));
    VkCommandBufferAllocateInfo allocate = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = t->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    CREATE(VkCommandBuffer, commands, vkAllocateCommandBuffers(t->device, &allocate, &handle));
    VkFenceCreateInfo fence_info = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    CREATE(VkFence, fence, vkCreateFence(t->device, &fence_info, NULL, &handle));
    *out = t;
    return VK_SUCCESS;
failure:
    destroy_target(t); // open never submits work
    return result;
}
#undef TRY
#undef CREATE
VkRenderPass mtvk_vk_offscreen_render_pass(const mtvk_vk_offscreen *t) { return t ? t->pass : VK_NULL_HANDLE; }
VkResult mtvk_vk_offscreen_begin(mtvk_vk_offscreen *t, const VkClearColorValue *clear, VkCommandBuffer *out) {
    if (!mtvk_vk_context_output_valid(t ? t->context : NULL, out, sizeof(*out)) ||
        (t && mtvk_spans_overlap(t, sizeof(*t), out, sizeof(*out))) ||
        (clear && (!mtvk_span_valid(clear, sizeof(*clear)) ||
                   mtvk_spans_overlap(clear, sizeof(*clear), out, sizeof(*out)))))
        return VK_ERROR_INITIALIZATION_FAILED;
    *out = VK_NULL_HANDLE;
    if (!t || !clear) return VK_ERROR_INITIALIZATION_FAILED;
    if (t->phase == TargetLost) return VK_ERROR_DEVICE_LOST;
    if (t->phase == TargetPending || t->phase == TargetUncertain || t->phase == TargetRecording) return VK_NOT_READY;
    for (unsigned i = 0; i < 4; ++i) if (!isfinite(clear->float32[i])) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = vkResetCommandPool(t->device, t->pool, 0);
    t->phase = TargetFailed;
    if (result != VK_SUCCESS) return remember_loss(t, result);
    VkCommandBufferBeginInfo begin = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    result = vkBeginCommandBuffer(t->commands, &begin);
    if (result != VK_SUCCESS) return remember_loss(t, result);
    VkClearValue value = { .color = *clear };
    VkRenderPassBeginInfo render = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = t->pass, .framebuffer = t->framebuffer,
        .renderArea = {{0, 0}, {t->width, t->height}}, .clearValueCount = 1, .pClearValues = &value };
    vkCmdBeginRenderPass(t->commands, &render, VK_SUBPASS_CONTENTS_INLINE);
    t->phase = TargetRecording; *out = t->commands;
    return VK_SUCCESS;
}
VkResult mtvk_vk_offscreen_submit(mtvk_vk_offscreen *t) {
    if (!t) return VK_ERROR_INITIALIZATION_FAILED;
    if (t->phase == TargetLost) return VK_ERROR_DEVICE_LOST;
    if (t->phase != TargetRecording) return VK_NOT_READY;
    vkCmdEndRenderPass(t->commands);
    VkBufferImageCopy region = { .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageExtent = {t->width, t->height, 1} };
    vkCmdCopyImageToBuffer(t->commands, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t->readback, 1, &region);
    VkMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
    vkCmdPipelineBarrier(t->commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 1, &barrier, 0, NULL, 0, NULL);
    t->phase = TargetFailed;
    VkResult result = vkEndCommandBuffer(t->commands);
    if (result != VK_SUCCESS) return remember_loss(t, result);
    result = vkResetFences(t->device, 1, &t->fence);
    if (result != VK_SUCCESS) return remember_loss(t, result);
    VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &t->commands };
    result = vkQueueSubmit(mtvk_vk_queue(t->context), 1, &submit, t->fence);
    t->phase = result == VK_SUCCESS ? TargetPending : result == VK_ERROR_DEVICE_LOST ? TargetLost : TargetUncertain;
    return result;
}
VkResult mtvk_vk_offscreen_wait(mtvk_vk_offscreen *t, uint64_t timeout_ns) {
    if (!t || timeout_ns > MTVK_VK_OFFSCREEN_MAX_WAIT_NS) return VK_ERROR_INITIALIZATION_FAILED;
    if (t->phase == TargetLost) return VK_ERROR_DEVICE_LOST;
    if (t->phase == TargetComplete) return VK_SUCCESS;
    if (t->phase != TargetPending) return VK_NOT_READY;
    const VkResult result = vkWaitForFences(t->device, 1, &t->fence, VK_TRUE, timeout_ns);
    if (result == VK_SUCCESS) t->phase = TargetComplete;
    else if (result == VK_ERROR_DEVICE_LOST) t->phase = TargetLost;
    return result;
}
VkResult mtvk_vk_offscreen_copy(mtvk_vk_offscreen *t, void *bytes, size_t capacity) {
    if (!t || capacity < t->bytes || !mtvk_vk_context_output_valid(t->context, bytes, capacity) ||
        mtvk_spans_overlap(t, sizeof(*t), bytes, capacity)) return VK_ERROR_INITIALIZATION_FAILED;
    if (t->phase == TargetLost) return VK_ERROR_DEVICE_LOST;
    if (t->phase != TargetComplete) return VK_NOT_READY;
    void *mapped = NULL;
    VkResult result = vkMapMemory(t->device, t->readback_memory, 0, VK_WHOLE_SIZE, 0, &mapped);
    if (result != VK_SUCCESS) return remember_loss(t, result);
    if (!mapped) { vkUnmapMemory(t->device, t->readback_memory); return VK_ERROR_MEMORY_MAP_FAILED; }
    if (!t->coherent) {
        VkMappedMemoryRange range = { .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = t->readback_memory, .offset = 0, .size = VK_WHOLE_SIZE };
        result = vkInvalidateMappedMemoryRanges(t->device, 1, &range);
    }
    if (result == VK_SUCCESS) memcpy(bytes, mapped, t->bytes);
    vkUnmapMemory(t->device, t->readback_memory);
    return remember_loss(t, result);
}
VkResult mtvk_vk_offscreen_drain(mtvk_vk_offscreen *t) {
    if (!t) return VK_ERROR_INITIALIZATION_FAILED;
    if (t->phase == TargetLost) return VK_ERROR_DEVICE_LOST;
    if (t->phase != TargetPending && t->phase != TargetUncertain) return VK_SUCCESS;
    const VkResult result = vkDeviceWaitIdle(t->device);
    if (result == VK_SUCCESS) t->phase = t->phase == TargetPending ? TargetComplete : TargetFailed;
    else if (result == VK_ERROR_DEVICE_LOST) t->phase = TargetLost;
    return result;
}
VkResult mtvk_vk_offscreen_close(mtvk_vk_offscreen *t) {
    if (!t) return VK_SUCCESS;
    if (t->phase == TargetPending || t->phase == TargetUncertain) return VK_NOT_READY;
    destroy_target(t);
    return VK_SUCCESS;
}
