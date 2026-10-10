// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_DRIVER_HPP
#define MTVK_DRIVER_HPP
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOKitKeys.h>
#include "ipc/init.h"

class MetalVKDevice : public IOService {
    OSDeclareDefaultStructors(MetalVKDevice)
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

class MetalVKDiagnosticClient : public IOUserClient {
    OSDeclareDefaultStructors(MetalVKDiagnosticClient)
    MetalVKDevice *device_ = nullptr; // retained until free; immutable after start
    UInt32 closed_ = 0; // atomic, also checked when clientClose races a request
    static IOReturn readSnapshot(OSObject *target, void *reference,
                                IOExternalMethodArguments *args);
public:
    bool initWithTask(task_t task, void *security, UInt32 type) override;
    bool start(IOService *provider) override;
    IOReturn clientClose() override;
    void free() override;
    IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
        IOExternalMethodDispatch *dispatch = nullptr, OSObject *target = nullptr,
        void *reference = nullptr) override;
};
#endif
