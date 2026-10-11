// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_KERNEL_PLATFORM_DEVICE_HPP
#define MTVK_KERNEL_PLATFORM_DEVICE_HPP
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include "ipc/init.h"

/* Shared PCI diagnostics service, not a GPU execution driver. start/stop are
 * serialized by IOKit; snapshot readers use the lock and copy-before-encode.
 * Vendor subclasses must validate their explicit device policy before start.
 * No provider open, MMIO mapping, DMA, interrupt or PCI register writes. */
class MetalVKPlatformDevice : public IOService {
    OSDeclareDefaultStructors(MetalVKPlatformDevice)
    IOLock *lock_ = nullptr;
    mtvk_diag_snapshot snapshot_ {};
    bool ready_ = false;
public:
    bool init(OSDictionary *dictionary = nullptr) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    void free() override;
    IOReturn newUserClient(task_t task, void *security, UInt32 type,
                          IOUserClient **out) override;
    IOReturn copyDiagnostics(void *out, size_t size);
};
#endif
