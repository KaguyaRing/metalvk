// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_NVIDIA_CLIENT_HPP
#define MTVK_NVIDIA_CLIENT_HPP
#include <IOKit/IOUserClient.h>
class MetalVKDevice;
/* Read-only synchronous endpoint. IOKit single-thread external-method locking
 * serializes calls; retained service, atomic close and copied snapshots avoid
 * using detached provider state. No user memory mapping or DMA methods. */
class MetalVKNvidiaClient : public IOUserClient {
    OSDeclareDefaultStructors(MetalVKNvidiaClient)
    MetalVKDevice *device_ = nullptr;
    uint32_t closed_ = 0u;
    static IOReturn query(OSObject *target, void *, IOExternalMethodArguments *args);
public:
    bool initWithTask(task_t task, void *security, UInt32 type) override;
    bool start(IOService *provider) override;
    IOReturn clientClose() override;
    void free() override;
    IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
        IOExternalMethodDispatch *, OSObject *, void *) override;
};
#endif
