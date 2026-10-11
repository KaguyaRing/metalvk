// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "runtime/time.h"
mtvk_status mtvk_time_deadline(uint64_t now, uint64_t delay, uint64_t *out) {
    if (!out) return MTVK_INVALID_ARGUMENT;
    if (delay > UINT64_MAX - now) return MTVK_LIMIT_EXCEEDED;
    *out = now + delay;
    return MTVK_OK;
}
