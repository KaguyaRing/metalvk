// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "Driver.hpp"
#include "Probe.hpp"
#include <libkern/c++/OSBoolean.h>
#include <mach/kmod.h>
// OSKext manages C++ constructors and refuses unload with live class instances.
extern "C" kern_return_t MetalVKStart(kmod_info_t *, void *) { return KERN_SUCCESS; }
extern "C" kern_return_t MetalVKStop(kmod_info_t *, void *) { return KERN_SUCCESS; }
extern "C" {
KMOD_EXPLICIT_DECL(org.metalvk.driver, "0.0.1", MetalVKStart, MetalVKStop)
}
namespace {
constexpr UInt8 VendorRegister = 0x00, DeviceRegister = 0x02;
constexpr const char *ProbeOption = "MetalVKNvidiaReadOnlyProbe";
}
OSDefineMetaClassAndStructors(MetalVKDevice, MetalVKPlatformDevice)
bool MetalVKDevice::start(IOService *provider) {
    auto *pci = OSDynamicCast(IOPCIDevice, provider);
    // Keep the original exact hardware policy; generic diagnostics are not
    // evidence that any additional device supports GPU execution.
    if (!pci || pci->configRead16(VendorRegister) != MTVK_NV_VENDOR_ID ||
        pci->configRead16(DeviceRegister) != MTVK_NV_INITIAL_DEVICE_ID) return false;
    auto *option = getProperty(ProbeOption);
    auto *enabled = OSDynamicCast(OSBoolean, option);
    // Explicit opt-in until real isolated hardware loading is verified. A
    // missing/invalid ordinary option uses false, reported in registry state.
    if (!setProperty("MetalVKNvidiaPhase", option && !enabled ? "disabled-invalid-option" : "diagnostics-only"))
        return false;
    if (enabled && enabled->isTrue()) {
        mtvk_nv_identity identity {};
        const IOReturn result = metalvk::nvidia::probeBoot(pci, this, &identity);
        if (result != kIOReturnSuccess) {
            IOLog("metalvk-nvidia: read-only boot probe failed (IOReturn 0x%08x)\n", result);
            return false;
        }
        if (!setProperty("MetalVKNvidiaArchitecture", identity.architecture, 8) ||
            !setProperty("MetalVKNvidiaImplementation", identity.implementation, 8) ||
            !setProperty("MetalVKNvidiaMajorRevision", identity.major_revision, 8) ||
            !setProperty("MetalVKNvidiaMinorRevision", identity.minor_revision, 8) ||
            !setProperty("MetalVKNvidiaExtendedRevision", identity.extended_revision, 8) ||
            !setProperty("MetalVKNvidiaRequiredRMVersion", MTVK_NV_RM_VERSION) ||
            !setProperty("MetalVKNvidiaRequiredGSPImage", MTVK_NV_GSP_IMAGE) ||
            !setProperty("MetalVKNvidiaRequiredSignature", MTVK_NV_GSP_SIGNATURE) ||
            !setProperty("MetalVKNvidiaPhase", "boot-identified-no-execution")) return false;
    }
    return MetalVKPlatformDevice::start(provider);
}
