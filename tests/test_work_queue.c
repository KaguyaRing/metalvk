// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "runtime/init.h"
#include <stdio.h>
#define CHECK(test) do { if (!(test)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); return 1; } } while (0)
static void increment(void *context) { ++*(unsigned *)context; }
int main(void) {
    mtvk_work_queue queue;
    unsigned invoked = 0;
    CHECK(mtvk_work_init(&queue) == MTVK_OK);
    const mtvk_work_item item = {increment, &invoked};
    mtvk_work_item taken = {0};
    CHECK(mtvk_work_take(&queue, &taken) == MTVK_NOT_FOUND);
    for (unsigned i = 0; i < MTVK_WORK_CAPACITY; ++i) CHECK(mtvk_work_push(&queue, item) == MTVK_OK);
    CHECK(mtvk_work_push(&queue, item) == MTVK_LIMIT_EXCEEDED);
    CHECK(mtvk_work_take(&queue, &taken) == MTVK_OK);
    CHECK(mtvk_work_take(&queue, &taken) == MTVK_BUSY);
    CHECK(mtvk_work_push(&queue, item) == MTVK_LIMIT_EXCEEDED); // Active item consumes capacity.
    CHECK(mtvk_work_close(&queue) == MTVK_BUSY);
    CHECK(mtvk_work_push(&queue, item) == MTVK_INVALID_CONTEXT);
    taken.function(taken.context);
    CHECK(mtvk_work_finish(&queue) == MTVK_OK);
    for (unsigned i = 1; i < MTVK_WORK_CAPACITY; ++i) {
        CHECK(mtvk_work_take(&queue, &taken) == MTVK_OK);
        taken.function(taken.context);
        CHECK(mtvk_work_finish(&queue) == MTVK_OK);
    }
    CHECK(invoked == MTVK_WORK_CAPACITY && queue.phase == MTVK_WORK_CLOSED);
    CHECK(mtvk_work_close(&queue) == MTVK_OK);
    CHECK(mtvk_work_finish(&queue) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_work_push(&queue, item) == MTVK_INVALID_CONTEXT);
    CHECK(mtvk_work_init(&queue) == MTVK_OK);
    CHECK(mtvk_work_push(&queue, (mtvk_work_item){0}) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_work_take(&queue, NULL) == MTVK_INVALID_ARGUMENT);
    unsigned counters[MTVK_WORK_CAPACITY] = {0};
    for (unsigned round = 0; round < 20; ++round) {
        for (unsigned i = 0; i < MTVK_WORK_CAPACITY; ++i)
            CHECK(mtvk_work_push(&queue, (mtvk_work_item){increment, &counters[i]}) == MTVK_OK);
        for (unsigned i = 0; i < MTVK_WORK_CAPACITY; ++i) {
            CHECK(mtvk_work_take(&queue, &taken) == MTVK_OK);
            CHECK(taken.context == &counters[i]);
            taken.function(taken.context);
            CHECK(mtvk_work_finish(&queue) == MTVK_OK);
        }
    }
    for (unsigned i = 0; i < MTVK_WORK_CAPACITY; ++i) CHECK(counters[i] == 20);
    // Reentrant production while the consumer owns an active task.
    CHECK(mtvk_work_push(&queue, item) == MTVK_OK);
    CHECK(mtvk_work_take(&queue, &taken) == MTVK_OK);
    CHECK(mtvk_work_push(&queue, item) == MTVK_OK);
    CHECK(mtvk_work_finish(&queue) == MTVK_OK);
    CHECK(mtvk_work_take(&queue, &taken) == MTVK_OK);
    CHECK(mtvk_work_close(&queue) == MTVK_BUSY);
    CHECK(mtvk_work_finish(&queue) == MTVK_OK && queue.phase == MTVK_WORK_CLOSED);
    CHECK(mtvk_work_take(&queue, &taken) == MTVK_NOT_FOUND);
    queue.head = MTVK_WORK_CAPACITY;
    CHECK(mtvk_work_push(&queue, item) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_work_take(&queue, &taken) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_work_close(&queue) == MTVK_INVALID_ARGUMENT);
    uint64_t deadline = 99;
    CHECK(mtvk_time_deadline(100, 20, &deadline) == MTVK_OK && deadline == 120);
    CHECK(mtvk_time_deadline(UINT64_MAX, 1, &deadline) == MTVK_LIMIT_EXCEEDED && deadline == 120);
    CHECK(mtvk_time_deadline(UINT64_MAX, 0, &deadline) == MTVK_OK && deadline == UINT64_MAX);
    CHECK(mtvk_time_deadline(1, 1, NULL) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_work_init(NULL) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_work_push(NULL, item) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_work_take(NULL, &taken) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_work_close(NULL) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_work_finish(NULL) == MTVK_INVALID_ARGUMENT);
    puts("PASS bounded work queue drain and monotonic deadline arithmetic");
    return 0;
}
