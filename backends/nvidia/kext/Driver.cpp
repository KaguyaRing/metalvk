// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "Driver.hpp"
#include <mach/kmod.h>
// OSKext manages C++ constructors and refuses unload with live class instances.
extern "C" kern_return_t MetalVKStart(kmod_info_t *, void *) { return KERN_SUCCESS; }
extern "C" kern_return_t MetalVKStop(kmod_info_t *, void *) { return KERN_SUCCESS; }
extern "C" {
KMOD_EXPLICIT_DECL(org.metalvk.driver, "0.0.1", MetalVKStart, MetalVKStop)
}
namespace {
constexpr UInt16 Vendor = 0x10de, Device = 0x28e0;
constexpr UInt8 VendorRegister = 0x00, DeviceRegister = 0x02;
}
OSDefineMetaClassAndStructors(MetalVKDevice, MetalVKPlatformDevice)
bool MetalVKDevice::start(IOService *provider) {
    auto *pci = OSDynamicCast(IOPCIDevice, provider);
    // Keep the original exact hardware policy; generic diagnostics are not
    // evidence that any additional device supports GPU execution.
    if (!pci || pci->configRead16(VendorRegister) != Vendor ||
        pci->configRead16(DeviceRegister) != Device) return false;
    return MetalVKPlatformDevice::start(provider);
}
