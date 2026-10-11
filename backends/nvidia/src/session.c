// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
/* Capability-gated NVIDIA transport dispatch. The kernel endpoint and the host
 * loopback tests build this one source, like transport.c. Sharing the refusal
 * policy reduces duplicate logic; it does not prove runtime endpoint alignment.
 *
 * Dependency-free on purpose: no libc, allocator, logging or Vulkan. */
#include "nvidia/session.h"
#include "base/span.h"

void mtvk_nv_session_init(mtvk_nv_session *session, uint32_t requested_ops,
                          const mtvk_nv_device_info *device) {
    if (!session) return;
    mtvk_nv_device_info initial;
    mtvk_nv_device_info_init(&initial);
    if (device) initial = *device; /* Snapshot before mutation, including self-init. */
    session->version = MTVK_NV_TRANSPORT_VERSION;
    session->backed_op_mask = (requested_ops & MTVK_NV_IMPLEMENTED_OPS) |
                              MTVK_NV_OP_BIT(MTVK_NV_OP_DEVICE_QUERY);
    session->device = initial;
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
    if (!mtvk_span_valid(session, sizeof(*session)) || !mtvk_span_valid(request, request_size) ||
        !mtvk_span_valid(response, response_capacity) || !mtvk_span_valid(response_written, sizeof(*response_written)) ||
        mtvk_spans_overlap(response, response_capacity, request, request_size) ||
        mtvk_spans_overlap(response, response_capacity, session, sizeof(*session)) ||
        mtvk_spans_overlap(response_written, sizeof(*response_written), response, response_capacity) ||
        mtvk_spans_overlap(response_written, sizeof(*response_written), request, request_size) ||
        mtvk_spans_overlap(response_written, sizeof(*response_written), session, sizeof(*session)))
        return MTVK_INVALID_ARGUMENT;
    *response_written = 0;
    /* Never emit a capability advertisement from corrupted or incompatible
     * session state. No handler may be enabled by mutating the public mask. */
    if (session->version != MTVK_NV_TRANSPORT_VERSION) return MTVK_INCOMPATIBLE_VERSION;
    if ((session->backed_op_mask & ~MTVK_NV_IMPLEMENTED_OPS) != 0u ||
        !(session->backed_op_mask & MTVK_NV_OP_BIT(MTVK_NV_OP_DEVICE_QUERY)))
        return MTVK_INVALID_ARGUMENT;
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
        const uint32_t payload = MTVK_NV_DEVICE_WIRE_BYTES;
        uint8_t encoded[MTVK_NV_DEVICE_WIRE_BYTES];
        const mtvk_status encoding = mtvk_nv_device_info_encode(&effective, encoded, sizeof(encoded));
        if (encoding != MTVK_OK)
            return reply(response, response_capacity, op, encoding, 0u,
                         session->backed_op_mask, response_written);
        /* response_bytes is a payload budget; zero retains the unspecified
         * budget used by older callers. Actual output capacity always applies. */
        if (view.header.response_bytes && view.header.response_bytes < payload)
            return reply(response, response_capacity, op, MTVK_BUFFER_TOO_SMALL, 0u,
                         session->backed_op_mask, response_written);
        size_t written = 0;
        const mtvk_status result = reply(response, response_capacity, op, MTVK_OK, payload,
                                         session->backed_op_mask, &written);
        if (result != MTVK_OK) return result;
        uint8_t *dst = (uint8_t *)response + sizeof(mtvk_nv_response);
        for (uint32_t index = 0; index < payload; ++index) dst[index] = encoded[index];
        *response_written = written;
        return MTVK_OK;
    }

    /* Backed but not yet handled: refuse rather than fabricate a result. */
    return reply(response, response_capacity, op, MTVK_NOT_SUPPORTED, 0u,
                 session->backed_op_mask, response_written);
}
