// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
// CPU owner-lock harness; not execution of XNU thread_call.
#include "runtime/init.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <exception>
#include <mutex>
#include <thread>
namespace {
constexpr unsigned Producers = 4, Tasks = 3000;
struct Harness {
    mtvk_work_queue queue {};
    std::mutex lock;
    std::atomic<bool> failed {false};
    unsigned accepted = 0, executed = 0;
};
void callback(void *context) {
    auto &h = *static_cast<Harness *>(context);
    // Re-enter owner's lock, as deferred frees/reentrant producers may do.
    // Invoking while holding it deadlocks and is caught by the CI job timeout.
    std::lock_guard<std::mutex> guard(h.lock);
    ++h.executed;
}
}
int main() {
    Harness h;
    if (mtvk_work_init(&h.queue) != MTVK_OK) return 1;
    std::array<std::thread, Producers> producers;
    std::thread consumer;
    try {
        consumer = std::thread([&] {
            for (;;) {
                mtvk_work_item item {};
                mtvk_status status;
                bool closed;
                {
                    std::lock_guard<std::mutex> guard(h.lock);
                    status = mtvk_work_take(&h.queue, &item);
                    closed = h.queue.phase == MTVK_WORK_CLOSED;
                }
                if (status == MTVK_NOT_FOUND) {
                    if (closed) break;
                    std::this_thread::yield();
                    continue;
                }
                if (status != MTVK_OK) { h.failed.store(true); break; }
                item.function(item.context);
                {
                    std::lock_guard<std::mutex> guard(h.lock);
                    if (mtvk_work_finish(&h.queue) != MTVK_OK) { h.failed.store(true); break; }
                }
            }
        });
        for (auto &producer : producers) producer = std::thread([&] {
            for (unsigned i = 0; i < Tasks && !h.failed.load();) {
                mtvk_status status;
                {
                    std::lock_guard<std::mutex> guard(h.lock);
                    status = mtvk_work_push(&h.queue, {callback, &h});
                    if (status == MTVK_OK) { ++h.accepted; ++i; }
                }
                if (status != MTVK_OK && status != MTVK_LIMIT_EXCEEDED) { h.failed.store(true); break; }
                std::this_thread::yield();
            }
        });
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Work test thread startup failed: %s\n", error.what());
        h.failed.store(true);
    }
    for (auto &producer : producers) if (producer.joinable()) producer.join();
    {
        std::lock_guard<std::mutex> guard(h.lock);
        const mtvk_status status = mtvk_work_close(&h.queue);
        if (status != MTVK_OK && status != MTVK_BUSY) h.failed.store(true);
        if (mtvk_work_push(&h.queue, {callback, &h}) != MTVK_INVALID_CONTEXT) h.failed.store(true);
    }
    if (consumer.joinable()) consumer.join();
    if (h.failed.load() || h.accepted != Producers * Tasks || h.executed != h.accepted ||
        h.queue.phase != MTVK_WORK_CLOSED || h.queue.count || h.queue.active) return 1;
    std::puts("PASS four-producer work drain, exact-once callbacks and lock reentry");
    return 0;
}
