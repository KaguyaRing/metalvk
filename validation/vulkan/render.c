// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
/* Real native Vulkan pipeline on library-owned offscreen targets. The library
 * has no knowledge of this pattern; every pixel is produced by the GPU. */
#include "vk/init.h"
#include "input.h"
#include "pattern.h"
#include <stdio.h>
#include <stdlib.h>
#define MTVK_RENDER_FENCE_NS UINT64_C(5000000000)
#define MTVK_RENDER_FRAMES 3u
static bool shader_module(VkDevice device, const char *path, VkShaderModule *out) {
    size_t size = 0;
    uint32_t *code = mtvk_probe_read_shader(path, &size);
    if (!code) { fprintf(stderr, "invalid or unreadable SPIR-V: %s\n", path); return false; }
    VkShaderModuleCreateInfo info = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size, .pCode = code };
    VkShaderModule module = VK_NULL_HANDLE;
    const VkResult result = vkCreateShaderModule(device, &info, NULL, &module);
    free(code);
    if (result != VK_SUCCESS) fprintf(stderr, "vkCreateShaderModule -> %d\n", (int)result);
    else *out = module;
    return result == VK_SUCCESS;
}
#define VK_TRY(call) do { const VkResult r = (call); if (r != VK_SUCCESS) { \
    fprintf(stderr, "%s -> VkResult %d\n", #call, (int)r); ok = false; goto cleanup; } } while (0)
static int render_pattern(mtvk_vk_context *context, const char *vertex, const char *fragment) {
    const VkDevice device = mtvk_vk_device(context);
    const uint32_t extent = MTVK_PATTERN_EXTENT;
    uint8_t pixels[MTVK_PATTERN_EXTENT * MTVK_PATTERN_EXTENT * MTVK_PATTERN_CHANNELS];
    mtvk_vk_offscreen *target = NULL;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
    bool ok = true, attempted = false;
    VK_TRY(mtvk_vk_offscreen_open(context, extent, extent, &target));
    // A live child must prevent even an otherwise idle parent from destruction.
    if (mtvk_vk_close(context) != VK_NOT_READY) { ok = false; goto cleanup; }
    if (!shader_module(device, vertex, &vert) || !shader_module(device, fragment, &frag)) {
        ok = false; goto cleanup;
    }
    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main" } };
    VkPipelineVertexInputStateCreateInfo input = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo assembly = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkViewport viewport = {0.0f, 0.0f, (float)extent, (float)extent, 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, {extent, extent}};
    VkPipelineViewportStateCreateInfo viewport_state = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor };
    VkPipelineRasterizationStateCreateInfo raster = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
    VkPipelineMultisampleStateCreateInfo multisample = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineColorBlendAttachmentState blend = { .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };
    VkPipelineColorBlendStateCreateInfo color_blend = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &blend };
    VkPipelineLayoutCreateInfo layout_info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    VK_TRY(vkCreatePipelineLayout(device, &layout_info, NULL, &layout));
    VkGraphicsPipelineCreateInfo pipeline_info = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &input,
        .pInputAssemblyState = &assembly, .pViewportState = &viewport_state,
        .pRasterizationState = &raster, .pMultisampleState = &multisample,
        .pColorBlendState = &color_blend, .layout = layout,
        .renderPass = mtvk_vk_offscreen_render_pass(target) };
    VK_TRY(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline));
    for (unsigned frame = 0; frame < MTVK_RENDER_FRAMES; ++frame) {
        const VkClearColorValue clear = {{0.0f, 0.0f, 0.0f, 1.0f}};
        VkCommandBuffer commands = VK_NULL_HANDLE;
        VK_TRY(mtvk_vk_offscreen_begin(target, &clear, &commands));
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdDraw(commands, 3, 1, 0, 0);
        attempted = true;
        VK_TRY(mtvk_vk_offscreen_submit(target));
        // In-flight ownership is retained even if the GPU completes immediately.
        if (mtvk_vk_offscreen_close(target) != VK_NOT_READY) { ok = false; goto cleanup; }
        VK_TRY(mtvk_vk_offscreen_wait(target, MTVK_RENDER_FENCE_NS));
        attempted = false;
        VK_TRY(mtvk_vk_offscreen_copy(target, pixels, sizeof(pixels)));
        size_t mismatch = SIZE_MAX;
        if (!mtvk_probe_check_pattern(pixels, sizeof(pixels), extent, extent, &mismatch)) {
            fprintf(stderr, "frame %u: full-image verification failed at pixel %zu\n", frame, mismatch);
            ok = false; goto cleanup;
        }
        printf("PASS library frame %u: %u pixels verified\n", frame, extent * extent);
    }
cleanup:
    if (attempted) {
        const VkResult drain = mtvk_vk_offscreen_drain(target);
        if (drain != VK_SUCCESS && drain != VK_ERROR_DEVICE_LOST) {
            fprintf(stderr, "drain failed: %d; preserving target/pipeline/context until process exit\n", (int)drain);
            return 3;
        }
    }
    if (mtvk_vk_offscreen_close(target) != VK_SUCCESS) return 3;
    if (pipeline) vkDestroyPipeline(device, pipeline, NULL);
    if (layout) vkDestroyPipelineLayout(device, layout, NULL);
    if (vert) vkDestroyShaderModule(device, vert, NULL);
    if (frag) vkDestroyShaderModule(device, frag, NULL);
    return ok ? 0 : 1;
}
int main(int argc, char **argv) {
    if (argc != 4) { fprintf(stderr, "usage: %s <pattern.vert.spv> <pattern.frag.spv> <vendor hex>\n", argv[0]); return 2; }
    uint32_t vendor = 0;
    if (!mtvk_probe_parse_vendor(argv[3], &vendor)) { fputs("invalid PCI vendor\n", stderr); return 2; }
    mtvk_vk_context *context = NULL;
    const VkResult opened = mtvk_vk_open_for_queues(vendor, true, VK_QUEUE_GRAPHICS_BIT, &context);
    if (opened != VK_SUCCESS) { fprintf(stderr, "no graphics hardware: VkResult %d\n", (int)opened); return 1; }
    const VkPhysicalDeviceProperties *properties = mtvk_vk_properties(context);
    printf("device: %s (0x%04x:0x%04x, driver %s)\n", properties->deviceName,
        properties->vendorID, properties->deviceID, mtvk_vk_driver(context)->driverName);
    int result = render_pattern(context, argv[1], argv[2]);
    if (result == 3) return 1;
    printf("Validation errors: %u; warnings: %u\n", mtvk_vk_validation_errors(context), mtvk_vk_validation_warnings(context));
    if (mtvk_vk_close(context) != VK_SUCCESS) { fputs("context close failed\n", stderr); result = 1; }
    if (!result) puts("real library graphics pipeline verified on hardware (not Metal or macOS NVIDIA support)");
    return result;
}
