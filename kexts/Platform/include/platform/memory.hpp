// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_KERNEL_PLATFORM_MEMORY_HPP
#define MTVK_KERNEL_PLATFORM_MEMORY_HPP
#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <libkern/c++/OSObject.h>
#include "memory/init.h"
class MetalVKMemoryDomain;
class MetalVKWorkQueue;
/* Wired host memory only: not contiguous physical memory, DMA or GPU VA.
 * One owned OSObject reference is returned. All methods and the final release
 * require sleepable thread context, outside interrupt/spinlock/gated callbacks.
 * The byte pointer is borrowed until the buffer's final release; callers must
 * serialize access to its payload. Never export it through a UserClient. */
class MetalVKWiredBuffer final : public OSObject {
    OSDeclareDefaultStructors(MetalVKWiredBuffer)
    friend class MetalVKMemoryDomain;
    MetalVKMemoryDomain *owner_ = nullptr;
    IOBufferMemoryDescriptor *descriptor_ = nullptr;
    void *bytes_ = nullptr;
    uint64_t charge_ = 0, length_ = 0;
    void free() override;
public:
    void *bytes() const { return bytes_; }
    uint64_t length() const { return length_; }
    /* IRQ-safe enqueue of one OWNED buffer reference. On success the caller
     * must not release that reference; on full/offline it still owns it. */
    IOReturn deferRelease(MetalVKWorkQueue *queue);
};
/* A parent domain owns the immutable policy and serializes quota/state. Each
 * reservation retains the domain until allocation rollback or buffer release.
 * The domain does not retain buffers, so there is no reference cycle. */
class MetalVKMemoryDomain final : public OSObject {
    OSDeclareDefaultStructors(MetalVKMemoryDomain)
    friend class MetalVKWiredBuffer;
    IOLock *lock_ = nullptr;
    mtvk_memory_budget budget_ {};
    bool initWithBudget(const mtvk_memory_budget &budget);
    IOReturn finishReservation(uint64_t charge);
    void free() override;
public:
    /* NULL limits selects documented defaults; invalid explicit policy fails
     * with BadArgument rather than being silently replaced. Output cleared. */
    static IOReturn create(const mtvk_memory_limits *limits, MetalVKMemoryDomain **out);
    IOReturn allocate(uint64_t bytes, MetalVKWiredBuffer **out);
    /* Rejects new reservations and returns busy while buffers/pending allocation
     * remain. Caller keeps its reference and retries after draining users. */
    IOReturn close();
    IOReturn copyBudget(mtvk_memory_budget *out);
};
#endif
