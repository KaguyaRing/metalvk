// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "platform/init.hpp"
#include <IOKit/IOKitKeys.h>
namespace {
// Stable XNU property names, absent from older compatibility header snapshots.
constexpr const char *DefaultLocking = "IOUserClientDefaultLocking";
constexpr const char *SetPropertiesLocking = "IOUserClientDefaultLockingSetProperties";
constexpr const char *SingleThreadMethods = "IOUserClientDefaultLockingSingleThreadExternalMethod";
}
OSDefineMetaClassAndStructors(MetalVKDiagnosticClient, IOUserClient)
bool MetalVKDiagnosticClient::initWithTask(task_t task, void *security, UInt32 type) {
    if (!task || type != MTVK_DIAG_CONNECT_TYPE ||
        clientHasPrivilege(task, kIOClientPrivilegeLocalUser) != kIOReturnSuccess ||
        !IOUserClient::initWithTask(task, security, type)) return false;
    return setProperty(DefaultLocking, kOSBooleanTrue) &&
        setProperty(SetPropertiesLocking, kOSBooleanTrue) &&
        setProperty(SingleThreadMethods, kOSBooleanTrue);
}
bool MetalVKDiagnosticClient::start(IOService *provider) {
    auto *device = OSDynamicCast(MetalVKPlatformDevice, provider);
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
