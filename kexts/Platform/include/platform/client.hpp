// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_KERNEL_PLATFORM_CLIENT_HPP
#define MTVK_KERNEL_PLATFORM_CLIENT_HPP
#include <IOKit/IOUserClient.h>
class MetalVKPlatformDevice;
class MetalVKDiagnosticClient : public IOUserClient {
    OSDeclareDefaultStructors(MetalVKDiagnosticClient)
    MetalVKPlatformDevice *device_ = nullptr; // retained, immutable after start
    UInt32 closed_ = 0; // atomic; default IOKit locking serializes external methods
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
