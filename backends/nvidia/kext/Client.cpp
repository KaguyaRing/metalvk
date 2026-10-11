// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "Client.hpp"
#include "Driver.hpp"
#include "nvidia/endpoint.h"
#include <IOKit/IOKitKeys.h>
OSDefineMetaClassAndStructors(MetalVKNvidiaClient, IOUserClient)
bool MetalVKNvidiaClient::initWithTask(task_t task, void *security, UInt32 type) {
    if (!task || type != MTVK_NV_CONNECT_TYPE ||
        clientHasPrivilege(task, kIOClientPrivilegeLocalUser) != kIOReturnSuccess ||
        !IOUserClient::initWithTask(task, security, type)) return false;
    return metalvk::platform::configureReadOnlyClient(this);
}
bool MetalVKNvidiaClient::start(IOService *provider) {
    auto *device = OSDynamicCast(MetalVKDevice, provider);
    if (!device || device->isInactive() || !IOUserClient::start(provider)) return false;
    device->retain(); device_ = device;
    return true;
}
IOReturn MetalVKNvidiaClient::clientClose() {
    __atomic_store_n(&closed_, 1u, __ATOMIC_RELEASE);
    terminate();
    return kIOReturnSuccess;
}
void MetalVKNvidiaClient::free() {
    if (device_) { device_->release(); device_ = nullptr; }
    IOUserClient::free();
}
IOReturn MetalVKNvidiaClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args,
    IOExternalMethodDispatch *, OSObject *, void *) {
    if (__atomic_load_n(&closed_, __ATOMIC_ACQUIRE) || isInactive()) return kIOReturnOffline;
    if (selector != MTVK_NV_QUERY_SELECTOR || !args) return kIOReturnBadArgument;
    static const IOExternalMethodDispatch method = {
        query, 0, MTVK_NV_QUERY_REQUEST_BYTES, 0, MTVK_NV_QUERY_RESPONSE_BYTES
    };
    return IOUserClient::externalMethod(selector, args,
        const_cast<IOExternalMethodDispatch *>(&method), this, nullptr);
}
IOReturn MetalVKNvidiaClient::query(OSObject *target, void *, IOExternalMethodArguments *args) {
    auto *client = OSDynamicCast(MetalVKNvidiaClient, target);
    if (!client || !args || args->structureInputDescriptor || args->structureOutputDescriptor ||
        args->asyncWakePort || args->scalarInputCount || args->scalarOutputCount ||
        !args->structureInput || !args->structureOutput ||
        args->structureInputSize != MTVK_NV_QUERY_REQUEST_BYTES ||
        args->structureOutputSize != MTVK_NV_QUERY_RESPONSE_BYTES) return kIOReturnBadArgument;
    if (__atomic_load_n(&client->closed_, __ATOMIC_ACQUIRE) || client->isInactive() || !client->device_)
        return kIOReturnOffline;
    size_t written = 0;
    const IOReturn result = client->device_->copyDiscovery(args->structureInput, args->structureInputSize,
        args->structureOutput, args->structureOutputSize, &written);
    if (result == kIOReturnSuccess) args->structureOutputSize = written;
    return result;
}
