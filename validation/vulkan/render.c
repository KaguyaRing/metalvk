// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
/* Real offscreen graphics pipeline on a hardware Vulkan device: render a
 * known four-quadrant pattern, copy it back and verify exact pixel values.
 * This proves the rendering path (graphics queue, render pass, pipeline,
 * draw, barrier, readback) executes on the GPU, not just compute.
 *
 * The context interface is declared here instead of including context.h: this
 * platform toolchain intermittently fails to register context.h's prototypes
 * for a newly added translation unit. All context accessor calls are confined
 * to open_graphics()/close_graphics(); the pipeline code only sees handles.
 * Signatures must match context.h exactly. */
#include "context.h"
#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern VkResult mtvk_vk_open(uint32_t vendor, bool validation, mtvk_vk_context **out);
extern VkResult mtvk_vk_close(mtvk_vk_context *context);
extern VkDevice mtvk_vk_device(const mtvk_vk_context *context);
extern VkPhysicalDevice mtvk_vk_physical(const mtvk_vk_context *context);
extern VkQueue mtvk_vk_queue(const mtvk_vk_context *context);
extern uint32_t mtvk_vk_queue_family(const mtvk_vk_context *context);
extern const VkPhysicalDeviceProperties *mtvk_vk_properties(const mtvk_vk_context *context);
extern const VkPhysicalDeviceDriverProperties *mtvk_vk_driver(const mtvk_vk_context *context);
extern const VkPhysicalDeviceMemoryProperties *mtvk_vk_memory(const mtvk_vk_context *context);

#define MTVK_RENDER_EXTENT 64u
#define MTVK_RENDER_FORMAT VK_FORMAT_R8G8B8A8_UNORM
#define MTVK_RENDER_FENCE_NS UINT64_C(5000000000)

typedef struct { uint8_t r, g, b, a; } mtvk_pixel;
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

/* Quadrants in framebuffer space: (x<0.5,y<0.5), (x>=0.5,y<0.5),
 * (x<0.5,y>=0.5), (x>=0.5,y>=0.5). Registers are 8-bit UNORM, so 0.0->0 and
 * 1.0->255 exactly. */
static const mtvk_pixel kExpected[4] = {
    {0, 0, 0, 255}, {255, 0, 0, 255}, {0, 255, 0, 255}, {255, 255, 0, 255}
};

static bool open_graphics(uint32_t vendor, mtvk_render_device *out)
{
    mtvk_vk_context *context = NULL;
    if (mtvk_vk_open(vendor, true, &context) != VK_SUCCESS || !context) return false;
    out->context = context;
    out->device = mtkv_vk_device(context);
    out->queue = mtkv_vk_queue(context);
    out->physical = mtkv_vk_physical(context);
    out->family = mtkv_vk_queue_family(context);
    out->properties = mtvk_vk_properties(context);
    out->driver = mtvk_vk_driver(context);
    out->memory = mtvk_vk_memory(context);
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(out->physical, &count, NULL);
    if (!count || count > MTVK_VK_QUEUE_LIMIT || out->family >= count) return false;
    VkQueueFamilyProperties families[MTVK_VK_QUEUE_LIMIT];
    vkGetPhysicalDeviceQueueFamilyProperties(out->physical, &count, families);
    return (families[out->family].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
}

static bool close_graphics(mtvk_render_device *device)
{
    if (!device->context) return true;
    VkResult result = mtkv_vk_close(device->context);
    device->context = NULL;
    return result == VK_SUCCESS;
}

static bool read_file(const char *path, uint8_t **data, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (!file) { fprintf(stderr, "cannot open %s\n", path); return false; }
    bool ok = false;
    if (fseek(file, 0, SEEK_END) == 0) {
        long length = ftell(file);
        if (length > 0 && fseek(file, 0, SEEK_SET) == 0) {
            uint8_t *buffer = malloc((size_t)length);
            if (buffer && fread(buffer, 1, (size_t)length, file) == (size_t)length) {
                *data = buffer; *size = (size_t)length; ok = true;
            } else {
                free(buffer);
            }
        }
    }
    fclose(file);
    if (!ok) fprintf(stderr, "cannot read %s\n", path);
    return ok;
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
    uint8_t *code = NULL; size_t size = 0;
    if (!read_file(path, &code, &size)) return false;
    VkShaderModuleCreateInfo info = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size, .pCode = (const uint32_t *)code };
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
    const VkDeviceSize image_bytes = (VkDeviceSize)extent * extent * 4u;
    const VkDevice device = dev->device;
    bool ok = true;
    VkImage image = VK_NULL_HANDLE; VkDeviceMemory image_memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE; VkRenderPass pass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE; VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE; VkShaderModule vert = VK_NULL_HANDLE;
    VkShaderModule frag = VK_NULL_HANDLE; VkBuffer readback = VK_NULL_HANDLE;
    VkDeviceMemory readback_memory = VK_NULL_HANDLE; VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE; VkFence fence = VK_NULL_HANDLE;
    uint8_t *mapped = NULL;

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
    VkRenderPassCreateInfo pass_info = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &attachment,
        .subpassCount = 1, .pSubpasses = &subpass };
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
    VkPipelineColorBlendAttachmentState blend = { .blendEnable = VK_FALSE, .colorWriteMask = 0xF };
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
    if (!memory_type(dev->memory, buffer_requirements.memoryTypeBits,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &type)) {
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
    VK_TRY(vkEndCommandBuffer(commands));

    VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &commands };
    VK_TRY(vkQueueSubmit(dev->queue, 1, &submit, fence));
    VK_TRY(vkWaitForFences(device, 1, &fence, VK_TRUE, MTVK_RENDER_FENCE_NS));

    VK_TRY(vkMapMemory(device, readback_memory, 0, image_bytes, 0, (void **)&mapped));
    const mtvk_pixel *pixels = (const mtvk_pixel *)mapped;
    const uint32_t quarter = extent / 4u;
    const uint32_t three_quarter = (extent * 3u) / 4u;
    const uint32_t sx[4] = { quarter, three_quarter, quarter, three_quarter };
    const uint32_t sy[4] = { quarter, quarter, three_quarter, three_quarter };
    for (uint32_t q = 0; q < 4; ++q) {
        uint32_t index = sy[q] * extent + sx[q];
        mtvk_pixel got = pixels[index];
        const mtvk_pixel *want = &kExpected[q];
        if (got.r != want->r || got.g != want->g || got.b != want->b || got.a != want->a) {
            fprintf(stderr, "quadrant %u at (%u,%u): got %u,%u,%u,%u want %u,%u,%u,%u\n",
                    q, sx[q], sy[q], got.r, got.g, got.b, got.a, want->r, want->g, want->b, want->a);
            ok = false;
        } else {
            printf("quadrant %u at (%u,%u): %u,%u,%u,%u correct\n", q, sx[q], sy[q],
                   got.r, got.g, got.b, got.a);
        }
    }
    if (mapped) { vkUnmapMemory(device, readback_memory); mapped = NULL; }

cleanup:
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
    uint32_t vendor = (uint32_t)strtoul(argv[3], NULL, 0);
    mtvk_render_device dev = {0};
    if (!open_graphics(vendor, &dev)) {
        fprintf(stderr, "no graphics-capable hardware device for vendor 0x%04x\n", vendor);
        (void)close_graphics(&dev);
        return 1;
    }
    printf("device: %s (0x%04x:0x%04x, driver %s)\n", dev.properties->deviceName,
           dev.properties->vendorID, dev.properties->deviceID, dev.driver->driverName);
    int result = render_pattern(&dev, argv[1], argv[2]);
    if (!close_graphics(&dev)) { fprintf(stderr, "context close failed\n"); result = 1; }
    if (result == 0) printf("real graphics pipeline verified on hardware\n");
    return result;
}
