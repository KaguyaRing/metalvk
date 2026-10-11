// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "runtime/work.h"
static bool valid(const mtvk_work_queue *queue) {
    return queue && queue->head < MTVK_WORK_CAPACITY && queue->count <= MTVK_WORK_CAPACITY &&
        (!queue->active || queue->count < MTVK_WORK_CAPACITY) &&
        queue->phase >= MTVK_WORK_OPEN && queue->phase <= MTVK_WORK_CLOSED &&
        (queue->phase != MTVK_WORK_CLOSED || (!queue->count && !queue->active));
}
mtvk_status mtvk_work_init(mtvk_work_queue *queue) {
    if (!queue) return MTVK_INVALID_ARGUMENT;
    const mtvk_work_queue initialized = {0};
    *queue = initialized;
    return MTVK_OK;
}
mtvk_status mtvk_work_push(mtvk_work_queue *queue, mtvk_work_item item) {
    if (!valid(queue) || !item.function) return MTVK_INVALID_ARGUMENT;
    if (queue->phase != MTVK_WORK_OPEN) return MTVK_INVALID_CONTEXT;
    if (queue->count + (queue->active ? 1u : 0u) == MTVK_WORK_CAPACITY) return MTVK_LIMIT_EXCEEDED;
    const uint32_t tail = (queue->head + queue->count) % MTVK_WORK_CAPACITY;
    queue->items[tail] = item;
    ++queue->count;
    return MTVK_OK;
}
mtvk_status mtvk_work_take(mtvk_work_queue *queue, mtvk_work_item *out) {
    if (!valid(queue) || !out) return MTVK_INVALID_ARGUMENT;
    if (queue->active) return MTVK_BUSY;
    if (!queue->count) return MTVK_NOT_FOUND;
    if (!queue->items[queue->head].function) return MTVK_INVALID_ARGUMENT;
    *out = queue->items[queue->head];
    queue->items[queue->head] = (mtvk_work_item){0};
    queue->head = (queue->head + 1u) % MTVK_WORK_CAPACITY;
    --queue->count;
    queue->active = true;
    return MTVK_OK;
}
mtvk_status mtvk_work_finish(mtvk_work_queue *queue) {
    if (!valid(queue) || !queue->active) return MTVK_INVALID_ARGUMENT;
    queue->active = false;
    if (queue->phase == MTVK_WORK_DRAINING && !queue->count) queue->phase = MTVK_WORK_CLOSED;
    return MTVK_OK;
}
mtvk_status mtvk_work_close(mtvk_work_queue *queue) {
    if (!valid(queue)) return MTVK_INVALID_ARGUMENT;
    const bool busy = queue->count || queue->active;
    queue->phase = busy ? MTVK_WORK_DRAINING : MTVK_WORK_CLOSED;
    return busy ? MTVK_BUSY : MTVK_OK;
}
