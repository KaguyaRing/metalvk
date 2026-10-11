// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_MEMORY_BUDGET_H
#define MTVK_MEMORY_BUDGET_H
#include <stdbool.h>
#include <stdint.h>
#include "base/status.h"
#define MTVK_MEMORY_MAX_ALLOCATION UINT64_C(134217728)
#define MTVK_MEMORY_MAX_BUDGET UINT64_C(536870912)
#define MTVK_MEMORY_MAX_BUFFERS 1024u
typedef struct mtvk_memory_limits {
    uint64_t allocation_bytes;
    uint64_t total_bytes;
    uint32_t buffers;
} mtvk_memory_limits;
typedef enum mtvk_memory_phase {
    MTVK_MEMORY_OPEN = 0, MTVK_MEMORY_DRAINING, MTVK_MEMORY_CLOSED
} mtvk_memory_phase;
typedef struct mtvk_memory_budget {
    mtvk_memory_limits limits;
    uint64_t reserved_bytes;
    uint32_t reserved_buffers;
    mtvk_memory_phase phase;
    bool default_limits;
} mtvk_memory_budget;
MTVK_EXTERN_C_BEGIN
/* Pure accounting, not an allocation registry or GPU command ABI. The owner
 * serializes all access, including validation + reservation + shutdown, with
 * one lock. Each successful reserve has exactly one matching release. */
mtvk_status mtvk_memory_budget_init(mtvk_memory_budget *out, const mtvk_memory_limits *limits);
mtvk_status mtvk_memory_budget_reserve(mtvk_memory_budget *budget, uint64_t charge);
mtvk_status mtvk_memory_budget_release(mtvk_memory_budget *budget, uint64_t charge);
mtvk_status mtvk_memory_budget_close(mtvk_memory_budget *budget);
/* Round payload to the allocator granule without overflow. */
mtvk_status mtvk_memory_charge(uint64_t bytes, uint64_t granule, uint64_t *charge);
MTVK_EXTERN_C_END
#endif
