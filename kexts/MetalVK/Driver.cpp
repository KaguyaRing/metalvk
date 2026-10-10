// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "Driver.hpp"
#include <IOKit/pci/IOPCIDevice.h>
#include <mach/kmod.h>

// OSKext initializes/finalizes C++ and refuses unload while class instances
// or dependency references remain. This diagnostic module owns no global I/O.
extern "C" kern_return_t MetalVKStart(kmod_info_t *, void *) { return KERN_SUCCESS; }
extern "C" kern_return_t MetalVKStop(kmod_info_t *, void *) { return KERN_SUCCESS; }
extern "C" {
KMOD_EXPLICIT_DECL(org.metalvk.driver, "0.0.1", MetalVKStart, MetalVKStop)
}

namespace {
constexpr UInt16 Vendor = 0x10de;
constexpr UInt16 Device = 0x28e0;
constexpr UInt8 VendorRegister = 0x00, DeviceRegister = 0x02;
constexpr UInt8 ClassRegister = 0x08, SubsystemVendorRegister = 0x2c;
constexpr UInt8 SubsystemDeviceRegister = 0x2e, BarRegister = 0x10, BarStride = 4;
// Stable IOKit property names from XNU's IOKitKeys.h. MacKernelSDK's
// compatibility header snapshot does not declare these modern macros.
constexpr const char *DefaultLocking = "IOUserClientDefaultLocking";
constexpr const char *SetPropertiesLocking = "IOUserClientDefaultLockingSetProperties";
constexpr const char *SingleThreadMethods = "IOUserClientDefaultLockingSingleThreadExternalMethod";
}
OSDefineMetaClassAndStructors(MetalVKDevice, IOService)
OSDefineMetaClassAndStructors(MetalVKDiagnosticClient, IOUserClient)

bool MetalVKDevice::init(OSDictionary *dictionary) {
    if (!IOService::init(dictionary)) return false;
    lock_ = IOLockAlloc();
    return lock_ != nullptr;
}
bool MetalVKDevice::start(IOService *provider) {
    auto *pci = OSDynamicCast(IOPCIDevice, provider);
    if (!pci || !lock_) return false;
    mtvk_diag_snapshot s {};
    s.vendor_id = pci->configRead16(VendorRegister);
    s.device_id = pci->configRead16(DeviceRegister);
    if (s.vendor_id != Vendor || s.device_id != Device) return false;
    s.subsystem_vendor_id = pci->configRead16(SubsystemVendorRegister);
    s.subsystem_device_id = pci->configRead16(SubsystemDeviceRegister);
    s.class_revision = pci->configRead32(ClassRegister);
    if (s.class_revision == UINT32_MAX || ((s.class_revision >> 24) != 3)) return false;
    s.pci_address = (static_cast<uint32_t>(pci->getBusNumber()) << 16) |
        (static_cast<uint32_t>(pci->getDeviceNumber()) << 8) | pci->getFunctionNumber();
    s.registry_id = getRegistryEntryID();
    s.phase = MTVK_DIAG_PCI_READY;
    s.capabilities = MTVK_DIAG_CAP_PCI;
    for (size_t i = 0; i < MTVK_DIAG_BAR_COUNT; ++i) {
        auto reg = static_cast<UInt8>(BarRegister + i * BarStride);
        s.bars[i].config_register = reg;
        auto *memory = pci->getDeviceMemoryWithRegister(reg); // borrowed provider resource
        if (memory && memory->getLength() != 0) {
            s.bars[i].flags = MTVK_DIAG_BAR_PRESENT;
            s.bars[i].length = memory->getLength();
        }
    }
    uint8_t verified[MTVK_DIAG_RESPONSE_SIZE];
    if (mtvk_diag_encode(&s, verified, sizeof(verified)) != MTVK_OK) return false;
    if (!IOService::start(provider)) return false;
    if (!setProperty("MetalVKProtocolVersion", MTVK_DIAG_VERSION, 32) ||
        !setProperty("MetalVKCapabilities", MTVK_DIAG_CAP_PCI, 32)) return false;
    IOLockLock(lock_);
    snapshot_ = s;
    ready_ = true;
    IOLockUnlock(lock_);
    registerService();
    return true;
}
void MetalVKDevice::stop(IOService *provider) {
    if (lock_) {
        IOLockLock(lock_);
        ready_ = false;
        IOLockUnlock(lock_);
    }
    IOService::stop(provider);
}
void MetalVKDevice::free() {
    if (lock_) { IOLockFree(lock_); lock_ = nullptr; }
    IOService::free();
}
IOReturn MetalVKDevice::copyDiagnostics(void *out, size_t size) {
    if (!out || size != MTVK_DIAG_RESPONSE_SIZE || !lock_) return kIOReturnBadArgument;
    mtvk_diag_snapshot s {};
    IOLockLock(lock_);
    const bool ready = ready_;
    if (ready) s = snapshot_;
    IOLockUnlock(lock_);
    if (!ready || isInactive()) return kIOReturnOffline;
    return mtvk_diag_encode(&s, out, size) == MTVK_OK ? kIOReturnSuccess : kIOReturnError;
}
IOReturn MetalVKDevice::newUserClient(task_t task, void *security, UInt32 type,
                                     IOUserClient **out) {
    if (!out || !task || type != MTVK_DIAG_CONNECT_TYPE) return kIOReturnBadArgument;
    *out = nullptr;
    if (isInactive()) return kIOReturnOffline;
    auto *client = new MetalVKDiagnosticClient;
    if (!client) return kIOReturnNoMemory;
    if (!client->initWithTask(task, security, type)) {
        client->release();
        return kIOReturnNotPrivileged;
    }
    if (!client->attach(this)) { client->release(); return kIOReturnNotAttached; }
    if (!client->start(this)) {
        client->detach(this); client->release(); return kIOReturnNotReady;
    }
    *out = client;
    return kIOReturnSuccess;
}
bool MetalVKDiagnosticClient::initWithTask(task_t task, void *security, UInt32 type) {
    if (!task || type != MTVK_DIAG_CONNECT_TYPE ||
        clientHasPrivilege(task, kIOClientPrivilegeLocalUser) != kIOReturnSuccess ||
        !IOUserClient::initWithTask(task, security, type)) return false;
    return setProperty(DefaultLocking, kOSBooleanTrue) &&
        setProperty(SetPropertiesLocking, kOSBooleanTrue) &&
        setProperty(SingleThreadMethods, kOSBooleanTrue);
}
bool MetalVKDiagnosticClient::start(IOService *provider) {
    auto *device = OSDynamicCast(MetalVKDevice, provider);
    if (!device || !IOUserClient::start(provider)) return false;
    device->retain();
    device_ = device;
    return true;
}
IOReturn MetalVKDiagnosticClient::clientClose() {
    __atomic_store_n(&closed_, 1u, __ATOMIC_RELEASE);
    terminate();
    return kIOReturnSuccess;
}
void MetalVKDiagnosticClient::free() {
    if (device_) { device_->release(); device_ = nullptr; }
    IOUserClient::free();
}
IOReturn MetalVKDiagnosticClient::externalMethod(uint32_t selector,
    IOExternalMethodArguments *args, IOExternalMethodDispatch *, OSObject *, void *) {
    if (__atomic_load_n(&closed_, __ATOMIC_ACQUIRE) || isInactive()) return kIOReturnOffline;
    if (selector != MTVK_DIAG_SELECTOR || !args) return kIOReturnBadArgument;
    static const IOExternalMethodDispatch method = {
        readSnapshot, 0, MTVK_DIAG_REQUEST_SIZE, 0, MTVK_DIAG_RESPONSE_SIZE
    };
    return IOUserClient::externalMethod(selector, args,
        const_cast<IOExternalMethodDispatch *>(&method), this, nullptr);
}
IOReturn MetalVKDiagnosticClient::readSnapshot(OSObject *target, void *,
                                              IOExternalMethodArguments *args) {
    auto *client = OSDynamicCast(MetalVKDiagnosticClient, target);
    if (!client || !args || args->structureInputDescriptor || args->structureOutputDescriptor ||
        args->asyncWakePort || !args->structureOutput ||
        args->structureOutputSize != MTVK_DIAG_RESPONSE_SIZE ||
        mtvk_diag_validate_request(args->structureInput, args->structureInputSize) != MTVK_OK)
        return kIOReturnBadArgument;
    if (__atomic_load_n(&client->closed_, __ATOMIC_ACQUIRE) || !client->device_)
        return kIOReturnOffline;
    return client->device_->copyDiagnostics(args->structureOutput, args->structureOutputSize);
}
