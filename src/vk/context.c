// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "vk/init.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct mtvk_vk_context {
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    uint32_t family;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceDriverProperties driver;
    VkPhysicalDeviceMemoryProperties memory;
    VkDebugUtilsMessengerEXT messenger;
    PFN_vkDestroyDebugUtilsMessengerEXT destroy_messenger;
    atomic_uint errors, warnings;
};
static VKAPI_ATTR VkBool32 VKAPI_CALL validation_message(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *data, void *user) {
    mtvk_vk_context *context = user;
    (void)type;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        atomic_fetch_add_explicit(&context->errors, 1u, memory_order_relaxed);
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        atomic_fetch_add_explicit(&context->warnings, 1u, memory_order_relaxed);
    fprintf(stderr, "Vulkan validation: %s\n", data && data->pMessage ? data->pMessage : "No message");
    return VK_FALSE;
}
static VkResult require_validation(void) {
    uint32_t count = MTVK_VK_LAYER_LIMIT;
    VkLayerProperties layers[MTVK_VK_LAYER_LIMIT];
    VkResult result = vkEnumerateInstanceLayerProperties(&count, layers);
    if (result != VK_SUCCESS) return result;
    bool found = false;
    for (uint32_t i = 0; i < count; ++i)
        found |= strcmp(layers[i].layerName, "VK_LAYER_KHRONOS_validation") == 0;
    if (!found) return VK_ERROR_LAYER_NOT_PRESENT;
    count = MTVK_VK_EXTENSION_LIMIT;
    VkExtensionProperties extensions[MTVK_VK_EXTENSION_LIMIT];
    result = vkEnumerateInstanceExtensionProperties(NULL, &count, extensions);
    if (result != VK_SUCCESS) return result;
    found = false;
    for (uint32_t i = 0; i < count; ++i)
        found |= strcmp(extensions[i].extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0;
    if (!found) return VK_ERROR_EXTENSION_NOT_PRESENT;
    count = MTVK_VK_EXTENSION_LIMIT;
    result = vkEnumerateInstanceExtensionProperties("VK_LAYER_KHRONOS_validation", &count, extensions);
    if (result != VK_SUCCESS) return result;
    for (uint32_t i = 0; i < count; ++i)
        if (strcmp(extensions[i].extensionName, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) == 0)
            return VK_SUCCESS;
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}
/* Requires no pending work and no live application-owned device children.
 * Used for open failure too: that path has never submitted anything. */
static VkResult dispose(mtvk_vk_context *context) {
    VkResult result = VK_SUCCESS;
    if (context->device) vkDestroyDevice(context->device, NULL);
    if (context->messenger) {
        if (context->destroy_messenger)
            context->destroy_messenger(context->instance, context->messenger, NULL);
        else {
            fputs("Vulkan invariant failure: missing messenger destructor.\n", stderr);
            result = VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }
    if (context->instance) vkDestroyInstance(context->instance, NULL);
    if (mtvk_vk_validation_errors(context) || mtvk_vk_validation_warnings(context))
        result = VK_ERROR_VALIDATION_FAILED_EXT;
    free(context);
    return result;
}
VkResult mtvk_vk_open(uint32_t vendor, bool validation, mtvk_vk_context **out) {
    return mtvk_vk_open_for_queues(vendor, validation, VK_QUEUE_COMPUTE_BIT, out);
}
VkResult mtvk_vk_open_for_queues(uint32_t vendor, bool validation,
                               VkQueueFlags required, mtvk_vk_context **out) {
    if (!out) return VK_ERROR_INITIALIZATION_FAILED;
    *out = NULL;
    const VkQueueFlags supported = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    if (vendor > UINT16_MAX || !required || (required & ~supported))
        return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = validation ? require_validation() : VK_SUCCESS;
    if (result != VK_SUCCESS) return result;
    mtvk_vk_context *context = calloc(1, sizeof(*context));
    if (!context) return VK_ERROR_OUT_OF_HOST_MEMORY;
    atomic_init(&context->errors, 0u);
    atomic_init(&context->warnings, 0u);
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "MetalVK Vulkan integration", .apiVersion = MTVK_VK_API_VERSION };
    const char *layer = "VK_LAYER_KHRONOS_validation";
    const char *extensions[] = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME };
    VkDebugUtilsMessengerCreateInfoEXT debug = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = validation_message, .pUserData = context };
    const VkValidationFeatureEnableEXT synchronization = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT validation_features = { .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .pNext = &debug, .enabledValidationFeatureCount = 1, .pEnabledValidationFeatures = &synchronization };
    VkInstanceCreateInfo create = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app, .pNext = validation ? &validation_features : NULL,
        .enabledLayerCount = validation ? 1u : 0u, .ppEnabledLayerNames = &layer,
        .enabledExtensionCount = validation ? 2u : 0u, .ppEnabledExtensionNames = extensions };
    VkInstance instance = VK_NULL_HANDLE;
    result = vkCreateInstance(&create, NULL, &instance);
    if (result != VK_SUCCESS) goto failure;
    context->instance = instance;
    if (validation) {
        PFN_vkCreateDebugUtilsMessengerEXT make = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(context->instance, "vkCreateDebugUtilsMessengerEXT");
        context->destroy_messenger = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(context->instance, "vkDestroyDebugUtilsMessengerEXT");
        if (!make || !context->destroy_messenger) { result = VK_ERROR_EXTENSION_NOT_PRESENT; goto failure; }
        VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
        result = make(context->instance, &debug, NULL, &messenger);
        if (result != VK_SUCCESS) goto failure;
        context->messenger = messenger;
    }
    uint32_t count = MTVK_VK_PHYSICAL_LIMIT;
    VkPhysicalDevice devices[MTVK_VK_PHYSICAL_LIMIT];
    result = vkEnumeratePhysicalDevices(context->instance, &count, devices);
    if (result != VK_SUCCESS) goto failure;
    for (uint32_t i = 0; i < count && !context->physical; ++i) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(devices[i], &properties);
        if ((vendor && properties.vendorID != vendor) || properties.apiVersion < MTVK_VK_API_VERSION ||
            (properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
             properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)) continue;
        uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &families, NULL);
        if (!families || families > MTVK_VK_QUEUE_LIMIT) continue;
        VkQueueFamilyProperties queues[MTVK_VK_QUEUE_LIMIT];
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &families, queues);
        for (uint32_t j = 0; j < families; ++j) {
            if (queues[j].queueCount && (queues[j].queueFlags & required) == required) {
                context->physical = devices[i]; context->family = j;
                context->properties = properties; break;
            }
        }
    }
    if (!context->physical) { result = VK_ERROR_INITIALIZATION_FAILED; goto failure; }
    context->driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
    VkPhysicalDeviceProperties2 properties2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &context->driver };
    vkGetPhysicalDeviceProperties2(context->physical, &properties2);
    // Vulkan-to-Metal would make a Metal-to-Vulkan front end circular.
    if (context->driver.driverID == VK_DRIVER_ID_MOLTENVK) {
        result = VK_ERROR_INCOMPATIBLE_DRIVER; goto failure;
    }
    vkGetPhysicalDeviceMemoryProperties(context->physical, &context->memory);
    if (!context->memory.memoryTypeCount || context->memory.memoryTypeCount > VK_MAX_MEMORY_TYPES ||
        !context->memory.memoryHeapCount || context->memory.memoryHeapCount > VK_MAX_MEMORY_HEAPS) {
        result = VK_ERROR_INITIALIZATION_FAILED; goto failure;
    }
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = context->family, .queueCount = 1, .pQueuePriorities = &priority };
    VkDeviceCreateInfo device = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue };
    VkDevice logical = VK_NULL_HANDLE;
    result = vkCreateDevice(context->physical, &device, NULL, &logical);
    if (result != VK_SUCCESS) goto failure;
    context->device = logical;
    vkGetDeviceQueue(context->device, context->family, 0, &context->queue);
    if (!context->queue || mtvk_vk_validation_errors(context)) {
        result = VK_ERROR_INITIALIZATION_FAILED; goto failure;
    }
    *out = context;
    return VK_SUCCESS;
failure:
    (void)dispose(context); // no work was submitted; preserve the original error
    return result;
}
VkResult mtvk_vk_close(mtvk_vk_context *context) {
    if (!context) return VK_SUCCESS;
    VkResult result = VK_SUCCESS;
    if (context->device) {
        result = vkDeviceWaitIdle(context->device);
        if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST) return result;
    }
    const VkResult disposed = dispose(context);
    return result == VK_SUCCESS ? disposed : result;
}
VkDevice mtvk_vk_device(const mtvk_vk_context *c) { return c ? c->device : VK_NULL_HANDLE; }
VkPhysicalDevice mtvk_vk_physical(const mtvk_vk_context *c) { return c ? c->physical : VK_NULL_HANDLE; }
VkQueue mtvk_vk_queue(const mtvk_vk_context *c) { return c ? c->queue : VK_NULL_HANDLE; }
uint32_t mtvk_vk_queue_family(const mtvk_vk_context *c) { return c ? c->family : UINT32_MAX; }
const VkPhysicalDeviceProperties *mtvk_vk_properties(const mtvk_vk_context *c) { return c ? &c->properties : NULL; }
const VkPhysicalDeviceDriverProperties *mtvk_vk_driver(const mtvk_vk_context *c) { return c ? &c->driver : NULL; }
const VkPhysicalDeviceMemoryProperties *mtvk_vk_memory(const mtvk_vk_context *c) { return c ? &c->memory : NULL; }
uint32_t mtvk_vk_validation_errors(const mtvk_vk_context *c) {
    return c ? atomic_load_explicit(&c->errors, memory_order_relaxed) : 0;
}
uint32_t mtvk_vk_validation_warnings(const mtvk_vk_context *c) {
    return c ? atomic_load_explicit(&c->warnings, memory_order_relaxed) : 0;
}
