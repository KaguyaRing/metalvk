// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
/* Capability-gated NVIDIA transport dispatch. The kernel endpoint and the host
 * loopback tests build this one source, like transport.c, so the refusal paths
 * and the capability mask cannot drift between the two sides.
 *
 * Dependency-free on purpose: no libc, allocator, logging or Vulkan. */
#include "nvidia/session.h"

_Static_assert(sizeof(mtvk_nv_device_info) % 4u == 0u,
               "device info payload must stay 4-byte aligned");

mtvk_nv_device_info mtvk_nv_effective_device_info(const mtvk_nv_device_info *device,
                                                  uint32_t backed_op_mask) {
    mtvk_nv_device_info out = *device;
    if (!(backed_op_mask & MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_ALLOC_TILED))) out.has_alloc_tiled = 0u;
    if (!(backed_op_mask & MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_MAP))) out.has_map_fixed = 0u;
    if (!(backed_op_mask & MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_OVERMAP))) out.has_overmap = 0u;
    if (!(backed_op_mask & MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_ALLOC_TILED))) out.has_compression = 0u;
    if (!(backed_op_mask & (MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_IMPORT) |
                            MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_EXPORT)))) out.has_dma_buf = 0u;
    return out;
}

void mtvk_nv_session_init(mtvk_nv_session *session, uint32_t requested_ops,
                          const mtvk_nv_device_info *device) {
    if (!session) return;
    session->version = MTVK_NV_TRANSPORT_VERSION;
    session->backed_op_mask = (requested_ops & MTVK_NV_IMPLEMENTED_OPS) |
                              MTVK_NV_OP_BIT(MTVK_NV_OP_DEVICE_QUERY);
    mtvk_nv_device_info_init(&session->device);
    if (device) session->device = *device;
}

static mtvk_status reply(void *response, size_t capacity, uint32_t op, mtvk_status status,
                         uint32_t payload_bytes, uint32_t backed_op_mask, size_t *written) {
    const mtvk_status result = mtvk_nv_response_init(response, capacity, op, (uint32_t)status,
                                                     payload_bytes, backed_op_mask, written);
    if (result != MTVK_OK) {
        *written = 0;
        return result;
    }
    /* The response was written; the caller sees the semantic status, not the
     * write result. */
    return status;
}

mtvk_status mtvk_nv_session_dispatch(mtvk_nv_session *session,
                                     const void *request, size_t request_size,
                                     void *response, size_t response_capacity,
                                     size_t *response_written) {
    if (!session || !response || !response_written) return MTVK_INVALID_ARGUMENT;
    *response_written = 0;
    mtvk_nv_request_view view;
    const mtvk_status framed = mtvk_nv_transport_validate(request, request_size, &view);
    if (framed != MTVK_OK)
        return reply(response, response_capacity, 0u, framed, 0u, session->backed_op_mask,
                     response_written);

    const uint32_t op = view.header.op;
    if (!(session->backed_op_mask & MTVK_NV_OP_BIT(op)))
        return reply(response, response_capacity, op, MTVK_NOT_SUPPORTED, 0u,
                     session->backed_op_mask, response_written);

    if (op == (uint32_t)MTVK_NV_OP_DEVICE_QUERY) {
        if (view.payload_bytes != 0u)
            return reply(response, response_capacity, op, MTVK_INVALID_ARGUMENT, 0u,
                         session->backed_op_mask, response_written);
        const mtvk_nv_device_info effective =
            mtvk_nv_effective_device_info(&session->device, session->backed_op_mask);
        const uint32_t payload = (uint32_t)sizeof(effective);
        size_t written = 0;
        const mtvk_status result = reply(response, response_capacity, op, MTVK_OK, payload,
                                         session->backed_op_mask, &written);
        if (result != MTVK_OK) return result;
        uint8_t *dst = (uint8_t *)response + sizeof(mtvk_nv_response);
        const uint8_t *src = (const uint8_t *)&effective;
        for (uint32_t index = 0; index < payload; ++index) dst[index] = src[index];
        *response_written = written;
        return MTVK_OK;
    }

    /* Backed but not yet handled: refuse rather than fabricate a result. */
    return reply(response, response_capacity, op, MTVK_NOT_SUPPORTED, 0u,
                 session->backed_op_mask, response_written);
}
