// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
// Host-side owner-lock contract test, not execution of the XNU allocator.
#include "memory/init.h"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <exception>
#include <mutex>
#include <thread>
int main() {
    constexpr size_t Workers = 4;
    constexpr unsigned Iterations = 5000;
    constexpr uint64_t Charge = 4096;
    const mtvk_memory_limits limits {Charge, Charge * Workers, static_cast<uint32_t>(Workers)};
    mtvk_memory_budget budget {};
    if (mtvk_memory_budget_init(&budget, &limits) != MTVK_OK) return 1;
    std::mutex mutex;
    std::condition_variable progress;
    size_t arrived = 0;
    bool draining = false;
    std::atomic<bool> failed {false};
    std::array<std::thread, Workers> threads;
    try {
        for (auto &thread : threads) thread = std::thread([&] {
            for (unsigned i = 0; i < Iterations; ++i) {
                bool reserved;
                {
                    std::lock_guard<std::mutex> guard(mutex);
                    reserved = mtvk_memory_budget_reserve(&budget, Charge) == MTVK_OK;
                }
                if (!reserved) { failed.store(true); break; }
                std::this_thread::yield(); // Backing allocation would be outside the owner lock.
                {
                    std::lock_guard<std::mutex> guard(mutex);
                    if (mtvk_memory_budget_release(&budget, Charge) != MTVK_OK) failed.store(true);
                }
            }
            {
                std::unique_lock<std::mutex> guard(mutex);
                const bool reserved = mtvk_memory_budget_reserve(&budget, Charge) == MTVK_OK;
                if (!reserved) failed.store(true);
                ++arrived;
                progress.notify_all();
                progress.wait(guard, [&] { return draining; });
                // Rollback of an allocation that lost the publication race.
                if (reserved && mtvk_memory_budget_release(&budget, Charge) != MTVK_OK) failed.store(true);
            }
            for (unsigned i = 0; i < Iterations; ++i) {
                std::lock_guard<std::mutex> guard(mutex);
                if (mtvk_memory_budget_reserve(&budget, Charge) != MTVK_INVALID_CONTEXT) failed.store(true);
            }
        });
        {
            std::unique_lock<std::mutex> guard(mutex);
            if (!progress.wait_for(guard, std::chrono::seconds(30), [&] { return arrived == Workers; }))
                failed.store(true);
            if (mtvk_memory_budget_close(&budget) != MTVK_BUSY ||
                budget.reserved_buffers != Workers || budget.reserved_bytes != Charge * Workers)
                failed.store(true);
            draining = true;
        }
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Memory test thread startup failed: %s\n", error.what());
        failed.store(true);
        std::lock_guard<std::mutex> guard(mutex);
        draining = true;
        (void)mtvk_memory_budget_close(&budget);
    }
    progress.notify_all();
    for (auto &thread : threads) if (thread.joinable()) thread.join();
    if (mtvk_memory_budget_close(&budget) != MTVK_OK || budget.reserved_buffers || budget.reserved_bytes)
        failed.store(true);
    if (failed.load()) { std::fputs("FAIL memory reservation/close race\n", stderr); return 1; }
    std::puts("PASS four-thread memory reservation, draining and pending-allocation rollback");
    return 0;
}
