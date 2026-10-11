// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
/* Single shared implementation of the NVIDIA transport standard. Compiled into
 * the Darwin kernel library, into the user-space nvkmd adapter and into host
 * tests from this one file, so both sides cannot drift apart.
 *
 * Dependency-free on purpose: no libc, allocator, logging or Vulkan. It must
 * link inside a kext and inside Mesa. */
#include "nvidia/transport.h"

_Static_assert(sizeof(mtvk_nv_request) == 24u, "transport header must stay 6 dwords");
_Static_assert(sizeof(mtvk_nv_response) == 24u, "transport response must stay 6 dwords");

static uint32_t load_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void store_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value & 0xffu);
    bytes[1] = (uint8_t)((value >> 8) & 0xffu);
    bytes[2] = (uint8_t)((value >> 16) & 0xffu);
    bytes[3] = (uint8_t)((value >> 24) & 0xffu);
}

/* Keyed by enum, so an added or renamed op without a name is a build error. */
static const char *const op_names[MTVK_NV_OP_COUNT] = {
    [MTVK_NV_OP_DEVICE_QUERY] = "DEVICE_QUERY",
    [MTVK_NV_OP_DEVICE_VRAM_USED] = "DEVICE_VRAM_USED",
    [MTVK_NV_OP_DRM_FD] = "DRM_FD",
    [MTVK_NV_OP_SESSION_CREATE] = "SESSION_CREATE",
    [MTVK_NV_OP_SESSION_DESTROY] = "SESSION_DESTROY",
    [MTVK_NV_OP_TIMESTAMP] = "TIMESTAMP",
    [MTVK_NV_OP_MEMORY_ALLOC] = "MEMORY_ALLOC",
    [MTVK_NV_OP_MEMORY_ALLOC_TILED] = "MEMORY_ALLOC_TILED",
    [MTVK_NV_OP_MEMORY_IMPORT] = "MEMORY_IMPORT",
    [MTVK_NV_OP_MEMORY_FREE] = "MEMORY_FREE",
    [MTVK_NV_OP_MEMORY_MAP] = "MEMORY_MAP",
    [MTVK_NV_OP_MEMORY_UNMAP] = "MEMORY_UNMAP",
    [MTVK_NV_OP_MEMORY_OVERMAP] = "MEMORY_OVERMAP",
    [MTVK_NV_OP_MEMORY_SYNC_TO_GPU] = "MEMORY_SYNC_TO_GPU",
    [MTVK_NV_OP_MEMORY_SYNC_FROM_GPU] = "MEMORY_SYNC_FROM_GPU",
    [MTVK_NV_OP_MEMORY_EXPORT] = "MEMORY_EXPORT",
    [MTVK_NV_OP_VA_ALLOC] = "VA_ALLOC",
    [MTVK_NV_OP_VA_FREE] = "VA_FREE",
    [MTVK_NV_OP_VA_BIND] = "VA_BIND",
    [MTVK_NV_OP_VA_UNBIND] = "VA_UNBIND",
    [MTVK_NV_OP_CONTEXT_CREATE] = "CONTEXT_CREATE",
    [MTVK_NV_OP_CONTEXT_DESTROY] = "CONTEXT_DESTROY",
    [MTVK_NV_OP_CONTEXT_BIND] = "CONTEXT_BIND",
    [MTVK_NV_OP_CONTEXT_EXEC] = "CONTEXT_EXEC",
    [MTVK_NV_OP_CONTEXT_FLUSH] = "CONTEXT_FLUSH",
    [MTVK_NV_OP_CONTEXT_WAIT] = "CONTEXT_WAIT",
    [MTVK_NV_OP_CONTEXT_SIGNAL] = "CONTEXT_SIGNAL",
    [MTVK_NV_OP_CONTEXT_SYNC] = "CONTEXT_SYNC",
};

static const mtvk_nv_op_class op_classes[MTVK_NV_OP_COUNT] = {
    [MTVK_NV_OP_DEVICE_QUERY] = MTVK_NV_CLASS_DISCOVERY,
    [MTVK_NV_OP_DEVICE_VRAM_USED] = MTVK_NV_CLASS_DISCOVERY,
    [MTVK_NV_OP_DRM_FD] = MTVK_NV_CLASS_REPLACED,
    [MTVK_NV_OP_SESSION_CREATE] = MTVK_NV_CLASS_DISCOVERY,
    [MTVK_NV_OP_SESSION_DESTROY] = MTVK_NV_CLASS_DISCOVERY,
    [MTVK_NV_OP_TIMESTAMP] = MTVK_NV_CLASS_DISCOVERY,
    [MTVK_NV_OP_MEMORY_ALLOC] = MTVK_NV_CLASS_MEMORY,
    [MTVK_NV_OP_MEMORY_ALLOC_TILED] = MTVK_NV_CLASS_MEMORY,
    [MTVK_NV_OP_MEMORY_IMPORT] = MTVK_NV_CLASS_REPLACED,
    [MTVK_NV_OP_MEMORY_FREE] = MTVK_NV_CLASS_MEMORY,
    [MTVK_NV_OP_MEMORY_MAP] = MTVK_NV_CLASS_MEMORY,
    [MTVK_NV_OP_MEMORY_UNMAP] = MTVK_NV_CLASS_MEMORY,
    [MTVK_NV_OP_MEMORY_OVERMAP] = MTVK_NV_CLASS_MEMORY,
    [MTVK_NV_OP_MEMORY_SYNC_TO_GPU] = MTVK_NV_CLASS_MEMORY,
    [MTVK_NV_OP_MEMORY_SYNC_FROM_GPU] = MTVK_NV_CLASS_MEMORY,
    [MTVK_NV_OP_MEMORY_EXPORT] = MTVK_NV_CLASS_REPLACED,
    [MTVK_NV_OP_VA_ALLOC] = MTVK_NV_CLASS_ADDRESS,
    [MTVK_NV_OP_VA_FREE] = MTVK_NV_CLASS_ADDRESS,
    [MTVK_NV_OP_VA_BIND] = MTVK_NV_CLASS_ADDRESS,
    [MTVK_NV_OP_VA_UNBIND] = MTVK_NV_CLASS_ADDRESS,
    [MTVK_NV_OP_CONTEXT_CREATE] = MTVK_NV_CLASS_EXECUTION,
    [MTVK_NV_OP_CONTEXT_DESTROY] = MTVK_NV_CLASS_EXECUTION,
    [MTVK_NV_OP_CONTEXT_BIND] = MTVK_NV_CLASS_EXECUTION,
    [MTVK_NV_OP_CONTEXT_EXEC] = MTVK_NV_CLASS_EXECUTION,
    [MTVK_NV_OP_CONTEXT_FLUSH] = MTVK_NV_CLASS_EXECUTION,
    [MTVK_NV_OP_CONTEXT_WAIT] = MTVK_NV_CLASS_SYNC,
    [MTVK_NV_OP_CONTEXT_SIGNAL] = MTVK_NV_CLASS_SYNC,
    [MTVK_NV_OP_CONTEXT_SYNC] = MTVK_NV_CLASS_SYNC,
};

mtvk_status mtvk_nv_request_init(void *out, size_t capacity, uint32_t op,
                                 uint32_t request_bytes, uint32_t response_bytes,
                                 size_t *written) {
    if (!out || !written) return MTVK_INVALID_ARGUMENT;
    if (op >= (uint32_t)MTVK_NV_OP_COUNT) return MTVK_INVALID_ARGUMENT;
    if (request_bytes > MTVK_NV_TRANSPORT_MAX_REQUEST_BYTES ||
        response_bytes > MTVK_NV_TRANSPORT_MAX_RESPONSE_BYTES) return MTVK_LIMIT_EXCEEDED;
    if (request_bytes % 4u != 0u) return MTVK_INVALID_ARGUMENT;
    const size_t total = sizeof(mtvk_nv_request) + (size_t)request_bytes;
    if (capacity < total) return MTVK_BUFFER_TOO_SMALL;
    uint8_t *bytes = out;
    store_le32(bytes, (uint32_t)sizeof(mtvk_nv_request));
    store_le32(bytes + 4, MTVK_NV_TRANSPORT_VERSION);
    store_le32(bytes + 8, op);
    store_le32(bytes + 12, 0u);
    store_le32(bytes + 16, request_bytes);
    store_le32(bytes + 20, response_bytes);
    *written = total;
    return MTVK_OK;
}

mtvk_status mtvk_nv_transport_validate(const void *bytes, size_t size,
                                       mtvk_nv_request_view *view) {
    if (!bytes || !view) return MTVK_INVALID_ARGUMENT;
    const uint8_t *raw = bytes;
    if (size < sizeof(mtvk_nv_request)) return MTVK_BUFFER_TOO_SMALL;
    mtvk_nv_request header;
    header.header_bytes = load_le32(raw);
    header.version = load_le32(raw + 4);
    header.op = load_le32(raw + 8);
    header.reserved = load_le32(raw + 12);
    header.request_bytes = load_le32(raw + 16);
    header.response_bytes = load_le32(raw + 20);
    if (header.header_bytes != (uint32_t)sizeof(mtvk_nv_request)) return MTVK_INVALID_ARGUMENT;
    if (header.version != MTVK_NV_TRANSPORT_VERSION) return MTVK_INCOMPATIBLE_VERSION;
    if (header.op >= (uint32_t)MTVK_NV_OP_COUNT) return MTVK_INVALID_ARGUMENT;
    if (header.reserved != 0u) return MTVK_INVALID_ARGUMENT;
    if (header.request_bytes > MTVK_NV_TRANSPORT_MAX_REQUEST_BYTES ||
        header.response_bytes > MTVK_NV_TRANSPORT_MAX_RESPONSE_BYTES) return MTVK_LIMIT_EXCEEDED;
    if (header.request_bytes % 4u != 0u) return MTVK_INVALID_ARGUMENT;
    const size_t expected = sizeof(mtvk_nv_request) + (size_t)header.request_bytes;
    if (size != expected) return MTVK_INVALID_ARGUMENT;
    view->header = header;
    view->payload = raw + sizeof(mtvk_nv_request);
    view->payload_bytes = header.request_bytes;
    view->raw = raw;
    view->raw_bytes = size;
    return MTVK_OK;
}

const char *mtvk_nv_op_name(uint32_t op) {
    if (op >= (uint32_t)MTVK_NV_OP_COUNT) return NULL;
    return op_names[op];
}

mtvk_nv_op_class mtvk_nv_op_class_of(uint32_t op) {
    if (op >= (uint32_t)MTVK_NV_OP_COUNT) return MTVK_NV_CLASS_NONE;
    return op_classes[op];
}

mtvk_status mtvk_nv_response_init(void *out, size_t capacity, uint32_t op, uint32_t status,
                                  uint32_t payload_bytes, uint32_t backed_op_mask,
                                  size_t *written) {
    if (!out || !written) return MTVK_INVALID_ARGUMENT;
    if (op >= (uint32_t)MTVK_NV_OP_COUNT) return MTVK_INVALID_ARGUMENT;
    if (status > (uint32_t)MTVK_CLEANUP_REQUIRED) return MTVK_INVALID_ARGUMENT;
    if (payload_bytes > MTVK_NV_TRANSPORT_MAX_RESPONSE_BYTES) return MTVK_LIMIT_EXCEEDED;
    if (payload_bytes % 4u != 0u) return MTVK_INVALID_ARGUMENT;
    const size_t total = sizeof(mtvk_nv_response) + (size_t)payload_bytes;
    if (capacity < total) return MTVK_BUFFER_TOO_SMALL;
    uint8_t *bytes = out;
    store_le32(bytes, (uint32_t)sizeof(mtvk_nv_response));
    store_le32(bytes + 4, MTVK_NV_TRANSPORT_VERSION);
    store_le32(bytes + 8, op);
    store_le32(bytes + 12, status);
    store_le32(bytes + 16, payload_bytes);
    store_le32(bytes + 20, backed_op_mask);
    *written = total;
    return MTVK_OK;
}

mtvk_status mtvk_nv_transport_validate_response(const void *bytes, size_t size,
                                                mtvk_nv_response_view *view) {
    if (!bytes || !view) return MTVK_INVALID_ARGUMENT;
    const uint8_t *raw = bytes;
    if (size < sizeof(mtvk_nv_response)) return MTVK_BUFFER_TOO_SMALL;
    mtvk_nv_response header;
    header.header_bytes = load_le32(raw);
    header.version = load_le32(raw + 4);
    header.op = load_le32(raw + 8);
    header.status = load_le32(raw + 12);
    header.payload_bytes = load_le32(raw + 16);
    header.backed_op_mask = load_le32(raw + 20);
    if (header.header_bytes != (uint32_t)sizeof(mtvk_nv_response)) return MTVK_INVALID_ARGUMENT;
    if (header.version != MTVK_NV_TRANSPORT_VERSION) return MTVK_INCOMPATIBLE_VERSION;
    if (header.op >= (uint32_t)MTVK_NV_OP_COUNT) return MTVK_INVALID_ARGUMENT;
    if (header.status > (uint32_t)MTVK_CLEANUP_REQUIRED) return MTVK_INVALID_ARGUMENT;
    if (header.payload_bytes > MTVK_NV_TRANSPORT_MAX_RESPONSE_BYTES) return MTVK_LIMIT_EXCEEDED;
    if (header.payload_bytes % 4u != 0u) return MTVK_INVALID_ARGUMENT;
    const size_t expected = sizeof(mtvk_nv_response) + (size_t)header.payload_bytes;
    if (size != expected) return MTVK_INVALID_ARGUMENT;
    view->header = header;
    view->payload = raw + sizeof(mtvk_nv_response);
    view->payload_bytes = header.payload_bytes;
    view->raw = raw;
    view->raw_bytes = size;
    return MTVK_OK;
}
