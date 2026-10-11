// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_KERNEL_PLATFORM_WORK_HPP
#define MTVK_KERNEL_PLATFORM_WORK_HPP
#include <IOKit/IOLib.h>
#include <libkern/c++/OSObject.h>
#include <kern/thread_call.h>
#include <mach/mach_types.h>
#include "runtime/init.h"
constexpr uint64_t MTVK_WORK_CLOSE_NS = UINT64_C(5000000000);
constexpr uint64_t MTVK_WORK_MAX_CLOSE_NS = UINT64_C(60000000000);
struct MetalVKWorkStats {
    mtvk_work_phase phase;
    uint32_t queued, invocations;
    bool active, fault;
};
/* Fixed, preallocated CPU queue. enqueue/copyStats may run in a normal IRQ;
 * create/close and final release must be sleepable and outside workloop gates.
 * Callers hold a queue reference through each call. Callback code/context must
 * remain alive until execution. A successful enqueue transfers task ownership;
 * rejection leaves it with the caller. No callbacks run while our locks are held.
 * close drains, never cancels accepted jobs. Timeout preserves call/anchor refs. */
class MetalVKWorkQueue final : public OSObject {
    OSDeclareDefaultStructors(MetalVKWorkQueue)
    IOSimpleLock *lock_ = nullptr;
    IOLock *control_ = nullptr;
    thread_call_t call_ = nullptr;
    thread_t worker_ = nullptr;
    mtvk_work_queue queue_ {};
    uint32_t invocations_ = 0;
    bool fault_ = false, anchored_ = false;
    bool initialize();
    void kickLocked();
    void run();
    static void invoke(thread_call_param_t context, thread_call_param_t unused);
    void free() override;
public:
    static IOReturn create(MetalVKWorkQueue **out);
    IOReturn enqueue(mtvk_work_function function, void *context);
    IOReturn copyStats(MetalVKWorkStats *out);
    IOReturn close(uint64_t timeout = MTVK_WORK_CLOSE_NS);
};
#endif
