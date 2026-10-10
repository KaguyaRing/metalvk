// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing

#include "base/init.h"

const char *mtvk_status_name(mtvk_status status)
{
    switch (status) {
    case MTVK_OK: return "ok";
    case MTVK_INVALID_ARGUMENT: return "invalid argument";
    case MTVK_BUFFER_TOO_SMALL: return "buffer too small";
    case MTVK_LIMIT_EXCEEDED: return "limit exceeded";
    case MTVK_NOT_SUPPORTED: return "not supported";
    case MTVK_INVALID_CONTEXT: return "invalid context";
    case MTVK_NOT_FOUND: return "not found";
    case MTVK_BUSY: return "busy";
    case MTVK_OUT_OF_MEMORY: return "out of memory";
    case MTVK_INCOMPATIBLE_VERSION: return "incompatible version";
    case MTVK_CLEANUP_REQUIRED: return "cleanup required";
    default: return "unknown status";
    }
}
