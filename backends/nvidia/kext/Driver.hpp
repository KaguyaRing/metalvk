// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_DRIVER_HPP
#define MTVK_DRIVER_HPP
#include "platform/init.hpp"
class MetalVKDevice : public MetalVKPlatformDevice {
    OSDeclareDefaultStructors(MetalVKDevice)
public:
    bool start(IOService *provider) override;
    IOReturn newUserClient(task_t task, void *security, UInt32 type, IOUserClient **out) override;
    IOReturn copyDiscovery(const void *request, size_t requestSize,
                           void *response, size_t capacity, size_t *written);
};
#endif
