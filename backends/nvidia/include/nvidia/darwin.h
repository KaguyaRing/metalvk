// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_NVIDIA_DARWIN_H
#define MTVK_NVIDIA_DARWIN_H
#include "nvidia/endpoint.h"
MTVK_EXTERN_C_BEGIN
typedef enum mtvk_nv_darwin_stage {
    MTVK_NV_DARWIN_ARGUMENTS = 0,
    MTVK_NV_DARWIN_LOOKUP,
    MTVK_NV_DARWIN_OPEN,
    MTVK_NV_DARWIN_CALL,
    MTVK_NV_DARWIN_CLOSE,
    MTVK_NV_DARWIN_RELEASE,
    MTVK_NV_DARWIN_RESPONSE,
    MTVK_NV_DARWIN_COMPLETE
} mtvk_nv_darwin_stage;
typedef struct mtvk_nv_darwin_result {
    mtvk_status status;
    mtvk_nv_darwin_stage stage;
    uint32_t native_error;
    size_t response_bytes;
} mtvk_nv_darwin_result;
/* Native macOS implementation only (no other-OS stub). Explicit service ID;
 * one short-lived, read-only connection. Output unchanged on every failure,
 * including close/release failures. Caller owns writable response memory;
 * success transfers wire bytes, never service/connection ownership. */
mtvk_nv_darwin_result mtvk_nv_darwin_query(uint64_t registry_id, void *response, size_t capacity);
MTVK_EXTERN_C_END
#endif
