// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_NVIDIA_SESSION_H
#define MTVK_NVIDIA_SESSION_H
#include "nvidia/discovery.h"
#include "nvidia/transport.h"

/* Capability-gated dispatch for one NVIDIA kernel endpoint session.
 *
 * The endpoint advertises which ops it can actually back and refuses every
 * other op with a defined status instead of fabricating a result. This is the
 * executable form of the alignment matrix: when a class gains a real
 * implementation its bit joins MTVK_NV_IMPLEMENTED_OPS, and the very same
 * response then carries that capability to the user-space adapter. */
MTVK_EXTERN_C_BEGIN

/* Shared dispatcher handlers, not proof of a live kernel endpoint. DEVICE_QUERY
 * currently serializes supplied discovery data; probe/UserClient wiring remains
 * unverified. Every memory, address, execution and sync op is refused. */
#define MTVK_NV_IMPLEMENTED_OPS (MTVK_NV_OP_BIT(MTVK_NV_OP_DEVICE_QUERY))

typedef struct mtvk_nv_session {
    uint32_t version;
    uint32_t backed_op_mask;
    mtvk_nv_device_info device;
} mtvk_nv_session;

/* advertised = requested_ops & MTVK_NV_IMPLEMENTED_OPS, and DEVICE_QUERY is
 * always present, so a caller can never advertise an op that has no handler.
 * device must be zero-initialized with mtvk_nv_device_info_init before any
 * field is set, so no uninitialized padding crosses the boundary. */
void mtvk_nv_session_init(mtvk_nv_session *session, uint32_t requested_ops,
                          const mtvk_nv_device_info *device);

/* Dispatches one request, writing a little-endian response. response_written is
 * set on every path that writes a header. A missing or malformed request and an
 * unbacked op both produce a defined status; no partial success is possible.
 * Incompatible/corrupted session state writes no response. The owner must
 * serialize dispatch and mutation; this structure provides no synchronization. */
mtvk_status mtvk_nv_session_dispatch(mtvk_nv_session *session,
                                     const void *request, size_t request_size,
                                     void *response, size_t response_capacity,
                                     size_t *response_written);

MTVK_EXTERN_C_END
#endif
