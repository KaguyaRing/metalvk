// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_NVIDIA_TRANSPORT_H
#define MTVK_NVIDIA_TRANSPORT_H
#include "base/status.h"
#include <stddef.h>
#include <stdint.h>

/* NVIDIA device transport framing standard shared by the metalvk user-space
 * NVK nvkmd adapter and the Darwin kernel device endpoint. This is a private
 * NVIDIA backend standard, NOT a cross-vendor application ABI and NOT a GPU
 * submission ABI. Both sides compile this header and src/transport.c from the
 * one source kept in this repository, which is what keeps them aligned.
 *
 * Wire format is little-endian regardless of host order. A message is a fixed
 * 24-byte header followed by exactly request_bytes of payload bytes. The same
 * envelope is reused on the response path; nothing transfers kernel object
 * ownership implicitly. This standard defines framing and validation only. It
 * does not claim that RM/GSP execution, DMA, GPU VA or submission exist. */
#define MTVK_NV_TRANSPORT_VERSION 2u
#define MTVK_NV_TRANSPORT_MAX_REQUEST_BYTES 4096u
#define MTVK_NV_TRANSPORT_MAX_RESPONSE_BYTES 4096u

MTVK_EXTERN_C_BEGIN

/* One transport op per distinct nvkmd capability. backends/nvidia/
 * nvkmd-alignment.json holds the authoritative mapping to the pinned upstream
 * operations and is checked in CI against the pinned Mesa header. */
typedef enum mtvk_nv_op {
    MTVK_NV_OP_DEVICE_QUERY = 0u,
    MTVK_NV_OP_DEVICE_VRAM_USED,
    MTVK_NV_OP_DRM_FD,
    MTVK_NV_OP_SESSION_CREATE,
    MTVK_NV_OP_SESSION_DESTROY,
    MTVK_NV_OP_TIMESTAMP,
    MTVK_NV_OP_MEMORY_ALLOC,
    MTVK_NV_OP_MEMORY_ALLOC_TILED,
    MTVK_NV_OP_MEMORY_IMPORT,
    MTVK_NV_OP_MEMORY_FREE,
    MTVK_NV_OP_MEMORY_MAP,
    MTVK_NV_OP_MEMORY_UNMAP,
    MTVK_NV_OP_MEMORY_OVERMAP,
    MTVK_NV_OP_MEMORY_SYNC_TO_GPU,
    MTVK_NV_OP_MEMORY_SYNC_FROM_GPU,
    MTVK_NV_OP_MEMORY_EXPORT,
    MTVK_NV_OP_VA_ALLOC,
    MTVK_NV_OP_VA_FREE,
    MTVK_NV_OP_VA_BIND,
    MTVK_NV_OP_VA_UNBIND,
    MTVK_NV_OP_CONTEXT_CREATE,
    MTVK_NV_OP_CONTEXT_DESTROY,
    MTVK_NV_OP_CONTEXT_BIND,
    MTVK_NV_OP_CONTEXT_EXEC,
    MTVK_NV_OP_CONTEXT_FLUSH,
    MTVK_NV_OP_CONTEXT_WAIT,
    MTVK_NV_OP_CONTEXT_SIGNAL,
    MTVK_NV_OP_CONTEXT_SYNC,
    MTVK_NV_OP_COUNT
} mtvk_nv_op;

/* Version 2 accepts only bits belonging to its operation catalog. Version 1's
 * host-struct DEVICE_QUERY payload is rejected rather than guessed/converted. */
#define MTVK_NV_VALID_OP_MASK ((UINT32_C(1) << (uint32_t)MTVK_NV_OP_COUNT) - UINT32_C(1))
#define MTVK_NV_OP_BIT(op) (UINT32_C(1) << (uint32_t)(op))

/* Which kernel capability an op depends on. This is the alignment axis: an op
 * may only be advertised once its whole class is backed by real hardware. */
typedef enum mtvk_nv_op_class {
    MTVK_NV_CLASS_DISCOVERY = 0,
    MTVK_NV_CLASS_MEMORY,
    MTVK_NV_CLASS_ADDRESS,
    MTVK_NV_CLASS_EXECUTION,
    MTVK_NV_CLASS_SYNC,
    MTVK_NV_CLASS_REPLACED,
    MTVK_NV_CLASS_NONE
} mtvk_nv_op_class;

typedef struct mtvk_nv_request {
    uint32_t header_bytes;
    uint32_t version;
    uint32_t op;
    uint32_t reserved;
    uint32_t request_bytes;
    uint32_t response_bytes; /* Payload budget; zero means unspecified, not zero capacity. */
} mtvk_nv_request;

/* Validated, host-order view over an untrusted message buffer. */
typedef struct mtvk_nv_request_view {
    mtvk_nv_request header;
    const uint8_t *payload;
    uint32_t payload_bytes;
    const uint8_t *raw;
    size_t raw_bytes;
} mtvk_nv_request_view;

/* Response envelope. Same fixed size as the request header so both directions
 * share one little-endian message shape. backed_op_mask is the executable form
 * of the alignment matrix: bit i set means op i is genuinely backed by the
 * endpoint that wrote this response. */
typedef struct mtvk_nv_response {
    uint32_t header_bytes;
    uint32_t version;
    uint32_t op;
    uint32_t status;
    uint32_t payload_bytes;
    uint32_t backed_op_mask;
} mtvk_nv_response;

typedef struct mtvk_nv_response_view {
    mtvk_nv_response header;
    const uint8_t *payload;
    uint32_t payload_bytes;
    const uint8_t *raw;
    size_t raw_bytes;
} mtvk_nv_response_view;

/* Fills a little-endian response header. written receives the full envelope size
 * (header plus payload reservation); the caller writes payload afterwards. */
mtvk_status mtvk_nv_response_init(void *out, size_t capacity, uint32_t op, uint32_t status,
                                  uint32_t payload_bytes, uint32_t backed_op_mask,
                                  size_t *written);

/* Validates a response envelope. Rejects a wrong version, unknown op, unknown
 * status, size overflow, missing or surplus bytes and non-4-byte alignment. */
mtvk_status mtvk_nv_transport_validate_response(const void *bytes, size_t size,
                                                mtvk_nv_response_view *view);

/* Fills a little-endian request header. written receives the full envelope size
 * (header plus payload reservation); the caller writes payload afterwards.
 * capacity must cover the payload reservation. */
mtvk_status mtvk_nv_request_init(void *out, size_t capacity, uint32_t op,
                                 uint32_t request_bytes, uint32_t response_bytes,
                                 size_t *written);

/* Validates an untrusted envelope and decodes it into host order. Rejects a
 * wrong version, unknown op, non-zero reserved, size overflow, missing or
 * surplus bytes, over-limit sizes and non-4-byte payload alignment. view is
 * only written on MTVK_OK. */
mtvk_status mtvk_nv_transport_validate(const void *bytes, size_t size,
                                       mtvk_nv_request_view *view);

const char *mtvk_nv_op_name(uint32_t op);
mtvk_nv_op_class mtvk_nv_op_class_of(uint32_t op);

MTVK_EXTERN_C_END
#endif
