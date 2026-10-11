// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "platform/init.hpp"
OSDefineMetaClassAndStructors(MetalVKPlatformDevice, IOService)
bool MetalVKPlatformDevice::init(OSDictionary *dictionary) {
    if (!IOService::init(dictionary)) return false;
    lock_ = IOLockAlloc();
    return lock_ != nullptr;
}
bool MetalVKPlatformDevice::start(IOService *provider) {
    auto *pci = OSDynamicCast(IOPCIDevice, provider);
    if (!pci || !lock_) return false;
    mtvk_diag_snapshot snapshot {};
    if (metalvk::platform::capturePci(pci, getRegistryEntryID(), &snapshot) != kIOReturnSuccess)
        return false;
    // Set fallible properties before starting the superclass: a failed start
    // must not leave superclass resources started without a matching stop.
    if (!setProperty("MetalVKProtocolVersion", MTVK_DIAG_VERSION, 32) ||
        !setProperty("MetalVKCapabilities", MTVK_DIAG_CAP_PCI, 32)) return false;
    if (!IOService::start(provider)) return false;
    IOLockLock(lock_);
    snapshot_ = snapshot;
    ready_ = true;
    IOLockUnlock(lock_);
    registerService();
    return true;
}
void MetalVKPlatformDevice::stop(IOService *provider) {
    if (lock_) {
        IOLockLock(lock_);
        ready_ = false;
        IOLockUnlock(lock_);
    }
    IOService::stop(provider);
}
void MetalVKPlatformDevice::free() {
    if (lock_) { IOLockFree(lock_); lock_ = nullptr; }
    IOService::free();
}
IOReturn MetalVKPlatformDevice::copyDiagnostics(void *out, size_t size) {
    if (!out || size != MTVK_DIAG_RESPONSE_SIZE || !lock_) return kIOReturnBadArgument;
    mtvk_diag_snapshot snapshot {};
    IOLockLock(lock_);
    const bool ready = ready_;
    if (ready) snapshot = snapshot_;
    IOLockUnlock(lock_);
    if (!ready || isInactive()) return kIOReturnOffline;
    return mtvk_diag_encode(&snapshot, out, size) == MTVK_OK ? kIOReturnSuccess : kIOReturnError;
}
IOReturn MetalVKPlatformDevice::newUserClient(task_t task, void *security, UInt32 type,
                                             IOUserClient **out) {
    if (!out) return kIOReturnBadArgument;
    *out = nullptr;
    if (!task || type != MTVK_DIAG_CONNECT_TYPE) return kIOReturnBadArgument;
    if (isInactive()) return kIOReturnOffline;
    auto *client = new MetalVKDiagnosticClient;
    if (!client) return kIOReturnNoMemory;
    if (!client->initWithTask(task, security, type)) {
        client->release(); return kIOReturnNotPrivileged;
    }
    if (!client->attach(this)) { client->release(); return kIOReturnNotAttached; }
    if (!client->start(this)) {
        client->detach(this); client->release(); return kIOReturnNotReady;
    }
    *out = client;
    return kIOReturnSuccess;
}
