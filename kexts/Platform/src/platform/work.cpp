// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "platform/work.hpp"
#include "platform/context.hpp"
#include <kern/thread.h>
namespace {
constexpr unsigned PollMilliseconds = 1;
}
OSDefineMetaClassAndStructors(MetalVKWorkQueue, OSObject)
IOReturn MetalVKWorkQueue::create(MetalVKWorkQueue **out) {
    if (!out) return kIOReturnBadArgument;
    *out = nullptr;
    if (!metalvk::platform::sleepable()) return kIOReturnCannotLock;
    auto *queue = new MetalVKWorkQueue;
    if (!queue) return kIOReturnNoMemory;
    if (!queue->initialize()) { queue->release(); return kIOReturnNoMemory; }
    *out = queue;
    return kIOReturnSuccess;
}
bool MetalVKWorkQueue::initialize() {
    if (!OSObject::init() || mtvk_work_init(&queue_) != MTVK_OK) return false;
    lock_ = IOSimpleLockAlloc();
    control_ = IOLockAlloc();
    if (!lock_ || !control_) return false;
    call_ = thread_call_allocate_with_options(invoke, this, THREAD_CALL_PRIORITY_KERNEL, THREAD_CALL_OPTIONS_ONCE);
    if (!call_) return false;
    retain(); // Anchor protects open queues, even if a caller forgets close.
    anchored_ = true;
    return true;
}
void MetalVKWorkQueue::kickLocked() {
    // Enter's boolean means "already pending", NOT success/failure. ONCE
    // coalesces repeated enters and serializes callbacks. Each new invocation
    // has its own reference, established before it can obtain our spinlock.
    if (!thread_call_enter(call_)) { retain(); ++invocations_; }
}
IOReturn MetalVKWorkQueue::enqueue(mtvk_work_function function, void *context) {
    if (!function) return kIOReturnBadArgument;
    if (!lock_) return kIOReturnNotReady;
    const IOInterruptState flags = IOSimpleLockLockDisableInterrupt(lock_);
    const mtvk_status result = fault_ ? MTVK_INVALID_CONTEXT : mtvk_work_push(&queue_, {function, context});
    if (result == MTVK_OK) kickLocked();
    IOSimpleLockUnlockEnableInterrupt(lock_, flags);
    if (result == MTVK_LIMIT_EXCEEDED) return kIOReturnNoResources;
    if (result == MTVK_INVALID_CONTEXT) return kIOReturnOffline;
    return result == MTVK_OK ? kIOReturnSuccess : kIOReturnBadArgument;
}
void MetalVKWorkQueue::run() {
    IOInterruptState flags = IOSimpleLockLockDisableInterrupt(lock_);
    worker_ = current_thread();
    IOSimpleLockUnlockEnableInterrupt(lock_, flags);
    for (;;) {
        mtvk_work_item item {};
        flags = IOSimpleLockLockDisableInterrupt(lock_);
        const mtvk_status result = fault_ ? MTVK_INVALID_CONTEXT : mtvk_work_take(&queue_, &item);
        if (result != MTVK_OK) {
            if (result != MTVK_NOT_FOUND) fault_ = true;
            worker_ = nullptr;
            IOSimpleLockUnlockEnableInterrupt(lock_, flags);
            return;
        }
        IOSimpleLockUnlockEnableInterrupt(lock_, flags);
        item.function(item.context); // May block/re-enter enqueue; no lock held.
        flags = IOSimpleLockLockDisableInterrupt(lock_);
        if (mtvk_work_finish(&queue_) != MTVK_OK) fault_ = true;
        IOSimpleLockUnlockEnableInterrupt(lock_, flags);
    }
}
void MetalVKWorkQueue::invoke(thread_call_param_t context, thread_call_param_t) {
    auto *queue = static_cast<MetalVKWorkQueue *>(context);
    queue->run();
    const IOInterruptState flags = IOSimpleLockLockDisableInterrupt(queue->lock_);
    if (queue->invocations_) --queue->invocations_;
    else queue->fault_ = true;
    IOSimpleLockUnlockEnableInterrupt(queue->lock_, flags);
    queue->release(); // Invocation reference; no further access to queue.
}
IOReturn MetalVKWorkQueue::copyStats(MetalVKWorkStats *out) {
    if (!out) return kIOReturnBadArgument;
    if (!lock_) return kIOReturnNotReady;
    const IOInterruptState flags = IOSimpleLockLockDisableInterrupt(lock_);
    *out = {queue_.phase, queue_.count, invocations_, queue_.active, fault_};
    IOSimpleLockUnlockEnableInterrupt(lock_, flags);
    return kIOReturnSuccess;
}
IOReturn MetalVKWorkQueue::close(uint64_t timeout) {
    if (!metalvk::platform::sleepable()) return kIOReturnCannotLock;
    if (!lock_ || !control_) return kIOReturnNotReady;
    if (timeout > MTVK_WORK_MAX_CLOSE_NS) return kIOReturnBadArgument;
    uint64_t deadline = 0;
    if (mtvk_time_deadline(metalvk::platform::uptimeNanoseconds(), timeout, &deadline) != MTVK_OK)
        return kIOReturnBadArgument;
    IOInterruptState flags = IOSimpleLockLockDisableInterrupt(lock_);
    const bool self = worker_ == current_thread();
    IOSimpleLockUnlockEnableInterrupt(lock_, flags);
    if (self) return kIOReturnBusy; // Never wait for our own callback.
    // A competing closer must not extend our bounded wait by holding this lock.
    if (!IOLockTryLock(control_)) return kIOReturnBusy;
    flags = IOSimpleLockLockDisableInterrupt(lock_);
    const mtvk_status state = mtvk_work_close(&queue_);
    IOSimpleLockUnlockEnableInterrupt(lock_, flags);
    if (state != MTVK_OK && state != MTVK_BUSY) { IOLockUnlock(control_); return kIOReturnError; }
    for (;;) {
        flags = IOSimpleLockLockDisableInterrupt(lock_);
        const bool fault = fault_;
        const bool drained = queue_.phase == MTVK_WORK_CLOSED && invocations_ == 0;
        thread_call_t call = call_;
        IOSimpleLockUnlockEnableInterrupt(lock_, flags);
        if (fault) { IOLockUnlock(control_); return kIOReturnError; }
        if (drained && (!call || thread_call_free(call))) {
            flags = IOSimpleLockLockDisableInterrupt(lock_);
            call_ = nullptr;
            const bool releaseAnchor = anchored_;
            anchored_ = false;
            IOSimpleLockUnlockEnableInterrupt(lock_, flags);
            IOLockUnlock(control_);
            if (releaseAnchor) release();
            return kIOReturnSuccess;
        }
        if (metalvk::platform::uptimeNanoseconds() >= deadline) {
            IOLockUnlock(control_); return kIOReturnTimeout;
        }
        IOSleep(PollMilliseconds);
    }
}
void MetalVKWorkQueue::free() {
    // Successful close removed the call/anchor and invocation references keep
    // any final callback epilogue alive. Partial create failures never scheduled.
    if (control_) { IOLockFree(control_); control_ = nullptr; }
    if (lock_) { IOSimpleLockFree(lock_); lock_ = nullptr; }
    OSObject::free();
}
