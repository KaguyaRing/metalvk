// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
// Host concurrency and backend-contract test; no physical display operations.
#include "display-fixture.h"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <exception>
#include <mutex>
#include <thread>
namespace {
struct Harness {
    mtvk_display_manager manager {};
    std::mutex state, gate;
    std::condition_variable progress;
    bool entered = false, proceed = false;
};
void lock(void *context) { static_cast<Harness *>(context)->state.lock(); }
void unlock(void *context) { static_cast<Harness *>(context)->state.unlock(); }
mtvk_status apply(void *context, uint64_t, const mtvk_display_mode *, const mtvk_display_config *) {
    auto &h = *static_cast<Harness *>(context);
    mtvk_display_snapshot copy {};
    if (mtvk_display_copy(&h.manager, &copy) != MTVK_OK || !copy.busy) return MTVK_CLEANUP_REQUIRED;
    std::unique_lock<std::mutex> guard(h.gate);
    h.entered = true;
    h.progress.notify_all();
    h.progress.wait(guard, [&] { return h.proceed; });
    return MTVK_OK;
}
mtvk_status disable(void *, uint64_t) { return MTVK_OK; }
mtvk_status reset(void *, uint64_t, mtvk_display_reset_reason) { return MTVK_OK; }
}
int main() {
    Harness h;
    const mtvk_display_backend backend {&h, lock, unlock, apply, disable, reset};
    const auto caps = display_caps();
    const auto config = display_hdr();
    if (mtvk_display_init(&h.manager, 19, &backend) != MTVK_OK ||
        mtvk_display_connect(&h.manager, &caps) != MTVK_OK) return 1;
    mtvk_status result = MTVK_OK;
    bool failed = false;
    std::thread worker;
    try {
        worker = std::thread([&] { result = mtvk_display_apply(&h.manager, &config); });
        {
            std::unique_lock<std::mutex> guard(h.gate);
            if (!h.progress.wait_for(guard, std::chrono::seconds(10), [&] { return h.entered; })) failed = true;
        }
        mtvk_display_snapshot copy {};
        if (mtvk_display_copy(&h.manager, &copy) != MTVK_OK || !copy.busy ||
            mtvk_display_apply(&h.manager, &config) != MTVK_BUSY ||
            mtvk_display_close(&h.manager) != MTVK_BUSY ||
            mtvk_display_recover(&h.manager, MTVK_DISPLAY_RESET_TIMEOUT) != MTVK_BUSY ||
            mtvk_display_notify(&h.manager, MTVK_DISPLAY_SIGNAL_SUSPEND) != MTVK_OK) failed = true;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Display concurrency test failed: %s\n", error.what()); failed = true;
    }
    {
        std::lock_guard<std::mutex> guard(h.gate);
        h.proceed = true;
    }
    h.progress.notify_all();
    if (worker.joinable()) worker.join();
    mtvk_display_snapshot copy {};
    if (result != MTVK_CLEANUP_REQUIRED || mtvk_display_copy(&h.manager, &copy) != MTVK_OK ||
        copy.phase != MTVK_DISPLAY_SUSPENDED || copy.has_applied || !copy.needs_quiesce ||
        mtvk_display_close(&h.manager) != MTVK_CLEANUP_REQUIRED ||
        mtvk_display_disable(&h.manager) != MTVK_OK || mtvk_display_close(&h.manager) != MTVK_OK) failed = true;
    if (failed) return 1;
    std::puts("PASS display concurrent signal invalidates in-flight success; callback lock reentry (host only)");
    return 0;
}
