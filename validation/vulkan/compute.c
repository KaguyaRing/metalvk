// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
// Integration test against a real hardware Vulkan ICD; no mock or Metal backend.
#include "vk/init.h"
#include "input.h"
#include <stdio.h>
#include <stdlib.h>

enum { ELEMENT_CAPACITY = 4097, LOCAL_SIZE = 64 };
static const uint64_t FenceTimeout = UINT64_C(5000000000);
static const uint32_t Sentinel = UINT32_C(0xdeadbeef);
typedef struct buffer {
    VkBuffer handle;
    VkDeviceMemory memory;
    uint32_t *mapped;
    bool coherent;
} buffer;
typedef struct resources {
    mtvk_vk_context *context;
    buffer buffers[2];
    VkShaderModule shader;
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkDescriptorSetLayout set_layout;
    VkDescriptorPool descriptor_pool;
    VkCommandPool command_pool;
    VkFence fence;
    bool submitted;
} resources;

static VkResult make_buffer(mtvk_vk_context *context, buffer *out) {
    const VkDevice device = mtvk_vk_device(context);
    VkBufferCreateInfo info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = ELEMENT_CAPACITY * sizeof(uint32_t), .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    VkBuffer handle = VK_NULL_HANDLE;
    VkResult result = vkCreateBuffer(device, &info, NULL, &handle);
    if (result != VK_SUCCESS) return result;
    out->handle = handle;
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, handle, &requirements);
    const VkPhysicalDeviceMemoryProperties *properties = mtvk_vk_memory(context);
    uint32_t index = UINT32_MAX;
    for (uint32_t i = 0; i < properties->memoryTypeCount; ++i) {
        VkMemoryPropertyFlags flags = properties->memoryTypes[i].propertyFlags;
        if ((requirements.memoryTypeBits & (1u << i)) && (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            index = i;
            if (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) break;
        }
    }
    if (index == UINT32_MAX) return VK_ERROR_FEATURE_NOT_PRESENT;
    out->coherent = (properties->memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    VkMemoryAllocateInfo allocate = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = index };
    VkDeviceMemory memory = VK_NULL_HANDLE;
    result = vkAllocateMemory(device, &allocate, NULL, &memory);
    if (result != VK_SUCCESS) return result;
    out->memory = memory;
    result = vkBindBufferMemory(device, handle, memory, 0);
    if (result != VK_SUCCESS) return result;
    void *mapped = NULL;
    result = vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapped);
    if (result == VK_SUCCESS) out->mapped = mapped;
    return result;
}
static VkResult cache_operation(VkDevice device, const buffer *b, bool invalidate) {
    if (b->coherent) return VK_SUCCESS;
    VkMappedMemoryRange range = { .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = b->memory, .offset = 0, .size = VK_WHOLE_SIZE };
    return invalidate ? vkInvalidateMappedMemoryRanges(device, 1, &range) :
                        vkFlushMappedMemoryRanges(device, 1, &range);
}
static VkResult release(resources *r) {
    VkDevice device = mtvk_vk_device(r->context);
    if (device && r->submitted) {
        // A timeout does NOT mean work completed. Drain before freeing any
        // in-flight object. The process/tool timeout is an outer fault boundary.
        VkResult result = vkDeviceWaitIdle(device);
        if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST) return result;
    }
    if (r->fence) vkDestroyFence(device, r->fence, NULL);
    if (r->command_pool) vkDestroyCommandPool(device, r->command_pool, NULL);
    if (r->pipeline) vkDestroyPipeline(device, r->pipeline, NULL);
    if (r->layout) vkDestroyPipelineLayout(device, r->layout, NULL);
    if (r->descriptor_pool) vkDestroyDescriptorPool(device, r->descriptor_pool, NULL);
    if (r->set_layout) vkDestroyDescriptorSetLayout(device, r->set_layout, NULL);
    if (r->shader) vkDestroyShaderModule(device, r->shader, NULL);
    for (size_t i = 0; i < 2; ++i) {
        if (r->buffers[i].mapped) vkUnmapMemory(device, r->buffers[i].memory);
        if (r->buffers[i].handle) vkDestroyBuffer(device, r->buffers[i].handle, NULL);
        if (r->buffers[i].memory) vkFreeMemory(device, r->buffers[i].memory, NULL);
    }
    return VK_SUCCESS;
}
#define VK_TRY(call) do { result = (call); if (result != VK_SUCCESS) { \
    fprintf(stderr, "%s: VkResult %d\n", #call, result); goto done; } } while (0)
#define VK_OWN(member, type, call) do { type handle = VK_NULL_HANDLE; VK_TRY(call); \
    r.member = handle; } while (0)

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: vulkan-compute validated-shader.spv [vendor-id, default 0x10de]\n");
        return 64;
    }
    uint32_t vendor = 0x10de;
    if (argc == 3) {
        if (!mtvk_probe_parse_vendor(argv[2], &vendor)) return 64;
    }
    resources r = {0};
    size_t shader_size = 0;
    uint32_t *code = mtvk_probe_read_shader(argv[1], &shader_size);
    if (!code) { fprintf(stderr, "Shader file invalid, unreadable or exceeds the size limit.\n"); return 1; }
    VkResult result = mtvk_vk_open(vendor, true, &r.context);
    bool passed = false;
    if (result != VK_SUCCESS) { fprintf(stderr, "Vulkan context: VkResult %d\n", result); goto done; }
    const VkDevice device = mtvk_vk_device(r.context);
    const VkPhysicalDeviceProperties *properties = mtvk_vk_properties(r.context);
    const VkPhysicalDeviceDriverProperties *driver = mtvk_vk_driver(r.context);
    printf("Device: %s (%04x:%04x), API %u.%u.%u\nDriver: %s / %s (driverID %d)\n",
        properties->deviceName, properties->vendorID, properties->deviceID,
        VK_API_VERSION_MAJOR(properties->apiVersion), VK_API_VERSION_MINOR(properties->apiVersion),
        VK_API_VERSION_PATCH(properties->apiVersion), driver->driverName, driver->driverInfo, driver->driverID);
    puts("Khronos validation and synchronization validation enabled; software devices rejected.");
    if (properties->limits.maxComputeWorkGroupInvocations < LOCAL_SIZE ||
        properties->limits.maxComputeWorkGroupSize[0] < LOCAL_SIZE ||
        properties->limits.maxComputeWorkGroupCount[0] < (ELEMENT_CAPACITY + LOCAL_SIZE - 1) / LOCAL_SIZE ||
        properties->limits.maxStorageBufferRange < ELEMENT_CAPACITY * sizeof(uint32_t) ||
        properties->limits.maxPushConstantsSize < 2 * sizeof(uint32_t)) {
        result = VK_ERROR_FEATURE_NOT_PRESENT; goto done;
    }
    VK_TRY(make_buffer(r.context, &r.buffers[0]));
    VK_TRY(make_buffer(r.context, &r.buffers[1]));
    VkShaderModuleCreateInfo shader = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shader_size, .pCode = code };
    VK_OWN(shader, VkShaderModule, vkCreateShaderModule(device, &shader, NULL, &handle));
    if (mtvk_vk_validation_errors(r.context)) goto done;
    VkDescriptorSetLayoutBinding bindings[2] = {
        { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
        { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT } };
    VkDescriptorSetLayoutCreateInfo set = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2, .pBindings = bindings };
    VK_OWN(set_layout, VkDescriptorSetLayout, vkCreateDescriptorSetLayout(device, &set, NULL, &handle));
    VkPushConstantRange push = { .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .size = 2 * sizeof(uint32_t) };
    VkPipelineLayoutCreateInfo layout = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &r.set_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &push };
    VK_OWN(layout, VkPipelineLayout, vkCreatePipelineLayout(device, &layout, NULL, &handle));
    VkComputePipelineCreateInfo pipeline = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = r.shader, .pName = "main" },
        .layout = r.layout };
    VK_OWN(pipeline, VkPipeline, vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline, NULL, &handle));
    VkDescriptorPoolSize pool_size = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 };
    VkDescriptorPoolCreateInfo pool = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &pool_size };
    VK_OWN(descriptor_pool, VkDescriptorPool, vkCreateDescriptorPool(device, &pool, NULL, &handle));
    VkDescriptorSetAllocateInfo set_allocate = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = r.descriptor_pool, .descriptorSetCount = 1, .pSetLayouts = &r.set_layout };
    VkDescriptorSet descriptor;
    VK_TRY(vkAllocateDescriptorSets(device, &set_allocate, &descriptor));
    VkDescriptorBufferInfo buffer_info[2]; VkWriteDescriptorSet writes[2];
    for (uint32_t i = 0; i < 2; ++i) {
        buffer_info[i] = (VkDescriptorBufferInfo){ r.buffers[i].handle, 0, ELEMENT_CAPACITY * sizeof(uint32_t) };
        writes[i] = (VkWriteDescriptorSet){ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor, .dstBinding = i, .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &buffer_info[i] };
    }
    vkUpdateDescriptorSets(device, 2, writes, 0, NULL);
    VkCommandPoolCreateInfo command_pool = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = mtvk_vk_queue_family(r.context) };
    VK_OWN(command_pool, VkCommandPool, vkCreateCommandPool(device, &command_pool, NULL, &handle));
    VkCommandBufferAllocateInfo command_allocate = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = r.command_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer command;
    VK_TRY(vkAllocateCommandBuffers(device, &command_allocate, &command));
    VkFenceCreateInfo fence = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VK_OWN(fence, VkFence, vkCreateFence(device, &fence, NULL, &handle));
    const uint32_t counts[] = {1, 63, 64, 65, 257, ELEMENT_CAPACITY};
    for (size_t test = 0; test < sizeof(counts) / sizeof(counts[0]); ++test) {
        const uint32_t salt = UINT32_C(0x12345678) + (uint32_t)test;
        for (uint32_t i = 0; i < ELEMENT_CAPACITY; ++i) {
            r.buffers[0].mapped[i] = i ^ salt;
            r.buffers[1].mapped[i] = Sentinel;
        }
        for (size_t i = 0; i < 2; ++i) VK_TRY(cache_operation(device, &r.buffers[i], false));
        VK_TRY(vkResetCommandBuffer(command, 0));
        VK_TRY(vkResetFences(device, 1, &r.fence));
        VkCommandBufferBeginInfo begin = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
        VK_TRY(vkBeginCommandBuffer(command, &begin));
        VkMemoryBarrier to_gpu = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT };
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &to_gpu, 0, NULL, 0, NULL);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, r.layout, 0, 1, &descriptor, 0, NULL);
        uint32_t parameters[2] = { counts[test], salt };
        vkCmdPushConstants(command, r.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(parameters), parameters);
        vkCmdDispatch(command, (counts[test] + LOCAL_SIZE - 1) / LOCAL_SIZE, 1, 1);
        VkMemoryBarrier to_host = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 1, &to_host, 0, NULL, 0, NULL);
        VK_TRY(vkEndCommandBuffer(command));
        VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1, .pCommandBuffers = &command };
        VK_TRY(vkQueueSubmit(mtvk_vk_queue(r.context), 1, &submit, r.fence));
        r.submitted = true;
        VK_TRY(vkWaitForFences(device, 1, &r.fence, VK_TRUE, FenceTimeout));
        r.submitted = false;
        VK_TRY(cache_operation(device, &r.buffers[1], true));
        for (uint32_t i = 0; i < ELEMENT_CAPACITY; ++i) {
            uint32_t expected = i < counts[test] ? (i ^ salt) ^
                ((i * UINT32_C(1664525) + salt) ^ UINT32_C(0xa5a55a5a)) : Sentinel;
            if (r.buffers[1].mapped[i] != expected) {
                fprintf(stderr, "Readback mismatch: test %zu index %u\n", test, i); goto done;
            }
        }
        printf("PASS real Vulkan compute: %u elements, untouched tail verified\n", counts[test]);
    }
    passed = true;
done:
    free(code);
    VkResult cleanup = release(&r);
    if (cleanup != VK_SUCCESS) {
        // Do not destroy still-in-flight resources on an unexpected drain error.
        // OS process teardown owns the remaining driver context in this failure path.
        fprintf(stderr, "Drain failed: %d; preserving in-flight resources until process exit.\n", cleanup);
        return 1;
    }
    uint32_t errors = mtvk_vk_validation_errors(r.context);
    uint32_t warnings = mtvk_vk_validation_warnings(r.context);
    cleanup = mtvk_vk_close(r.context);
    printf("Validation errors: %u; warnings: %u\n", errors, warnings);
    return passed && result == VK_SUCCESS && cleanup == VK_SUCCESS && !errors && !warnings ? 0 : 1;
}
