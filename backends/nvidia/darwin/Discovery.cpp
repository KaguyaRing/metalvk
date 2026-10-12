// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "nvidia/darwin.h"
#include "base/span.h"
#include <IOKit/IOKitLib.h>
#include <cstring>
mtvk_nv_darwin_result mtvk_nv_darwin_query(uint64_t registryId, void *response, size_t capacity) {
    mtvk_nv_darwin_result result {MTVK_INVALID_ARGUMENT, MTVK_NV_DARWIN_ARGUMENTS, 0u, 0u};
    if (!registryId || !response || !mtvk_span_valid(response, capacity)) return result;
    if (capacity < MTVK_NV_QUERY_RESPONSE_BYTES) { result.status = MTVK_BUFFER_TOO_SMALL; return result; }
    result.stage = MTVK_NV_DARWIN_LOOKUP;
    auto *matching = IORegistryEntryIDMatching(registryId);
    if (!matching) { result.status = MTVK_OUT_OF_MEMORY; return result; }
    io_service_t service = IOServiceGetMatchingService(kIOMainPortDefault, matching);
    if (!service) { result.status = MTVK_NOT_FOUND; return result; }
    uint64_t actualId = 0;
    kern_return_t error = IORegistryEntryGetRegistryEntryID(service, &actualId);
    const bool expected = error == KERN_SUCCESS && actualId == registryId &&
        IOObjectConformsTo(service, MTVK_DIAG_SERVICE_CLASS);
    if (!expected) {
        result.status = MTVK_INVALID_CONTEXT;
        result.native_error = static_cast<uint32_t>(error);
    } else {
        result.stage = MTVK_NV_DARWIN_OPEN;
        io_connect_t connection = IO_OBJECT_NULL;
        error = IOServiceOpen(service, mach_task_self(), MTVK_NV_CONNECT_TYPE, &connection);
        result.status = MTVK_INVALID_CONTEXT;
        result.native_error = static_cast<uint32_t>(error);
        if (error == KERN_SUCCESS) {
            uint8_t request[MTVK_NV_QUERY_REQUEST_BYTES], bytes[MTVK_NV_QUERY_RESPONSE_BYTES] {};
            size_t requestSize = 0, responseSize = sizeof(bytes);
            result.status = mtvk_nv_request_init(request, sizeof(request), MTVK_NV_OP_DEVICE_QUERY,
                0u, MTVK_NV_DEVICE_WIRE_BYTES, &requestSize);
            result.stage = MTVK_NV_DARWIN_CALL;
            if (result.status == MTVK_OK) {
                error = IOConnectCallStructMethod(connection, MTVK_NV_QUERY_SELECTOR,
                    request, requestSize, bytes, &responseSize);
                result.native_error = static_cast<uint32_t>(error);
                if (error != KERN_SUCCESS) result.status = MTVK_INVALID_CONTEXT;
            }
            const kern_return_t closed = IOServiceClose(connection);
            if (closed != KERN_SUCCESS) {
                result = {MTVK_CLEANUP_REQUIRED, MTVK_NV_DARWIN_CLOSE, static_cast<uint32_t>(closed), 0u};
            } else if (result.status == MTVK_OK) {
                result.stage = MTVK_NV_DARWIN_RESPONSE;
                mtvk_nv_device_info info;
                result.status = responseSize > sizeof(bytes) ? MTVK_INVALID_ARGUMENT :
                    mtvk_nv_discovery_response_decode(bytes, responseSize, &info);
                // Release before publishing: failures must never publish partial success.
                const kern_return_t released = IOObjectRelease(service);
                service = IO_OBJECT_NULL;
                if (released != KERN_SUCCESS)
                    result = {MTVK_CLEANUP_REQUIRED, MTVK_NV_DARWIN_RELEASE, static_cast<uint32_t>(released), 0u};
                else if (result.status == MTVK_OK) {
                    std::memcpy(response, bytes, responseSize);
                    result.stage = MTVK_NV_DARWIN_COMPLETE;
                    result.response_bytes = responseSize;
                }
            }
        }
    }
    if (service) {
        const kern_return_t released = IOObjectRelease(service);
        if (released != KERN_SUCCESS)
            result = {MTVK_CLEANUP_REQUIRED, MTVK_NV_DARWIN_RELEASE, static_cast<uint32_t>(released), 0u};
    }
    return result;
}
