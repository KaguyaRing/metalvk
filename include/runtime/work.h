// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_RUNTIME_WORK_H
#define MTVK_RUNTIME_WORK_H
#include <stdbool.h>
#include <stdint.h>
#include "base/status.h"
#define MTVK_WORK_CAPACITY 256u
typedef void (*mtvk_work_function)(void *context);
typedef struct mtvk_work_item { mtvk_work_function function; void *context; } mtvk_work_item;
typedef enum mtvk_work_phase { MTVK_WORK_OPEN = 0, MTVK_WORK_DRAINING, MTVK_WORK_CLOSED } mtvk_work_phase;
typedef struct mtvk_work_queue {
    mtvk_work_item items[MTVK_WORK_CAPACITY];
    uint32_t head, count;
    bool active;
    mtvk_work_phase phase;
} mtvk_work_queue;
MTVK_EXTERN_C_BEGIN
/* CPU work accounting only. Owner serializes all access with one lock; a single
 * consumer copies an item out, unlocks, invokes it, then locks to finish it.
 * init requires unpublished/quiescent storage. Context lifetime is the caller's
 * responsibility until invocation; failed push never transfers ownership. */
mtvk_status mtvk_work_init(mtvk_work_queue *queue);
mtvk_status mtvk_work_push(mtvk_work_queue *queue, mtvk_work_item item);
mtvk_status mtvk_work_take(mtvk_work_queue *queue, mtvk_work_item *out);
mtvk_status mtvk_work_finish(mtvk_work_queue *queue);
mtvk_status mtvk_work_close(mtvk_work_queue *queue);
MTVK_EXTERN_C_END
#endif
