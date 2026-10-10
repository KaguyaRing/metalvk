// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing

#ifndef MTVK_BASE_STATUS_H
#define MTVK_BASE_STATUS_H

#include "platform.h"

MTVK_EXTERN_C_BEGIN

/* Internal results for the generic GPU library. Not a vendor error code. */
typedef enum mtvk_status {
    MTVK_OK = 0,
    MTVK_INVALID_ARGUMENT,
    MTVK_BUFFER_TOO_SMALL,
    MTVK_LIMIT_EXCEEDED,
    MTVK_NOT_SUPPORTED,
    MTVK_INVALID_CONTEXT,
    MTVK_NOT_FOUND,
    MTVK_BUSY,
    MTVK_OUT_OF_MEMORY,
    MTVK_INCOMPATIBLE_VERSION,
    MTVK_CLEANUP_REQUIRED
} mtvk_status;

const char *mtvk_status_name(mtvk_status status);

MTVK_EXTERN_C_END
#endif
