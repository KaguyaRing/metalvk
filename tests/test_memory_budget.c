// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "memory/init.h"
#include <stdio.h>
#include <string.h>
#define CHECK(test) do { if (!(test)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); return 1; } } while (0)
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
    CHECK(memcmp(&full, &budget, sizeof(budget)) == 0);
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
    CHECK(memcmp(&closed, &budget, sizeof(budget)) == 0);
    CHECK(mtvk_memory_budget_init(NULL, &limits) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_memory_budget_close(NULL) == MTVK_INVALID_ARGUMENT);
    puts("PASS memory budget reservation, rollback and draining lifecycle");
    return 0;
}
