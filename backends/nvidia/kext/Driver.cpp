// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "Driver.hpp"
#include "Probe.hpp"
#include "Client.hpp"
#include "nvidia/endpoint.h"
#include "nvidia/session.h"
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
    if (!setProperty("MetalVKNvidiaTransportVersion", MTVK_NV_TRANSPORT_VERSION, 32) ||
        !setProperty("MetalVKNvidiaBackedOps", MTVK_NV_IMPLEMENTED_OPS, 32)) return false;
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
IOReturn MetalVKDevice::newUserClient(task_t task, void *security, UInt32 type, IOUserClient **out) {
    if (type != MTVK_NV_CONNECT_TYPE) return MetalVKPlatformDevice::newUserClient(task, security, type, out);
    if (!out) return kIOReturnBadArgument;
    *out = nullptr;
    if (!task) return kIOReturnBadArgument;
    if (isInactive()) return kIOReturnOffline;
    auto *client = new MetalVKNvidiaClient;
    if (!client) return kIOReturnNoMemory;
    if (!client->initWithTask(task, security, type)) { client->release(); return kIOReturnNotPrivileged; }
    if (!client->attach(this)) { client->release(); return kIOReturnNotAttached; }
    if (!client->start(this)) { client->detach(this); client->release(); return kIOReturnNotReady; }
    *out = client;
    return kIOReturnSuccess;
}
IOReturn MetalVKDevice::copyDiscovery(const void *request, size_t requestSize,
    void *response, size_t capacity, size_t *written) {
    if (!request || !response || !written) return kIOReturnBadArgument;
    *written = 0;
    mtvk_diag_snapshot snapshot {};
    const IOReturn copied = copySnapshot(&snapshot);
    if (copied != kIOReturnSuccess) return copied;
    mtvk_nv_device_info info;
    if (mtvk_nv_discovery_from_pci(&snapshot, &info) != MTVK_OK) return kIOReturnError;
    mtvk_nv_session session;
    mtvk_nv_session_init(&session, 0u, &info);
    (void)mtvk_nv_session_dispatch(&session, request, requestSize, response, capacity, written);
    // Semantic failures are carried in the response, not disguised as success.
    return *written ? kIOReturnSuccess : kIOReturnBadArgument;
}
