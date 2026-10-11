// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
/* Real offscreen graphics pipeline on a hardware Vulkan device: render a
 * known four-quadrant pattern, copy it back and verify exact pixel values.
 * This proves the rendering path (graphics queue, render pass, pipeline,
 * draw, barrier, readback) executes on the GPU, not just compute. */
#include "vk/init.h"
#include "input.h"
#include "pattern.h"
#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define MTVK_RENDER_EXTENT MTVK_PATTERN_EXTENT
#define MTVK_RENDER_FORMAT VK_FORMAT_R8G8B8A8_UNORM
#define MTVK_RENDER_FENCE_NS UINT64_C(5000000000)
typedef struct {
    mtvk_vk_context *context;
    VkDevice device;
    VkQueue queue;
    VkPhysicalDevice physical;
    uint32_t family;
    const VkPhysicalDeviceProperties *properties;
    const VkPhysicalDeviceDriverProperties *driver;
    const VkPhysicalDeviceMemoryProperties *memory;
} mtvk_render_device;

static bool open_graphics(uint32_t vendor, mtvk_render_device *out)
{
    mtvk_vk_context *context = NULL;
    if (!out || mtvk_vk_open_for_queues(vendor, true, VK_QUEUE_GRAPHICS_BIT, &context) != VK_SUCCESS ||
        !context) return false;
    out->context = context;
    out->device = mtvk_vk_device(context);
    out->queue = mtvk_vk_queue(context);
    out->physical = mtvk_vk_physical(context);
    out->family = mtvk_vk_queue_family(context);
    out->properties = mtvk_vk_properties(context);
    out->driver = mtvk_vk_driver(context);
    out->memory = mtvk_vk_memory(context);
    return true;
}

static bool close_graphics(mtvk_render_device *device)
{
    if (!device->context) return true;
    VkResult result = mtvk_vk_close(device->context);
    if (result == VK_SUCCESS || result == VK_ERROR_DEVICE_LOST ||
        result == VK_ERROR_VALIDATION_FAILED_EXT) device->context = NULL;
    return result == VK_SUCCESS;
}

static bool memory_type(const VkPhysicalDeviceMemoryProperties *memory,
                        uint32_t type_bits, VkMemoryPropertyFlags want, uint32_t *index)
{
    for (uint32_t i = 0; i < memory->memoryTypeCount; ++i)
        if ((type_bits & (1u << i)) &&
            (memory->memoryTypes[i].propertyFlags & want) == want) {
            *index = i; return true;
        }
    return false;
}

static bool shader_module(VkDevice device, const char *path, VkShaderModule *out)
{
    size_t size = 0;
    uint32_t *code = mtvk_probe_read_shader(path, &size);
    if (!code) { fprintf(stderr, "invalid or unreadable SPIR-V: %s\n", path); return false; }
    VkShaderModuleCreateInfo info = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size, .pCode = code };
    VkResult result = vkCreateShaderModule(device, &info, NULL, out);
    free(code);
    if (result != VK_SUCCESS) { fprintf(stderr, "vkCreateShaderModule(%s) -> %d\n", path, (int)result); return false; }
    return true;
}

#define VK_TRY(expression)                                                    \
    do {                                                                      \
        VkResult vk_try_result = (expression);                                \
        if (vk_try_result != VK_SUCCESS) {                                    \
            fprintf(stderr, "%s -> VkResult %d\n", #expression,               \
                    (int)vk_try_result);                                      \
            ok = false;                                                       \
            goto cleanup;                                                     \
        }                                                                     \
    } while (0)

static int render_pattern(const mtvk_render_device *dev, const char *vert_path, const char *frag_path)
{
    const uint32_t extent = MTVK_RENDER_EXTENT;
    const VkDeviceSize image_bytes = (VkDeviceSize)extent * extent * MTVK_PATTERN_CHANNELS;
    const VkDevice device = dev->device;
    bool ok = true;
    bool submitted = false;
    VkImage image = VK_NULL_HANDLE; VkDeviceMemory image_memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE; VkRenderPass pass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE; VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE; VkShaderModule vert = VK_NULL_HANDLE;
    VkShaderModule frag = VK_NULL_HANDLE; VkBuffer readback = VK_NULL_HANDLE;
    VkDeviceMemory readback_memory = VK_NULL_HANDLE; VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE; VkFence fence = VK_NULL_HANDLE;
    void *mapped = NULL;
    bool coherent = false;

    VkFormatProperties format;
    vkGetPhysicalDeviceFormatProperties(dev->physical, MTVK_RENDER_FORMAT, &format);
    const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    if ((format.optimalTilingFeatures & required) != required ||
        dev->properties->limits.maxFramebufferWidth < extent ||
        dev->properties->limits.maxFramebufferHeight < extent) {
        fprintf(stderr, "device cannot render the required offscreen format/extent\n"); return 1;
    }

    VkImageCreateInfo image_info = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = MTVK_RENDER_FORMAT,
        .extent = { extent, extent, 1 }, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
    VK_TRY(vkCreateImage(device, &image_info, NULL, &image));
    VkMemoryRequirements requirements; vkGetImageMemoryRequirements(device, image, &requirements);
    uint32_t type = 0;
    if (!memory_type(dev->memory, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &type)) {
        fprintf(stderr, "no device-local memory type for the target image\n"); ok = false; goto cleanup;
    }
    VkMemoryAllocateInfo image_alloc = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = type };
    VK_TRY(vkAllocateMemory(device, &image_alloc, NULL, &image_memory));
    VK_TRY(vkBindImageMemory(device, image, image_memory, 0));

    VkImageViewCreateInfo view_info = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = MTVK_RENDER_FORMAT,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    VK_TRY(vkCreateImageView(device, &view_info, NULL, &view));

    VkAttachmentDescription attachment = { .format = MTVK_RENDER_FORMAT,
        .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL };
    VkAttachmentReference reference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1, .pColorAttachments = &reference };
    VkSubpassDependency dependency = { .srcSubpass = 0, .dstSubpass = VK_SUBPASS_EXTERNAL,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT };
    VkRenderPassCreateInfo pass_info = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &attachment,
        .subpassCount = 1, .pSubpasses = &subpass,
        .dependencyCount = 1, .pDependencies = &dependency };
    VK_TRY(vkCreateRenderPass(device, &pass_info, NULL, &pass));

    VkFramebufferCreateInfo framebuffer_info = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = pass, .attachmentCount = 1, .pAttachments = &view,
        .width = extent, .height = extent, .layers = 1 };
    VK_TRY(vkCreateFramebuffer(device, &framebuffer_info, NULL, &framebuffer));

    if (!shader_module(device, vert_path, &vert) || !shader_module(device, frag_path, &frag)) { ok = false; goto cleanup; }
    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main" } };
    VkPipelineVertexInputStateCreateInfo vertex_input = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo assembly = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkViewport viewport = { 0.0f, 0.0f, (float)extent, (float)extent, 0.0f, 1.0f };
    VkRect2D scissor = { { 0, 0 }, { extent, extent } };
    VkPipelineViewportStateCreateInfo viewport_state = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor };
    VkPipelineRasterizationStateCreateInfo raster = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
    VkPipelineMultisampleStateCreateInfo multisample = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineColorBlendAttachmentState blend = { .blendEnable = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };
    VkPipelineColorBlendStateCreateInfo color_blend = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &blend };
    VkPipelineLayoutCreateInfo layout_info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    VK_TRY(vkCreatePipelineLayout(device, &layout_info, NULL, &layout));
    VkGraphicsPipelineCreateInfo pipeline_info = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &assembly, .pViewportState = &viewport_state,
        .pRasterizationState = &raster, .pMultisampleState = &multisample,
        .pColorBlendState = &color_blend, .layout = layout, .renderPass = pass, .subpass = 0 };
    VK_TRY(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline));

    VkBufferCreateInfo buffer_info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = image_bytes, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    VK_TRY(vkCreateBuffer(device, &buffer_info, NULL, &readback));
    VkMemoryRequirements buffer_requirements; vkGetBufferMemoryRequirements(device, readback, &buffer_requirements);
    coherent = memory_type(dev->memory, buffer_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &type);
    if (!coherent && !memory_type(dev->memory, buffer_requirements.memoryTypeBits,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &type)) {
        fprintf(stderr, "no host-visible memory type for the readback buffer\n"); ok = false; goto cleanup;
    }
    VkMemoryAllocateInfo buffer_alloc = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = buffer_requirements.size, .memoryTypeIndex = type };
    VK_TRY(vkAllocateMemory(device, &buffer_alloc, NULL, &readback_memory));
    VK_TRY(vkBindBufferMemory(device, readback, readback_memory, 0));

    VkCommandPoolCreateInfo pool_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, .queueFamilyIndex = dev->family };
    VK_TRY(vkCreateCommandPool(device, &pool_info, NULL, &pool));
    VkCommandBufferAllocateInfo allocate = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VK_TRY(vkAllocateCommandBuffers(device, &allocate, &commands));
    VkFenceCreateInfo fence_info = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VK_TRY(vkCreateFence(device, &fence_info, NULL, &fence));

    VkCommandBufferBeginInfo begin = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    VK_TRY(vkBeginCommandBuffer(commands, &begin));
    VkClearValue clear = { .color = { { 0.0f, 0.0f, 0.0f, 1.0f } } };
    VkRenderPassBeginInfo render_begin = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = pass, .framebuffer = framebuffer, .renderArea = { { 0, 0 }, { extent, extent } },
        .clearValueCount = 1, .pClearValues = &clear };
    vkCmdBeginRenderPass(commands, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRenderPass(commands);
    VkBufferImageCopy region = { .bufferOffset = 0, .bufferRowLength = 0, .bufferImageHeight = 0,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset = { 0, 0, 0 }, .imageExtent = { extent, extent, 1 } };
    vkCmdCopyImageToBuffer(commands, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &region);
    VkMemoryBarrier to_host = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 1, &to_host, 0, NULL, 0, NULL);
    VK_TRY(vkEndCommandBuffer(commands));

    VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &commands };
    VK_TRY(vkQueueSubmit(dev->queue, 1, &submit, fence));
    submitted = true;
    VK_TRY(vkWaitForFences(device, 1, &fence, VK_TRUE, MTVK_RENDER_FENCE_NS));
    submitted = false;

    VK_TRY(vkMapMemory(device, readback_memory, 0, VK_WHOLE_SIZE, 0, &mapped));
    if (!coherent) {
        VkMappedMemoryRange range = { .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = readback_memory, .offset = 0, .size = VK_WHOLE_SIZE };
        VK_TRY(vkInvalidateMappedMemoryRanges(device, 1, &range));
    }
    size_t mismatch = SIZE_MAX;
    ok = mtvk_probe_check_pattern(mapped, (size_t)image_bytes, extent, extent, &mismatch);
    if (!ok) fprintf(stderr, "full-image verification failed at pixel %zu\n", mismatch);
    if (mapped) { vkUnmapMemory(device, readback_memory); mapped = NULL; }
    if (ok) printf("PASS full image: %u pixels verified\n", extent * extent);

cleanup:
    if (submitted) {
        const VkResult drain = vkDeviceWaitIdle(device);
        if (drain != VK_SUCCESS && drain != VK_ERROR_DEVICE_LOST) {
            fprintf(stderr, "drain failed: %d; preserving resources until process exit\n", (int)drain);
            return 3;
        }
    }
    if (mapped) vkUnmapMemory(device, readback_memory);
    if (fence) vkDestroyFence(device, fence, NULL);
    if (pool) vkDestroyCommandPool(device, pool, NULL);
    if (readback) vkDestroyBuffer(device, readback, NULL);
    if (readback_memory) vkFreeMemory(device, readback_memory, NULL);
    if (pipeline) vkDestroyPipeline(device, pipeline, NULL);
    if (layout) vkDestroyPipelineLayout(device, layout, NULL);
    if (vert) vkDestroyShaderModule(device, vert, NULL);
    if (frag) vkDestroyShaderModule(device, frag, NULL);
    if (framebuffer) vkDestroyFramebuffer(device, framebuffer, NULL);
    if (pass) vkDestroyRenderPass(device, pass, NULL);
    if (view) vkDestroyImageView(device, view, NULL);
    if (image) vkDestroyImage(device, image, NULL);
    if (image_memory) vkFreeMemory(device, image_memory, NULL);
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s <pattern.vert.spv> <pattern.frag.spv> <vendor hex>\n", argv[0]);
        return 2;
    }
    uint32_t vendor = 0;
    if (!mtvk_probe_parse_vendor(argv[3], &vendor)) {
        fprintf(stderr, "invalid PCI vendor: %s\n", argv[3]); return 2;
    }
    mtvk_render_device dev = {0};
    if (!open_graphics(vendor, &dev)) {
        fprintf(stderr, "no graphics-capable hardware device for vendor 0x%04x\n", vendor);
        (void)close_graphics(&dev);
        return 1;
    }
    printf("device: %s (0x%04x:0x%04x, driver %s)\n", dev.properties->deviceName,
           dev.properties->vendorID, dev.properties->deviceID, dev.driver->driverName);
    int result = render_pattern(&dev, argv[1], argv[2]);
    if (result == 3) return 1; // No child destruction or context close while work may be pending.
    printf("Validation errors: %u; warnings: %u\n",
           mtvk_vk_validation_errors(dev.context), mtvk_vk_validation_warnings(dev.context));
    if (!close_graphics(&dev)) { fprintf(stderr, "context close failed\n"); result = 1; }
    if (result == 0) printf("real graphics pipeline verified on hardware\n");
    return result;
}
