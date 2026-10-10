// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing

#ifndef MTVK_BASE_SPAN_H
#define MTVK_BASE_SPAN_H

#include "platform.h"

/* Flat-address targets only. Checks representability/overlap, not whether a
 * pointer references readable memory. */
static inline bool mtvk_span_valid(const void *pointer, size_t length)
{
    return length == 0 || (pointer != NULL && length <= UINTPTR_MAX - (uintptr_t)pointer);
}

/* Callers validate both spans before relying on the overlap result. */
static inline bool mtvk_spans_overlap(const void *left, size_t left_size,
                                     const void *right, size_t right_size)
{
    uintptr_t a = (uintptr_t)left;
    uintptr_t b = (uintptr_t)right;
    if (left_size == 0 || right_size == 0) { return false; }
    return a <= b ? b - a < left_size : a - b < right_size;
}

#endif
