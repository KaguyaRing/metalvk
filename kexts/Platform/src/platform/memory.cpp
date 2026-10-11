// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "platform/memory.hpp"
#include <mach/vm_param.h>
#include <machine/machine_routines.h>
#include <kern/sched_prim.h>
#include <libkern/libkern.h>
namespace {
bool sleepable() {
    return !ml_at_interrupt_context() && preemption_enabled() && ml_get_interrupts_enabled();
}
IOReturn translate(mtvk_status status) {
    switch (status) {
        case MTVK_OK: return kIOReturnSuccess;
        case MTVK_BUSY: return kIOReturnBusy;
        case MTVK_LIMIT_EXCEEDED: return kIOReturnNoResources;
        case MTVK_INVALID_CONTEXT: return kIOReturnOffline;
        case MTVK_INVALID_ARGUMENT: return kIOReturnBadArgument;
        default: return kIOReturnError;
    }
}
}
OSDefineMetaClassAndStructors(MetalVKMemoryDomain, OSObject)
OSDefineMetaClassAndStructors(MetalVKWiredBuffer, OSObject)
IOReturn MetalVKMemoryDomain::create(const mtvk_memory_limits *limits, MetalVKMemoryDomain **out) {
    if (!out) return kIOReturnBadArgument;
    *out = nullptr;
    if (!sleepable()) return kIOReturnCannotLock;
    mtvk_memory_budget budget {};
    const mtvk_status status = mtvk_memory_budget_init(&budget, limits);
    if (status != MTVK_OK) return translate(status);
    auto *domain = new MetalVKMemoryDomain;
    if (!domain) return kIOReturnNoMemory;
    if (!domain->initWithBudget(budget)) { domain->release(); return kIOReturnNoMemory; }
    *out = domain;
    return kIOReturnSuccess;
}
bool MetalVKMemoryDomain::initWithBudget(const mtvk_memory_budget &budget) {
    if (!OSObject::init()) return false;
    budget_ = budget;
    lock_ = IOLockAlloc();
    return lock_ != nullptr;
}
IOReturn MetalVKMemoryDomain::allocate(uint64_t length, MetalVKWiredBuffer **out) {
    if (!out) return kIOReturnBadArgument;
    *out = nullptr;
    if (!sleepable()) return kIOReturnCannotLock;
    if (!lock_) return kIOReturnNotReady;
    uint64_t charge = 0;
    mtvk_status status = mtvk_memory_charge(length, PAGE_SIZE, &charge);
    if (status != MTVK_OK) return translate(status);
    IOLockLock(lock_);
    status = mtvk_memory_budget_reserve(&budget_, charge);
    if (status == MTVK_OK) retain(); // Includes allocations not yet published.
    IOLockUnlock(lock_);
    if (status != MTVK_OK) return translate(status);

    // Allocation/zeroing and all final releases occur outside the domain lock.
    auto *buffer = new MetalVKWiredBuffer;
    if (!buffer || !buffer->init()) {
        if (buffer) buffer->release();
        const IOReturn rollback = finishReservation(charge);
        return rollback == kIOReturnSuccess ? kIOReturnNoMemory : rollback;
    }
    buffer->owner_ = this;
    buffer->charge_ = charge;
    buffer->length_ = length;
    buffer->descriptor_ = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
        kIODirectionInOut | kIOMemoryThreadSafe, static_cast<vm_size_t>(charge), PAGE_SIZE);
    if (!buffer->descriptor_) { buffer->release(); return kIOReturnNoMemory; }
    buffer->bytes_ = buffer->descriptor_->getBytesNoCopy();
    if (!buffer->bytes_) { buffer->release(); return kIOReturnError; }
    bzero(buffer->bytes_, static_cast<size_t>(charge));

    IOLockLock(lock_);
    const bool publish = budget_.phase == MTVK_MEMORY_OPEN;
    if (publish) *out = buffer;
    IOLockUnlock(lock_);
    if (!publish) { buffer->release(); return kIOReturnOffline; }
    return kIOReturnSuccess;
}
IOReturn MetalVKMemoryDomain::finishReservation(uint64_t charge) {
    IOLockLock(lock_);
    const mtvk_status status = mtvk_memory_budget_release(&budget_, charge);
    if (status != MTVK_OK) budget_.phase = MTVK_MEMORY_DRAINING;
    IOLockUnlock(lock_);
    if (status != MTVK_OK) {
        IOLog("MetalVK ERROR: memory accounting invariant failed (%u); retaining domain\n",
            static_cast<unsigned>(status));
        return kIOReturnError; // Fail closed, keep the reservation's domain reference.
    }
    release(); // May free this; no further access to members.
    return kIOReturnSuccess;
}
IOReturn MetalVKMemoryDomain::close() {
    if (!sleepable()) return kIOReturnCannotLock;
    if (!lock_) return kIOReturnNotReady;
    IOLockLock(lock_);
    const mtvk_status status = mtvk_memory_budget_close(&budget_);
    IOLockUnlock(lock_);
    return translate(status);
}
IOReturn MetalVKMemoryDomain::copyBudget(mtvk_memory_budget *out) {
    if (!out) return kIOReturnBadArgument;
    if (!sleepable()) return kIOReturnCannotLock;
    if (!lock_) return kIOReturnNotReady;
    IOLockLock(lock_);
    *out = budget_;
    IOLockUnlock(lock_);
    return kIOReturnSuccess;
}
void MetalVKMemoryDomain::free() {
    // Every outstanding reservation retains us. Last release implies no child
    // or allocating thread still uses this lock. Caller supplies quiescence.
    if (lock_) { IOLockFree(lock_); lock_ = nullptr; }
    OSObject::free();
}
void MetalVKWiredBuffer::free() {
    if (descriptor_) { descriptor_->release(); descriptor_ = nullptr; }
    bytes_ = nullptr;
    if (owner_) {
        auto *owner = owner_;
        owner_ = nullptr;
        // An invariant failure is reported and preserves the owner's reference.
        (void)owner->finishReservation(charge_);
    }
    OSObject::free();
}
