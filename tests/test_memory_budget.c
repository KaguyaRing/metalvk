// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "memory/init.h"
#include <stdio.h>
#define CHECK(test) do { if (!(test)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); return 1; } } while (0)
static bool same(const mtvk_memory_budget *a, const mtvk_memory_budget *b) {
    return a->limits.allocation_bytes == b->limits.allocation_bytes &&
        a->limits.total_bytes == b->limits.total_bytes && a->limits.buffers == b->limits.buffers &&
        a->reserved_bytes == b->reserved_bytes && a->reserved_buffers == b->reserved_buffers &&
        a->phase == b->phase && a->default_limits == b->default_limits;
}
static int test_reservations_match_a_ticket_model(void) {
    enum { Slots = 16, Iterations = 20000 };
    const mtvk_memory_limits limits = {4096, 32768, Slots};
    mtvk_memory_budget budget;
    CHECK(mtvk_memory_budget_init(&budget, &limits) == MTVK_OK);
    uint64_t tickets[Slots] = {0}, bytes = 0;
    uint32_t count = 0, random = UINT32_C(0x13579bdf);
    for (unsigned step = 0; step < Iterations; ++step) {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        const unsigned operation = random % 5u;
        if (operation == 0) {
            const uint64_t charge = (1u + (random >> 8) % 8u) * UINT64_C(512);
            const mtvk_memory_budget before = budget;
            const mtvk_status expected = budget.phase != MTVK_MEMORY_OPEN ? MTVK_INVALID_CONTEXT :
                count == Slots || charge > limits.total_bytes - bytes ? MTVK_LIMIT_EXCEEDED : MTVK_OK;
            CHECK(mtvk_memory_budget_reserve(&budget, charge) == expected);
            if (expected == MTVK_OK) {
                for (size_t i = 0; i < Slots; ++i) if (!tickets[i]) { tickets[i] = charge; break; }
                bytes += charge; ++count;
            } else CHECK(same(&before, &budget));
        } else if ((operation == 1 || operation == 2) && count) {
            for (size_t j = 0; j < Slots; ++j) {
                const size_t i = (random + j) % Slots;
                if (!tickets[i]) continue;
                CHECK(mtvk_memory_budget_release(&budget, tickets[i]) == MTVK_OK);
                bytes -= tickets[i]; tickets[i] = 0; --count; break;
            }
        } else if (operation == 3) {
            CHECK(mtvk_memory_budget_close(&budget) == (count ? MTVK_BUSY : MTVK_OK));
        } else if (operation == 4 && !count && budget.phase == MTVK_MEMORY_CLOSED) {
            CHECK(mtvk_memory_budget_init(&budget, &limits) == MTVK_OK);
        }
        CHECK(budget.reserved_bytes == bytes && budget.reserved_buffers == count);
    }
    for (size_t i = 0; i < Slots; ++i)
        if (tickets[i]) CHECK(mtvk_memory_budget_release(&budget, tickets[i]) == MTVK_OK);
    CHECK(mtvk_memory_budget_close(&budget) == MTVK_OK);
    return 0;
}
int main(void) {
    mtvk_memory_budget budget;
    CHECK(mtvk_memory_budget_init(&budget, NULL) == MTVK_OK);
    CHECK(budget.default_limits && budget.limits.total_bytes == MTVK_MEMORY_MAX_BUDGET);
    const mtvk_memory_limits limits = {8192, 12288, 2};
    CHECK(mtvk_memory_budget_init(&budget, &limits) == MTVK_OK && !budget.default_limits);
    uint64_t charge = 0;
    CHECK(mtvk_memory_charge(1, 4096, &charge) == MTVK_OK && charge == 4096);
    CHECK(mtvk_memory_charge(4096, 4096, &charge) == MTVK_OK && charge == 4096);
    CHECK(mtvk_memory_charge(4097, 4096, &charge) == MTVK_OK && charge == 8192);
    CHECK(mtvk_memory_charge(UINT64_MAX, 4096, &charge) == MTVK_LIMIT_EXCEEDED);
    CHECK(charge == 8192);
    CHECK(mtvk_memory_charge(0, 4096, &charge) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_memory_charge(1, 3, &charge) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_memory_charge(1, 4096, NULL) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_memory_budget_reserve(&budget, 8192) == MTVK_OK);
    CHECK(mtvk_memory_budget_reserve(&budget, 4096) == MTVK_OK);
    const mtvk_memory_budget full = budget;
    CHECK(mtvk_memory_budget_reserve(&budget, 1) == MTVK_LIMIT_EXCEEDED);
    CHECK(same(&full, &budget));
    CHECK(mtvk_memory_budget_close(&budget) == MTVK_BUSY && budget.phase == MTVK_MEMORY_DRAINING);
    CHECK(mtvk_memory_budget_reserve(&budget, 1) == MTVK_INVALID_CONTEXT);
    CHECK(mtvk_memory_budget_release(&budget, 8192) == MTVK_OK);
    CHECK(mtvk_memory_budget_release(&budget, 1) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_memory_budget_release(&budget, 4096) == MTVK_OK);
    CHECK(mtvk_memory_budget_release(&budget, 4096) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_memory_budget_close(&budget) == MTVK_OK && budget.phase == MTVK_MEMORY_CLOSED);
    CHECK(mtvk_memory_budget_close(&budget) == MTVK_OK);
    CHECK(mtvk_memory_budget_reserve(&budget, 4096) == MTVK_INVALID_CONTEXT);
    mtvk_memory_limits invalid = {0, 12288, 2};
    const mtvk_memory_budget closed = budget;
    CHECK(mtvk_memory_budget_init(&budget, &invalid) == MTVK_INVALID_ARGUMENT);
    CHECK(same(&closed, &budget));
    CHECK(mtvk_memory_budget_init(NULL, &limits) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_memory_budget_close(NULL) == MTVK_INVALID_ARGUMENT);
    const mtvk_memory_limits invalid_limits[] = {
        {1, 1, 0}, {2, 1, 1}, {MTVK_MEMORY_MAX_ALLOCATION + 1, MTVK_MEMORY_MAX_BUDGET, 1},
        {1, MTVK_MEMORY_MAX_BUDGET + 1, 1}, {1, 1, MTVK_MEMORY_MAX_BUFFERS + 1}, {1, 0, 1}
    };
    for (size_t i = 0; i < sizeof(invalid_limits) / sizeof(invalid_limits[0]); ++i) {
        CHECK(mtvk_memory_budget_init(&budget, &invalid_limits[i]) == MTVK_INVALID_ARGUMENT);
        CHECK(same(&closed, &budget));
    }
    CHECK(mtvk_memory_budget_reserve(NULL, 1) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_memory_budget_release(NULL, 1) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_memory_budget_init(&budget, &limits) == MTVK_OK);
    CHECK(mtvk_memory_budget_reserve(&budget, 0) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_memory_budget_reserve(&budget, 8193) == MTVK_LIMIT_EXCEEDED);
    CHECK(mtvk_memory_budget_reserve(&budget, UINT64_MAX) == MTVK_LIMIT_EXCEEDED);
    CHECK(mtvk_memory_budget_reserve(&budget, 4096) == MTVK_OK);
    CHECK(mtvk_memory_budget_reserve(&budget, 4096) == MTVK_OK);
    CHECK(mtvk_memory_budget_release(&budget, 8192) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_memory_budget_release(&budget, 8193) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_memory_budget_release(&budget, 4096) == MTVK_OK);
    CHECK(mtvk_memory_budget_release(&budget, 4096) == MTVK_OK);
    CHECK(mtvk_memory_budget_close(&budget) == MTVK_OK);
    budget.reserved_buffers = 1;
    const mtvk_memory_budget corrupt = budget;
    CHECK(mtvk_memory_budget_reserve(&budget, 1) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_memory_budget_release(&budget, 1) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_memory_budget_close(&budget) == MTVK_INVALID_ARGUMENT);
    CHECK(same(&corrupt, &budget));
    CHECK(test_reservations_match_a_ticket_model() == 0);
    puts("PASS memory budget reservation, rollback and draining lifecycle");
    return 0;
}
