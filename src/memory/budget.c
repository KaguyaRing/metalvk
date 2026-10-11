// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "memory/init.h"

mtvk_status mtvk_memory_budget_init(mtvk_memory_budget *out, const mtvk_memory_limits *limits) {
    if (!out) return MTVK_INVALID_ARGUMENT;
    const mtvk_memory_limits chosen = limits ? *limits : (mtvk_memory_limits){
        MTVK_MEMORY_MAX_ALLOCATION, MTVK_MEMORY_MAX_BUDGET, MTVK_MEMORY_MAX_BUFFERS };
    if (!chosen.allocation_bytes || !chosen.total_bytes || !chosen.buffers ||
        chosen.allocation_bytes > chosen.total_bytes ||
        chosen.allocation_bytes > MTVK_MEMORY_MAX_ALLOCATION ||
        chosen.total_bytes > MTVK_MEMORY_MAX_BUDGET || chosen.buffers > MTVK_MEMORY_MAX_BUFFERS)
        return MTVK_INVALID_ARGUMENT;
    const mtvk_memory_budget initialized = {chosen, 0, 0, MTVK_MEMORY_OPEN, limits == 0};
    *out = initialized;
    return MTVK_OK;
}
mtvk_status mtvk_memory_charge(uint64_t bytes, uint64_t granule, uint64_t *charge) {
    if (!charge || !bytes || !granule || (granule & (granule - 1))) return MTVK_INVALID_ARGUMENT;
    if (bytes > UINT64_MAX - (granule - 1)) return MTVK_LIMIT_EXCEEDED;
    *charge = (bytes + granule - 1) & ~(granule - 1);
    return MTVK_OK;
}
static bool valid(const mtvk_memory_budget *budget) {
    return budget && budget->limits.allocation_bytes && budget->limits.total_bytes &&
        budget->limits.buffers && budget->limits.allocation_bytes <= budget->limits.total_bytes &&
        budget->limits.allocation_bytes <= MTVK_MEMORY_MAX_ALLOCATION &&
        budget->limits.total_bytes <= MTVK_MEMORY_MAX_BUDGET &&
        budget->limits.buffers <= MTVK_MEMORY_MAX_BUFFERS &&
        budget->reserved_bytes <= budget->limits.total_bytes &&
        budget->reserved_buffers <= budget->limits.buffers &&
        ((budget->reserved_bytes == 0) == (budget->reserved_buffers == 0)) &&
        budget->phase >= MTVK_MEMORY_OPEN && budget->phase <= MTVK_MEMORY_CLOSED &&
        (budget->phase != MTVK_MEMORY_CLOSED || budget->reserved_buffers == 0);
}
mtvk_status mtvk_memory_budget_reserve(mtvk_memory_budget *budget, uint64_t charge) {
    if (!charge || !valid(budget)) return MTVK_INVALID_ARGUMENT;
    if (budget->phase != MTVK_MEMORY_OPEN) return MTVK_INVALID_CONTEXT;
    if (charge > budget->limits.allocation_bytes ||
        charge > budget->limits.total_bytes - budget->reserved_bytes ||
        budget->reserved_buffers == budget->limits.buffers) return MTVK_LIMIT_EXCEEDED;
    budget->reserved_bytes += charge;
    ++budget->reserved_buffers;
    return MTVK_OK;
}
mtvk_status mtvk_memory_budget_release(mtvk_memory_budget *budget, uint64_t charge) {
    if (!charge || !valid(budget) || !budget->reserved_buffers || charge > budget->reserved_bytes)
        return MTVK_INVALID_ARGUMENT;
    // The last release must leave zero bytes, and earlier releases must not.
    if ((budget->reserved_buffers == 1) != (charge == budget->reserved_bytes))
        return MTVK_INVALID_ARGUMENT;
    budget->reserved_bytes -= charge;
    --budget->reserved_buffers;
    return MTVK_OK;
}
mtvk_status mtvk_memory_budget_close(mtvk_memory_budget *budget) {
    if (!valid(budget)) return MTVK_INVALID_ARGUMENT;
    budget->phase = budget->reserved_buffers ? MTVK_MEMORY_DRAINING : MTVK_MEMORY_CLOSED;
    return budget->reserved_buffers ? MTVK_BUSY : MTVK_OK;
}
