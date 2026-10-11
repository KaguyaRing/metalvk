// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_DRIVER_HPP
#define MTVK_DRIVER_HPP
#include "platform/init.hpp"
class MetalVKDevice : public MetalVKPlatformDevice {
    OSDeclareDefaultStructors(MetalVKDevice)
public:
    bool start(IOService *provider) override;
};
#endif
