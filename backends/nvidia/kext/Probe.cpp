// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "Probe.hpp"
#include <IOKit/IOMemoryDescriptor.h>
#include <libkern/OSAtomic.h>
#include <libkern/OSByteOrder.h>
#include <mach/task.h>
namespace metalvk { namespace nvidia {
#if defined(__x86_64__)
namespace {
constexpr UInt8 Bar0Register = 0x10;
constexpr size_t ConfigStride = sizeof(UInt32);
static_assert(MTVK_NV_BOOT_42_OFFSET + sizeof(uint32_t) <= MTVK_NV_PROBE_MAP_BYTES,
    "boot registers must fit in the bounded BAR0 mapping");
class ProbeResources final {
    IOPCIDevice *pci_;
    IOService *owner_;
public:
    IOMemoryMap *map = nullptr;
    ProbeResources(IOPCIDevice *pci, IOService *owner) : pci_(pci), owner_(owner) {}
    ~ProbeResources() { if (map) map->release(); pci_->close(owner_); }
    ProbeResources(const ProbeResources &) = delete;
    ProbeResources &operator=(const ProbeResources &) = delete;
};
void captureConfig(IOPCIDevice *pci, uint8_t *bytes) {
    for (size_t i = 0; i < MTVK_NV_PCI_CONFIG_BYTES; i += ConfigStride) {
        const UInt32 value = pci->configRead32(static_cast<UInt8>(i));
        for (size_t j = 0; j < ConfigStride; ++j) bytes[i + j] = static_cast<uint8_t>(value >> (j * 8));
    }
}
bool equalRegisters(const mtvk_nv_boot_registers &a, const mtvk_nv_boot_registers &b) {
    return a.boot_0 == b.boot_0 && a.boot_1 == b.boot_1 && a.boot_42 == b.boot_42;
}
uint32_t readBoot(const void *base, size_t offset) {
    // OSReadLittleInt32 is a volatile aligned 32-bit access, not PCI port I/O.
    // PCI ioRead32 uses inl on x86 and is NOT a BAR0 MMIO accessor.
    OSSynchronizeIO();
    const uint32_t value = OSReadLittleInt32(base, offset);
    OSSynchronizeIO();
    return value;
}
mtvk_nv_boot_registers captureBoot(const void *base) {
    return {readBoot(base, MTVK_NV_BOOT_0_OFFSET), readBoot(base, MTVK_NV_BOOT_1_OFFSET),
        readBoot(base, MTVK_NV_BOOT_42_OFFSET)};
}
}
#endif
IOReturn probeBoot(IOPCIDevice *pci, IOService *owner, mtvk_nv_identity *out) {
    if (!pci || !owner || !out) return kIOReturnBadArgument;
#if !defined(__x86_64__)
    return kIOReturnUnsupported;
#else
    if (pci->isInactive()) return kIOReturnOffline;
    if (!pci->open(owner)) return kIOReturnExclusiveAccess;
    ProbeResources resources(pci, owner);
    uint8_t config[MTVK_NV_PCI_CONFIG_BYTES];
    captureConfig(pci, config);
    auto *memory = pci->getDeviceMemoryWithRegister(Bar0Register); // borrowed
    if (!memory || pci->isInactive()) return kIOReturnOffline;
    const uint64_t length = memory->getLength();
    if (mtvk_nv_validate_pci(config, sizeof(config), length) != MTVK_OK) return kIOReturnUnsupported;
    resources.map = memory->createMappingInTask(kernel_task, 0,
        kIOMapAnywhere | kIOMapReadOnly | kIOMapInhibitCache | kIOMapUnique, 0, MTVK_NV_PROBE_MAP_BYTES);
    if (!resources.map) return kIOReturnNoMemory;
    const auto address = resources.map->getVirtualAddress();
    const auto options = resources.map->getMapOptions();
    if (!address || address > UINTPTR_MAX - MTVK_NV_PROBE_MAP_BYTES ||
        (address & (sizeof(uint32_t) - 1)) || resources.map->getLength() != MTVK_NV_PROBE_MAP_BYTES ||
        !(options & kIOMapReadOnly) || (options & kIOMapCacheMask) != kIOMapInhibitCache)
        return kIOReturnBadArgument;
    if (pci->isInactive()) return kIOReturnOffline;
    const void *base = reinterpret_cast<const void *>(address);
    const mtvk_nv_boot_registers first = captureBoot(base);
    if (pci->isInactive()) return kIOReturnOffline;
    const mtvk_nv_boot_registers second = captureBoot(base);
    uint8_t after[MTVK_NV_PCI_CONFIG_BYTES];
    captureConfig(pci, after);
    if (pci->isInactive() || memory != pci->getDeviceMemoryWithRegister(Bar0Register) ||
        memory->getLength() != length || mtvk_nv_validate_pci(after, sizeof(after), length) != MTVK_OK)
        return kIOReturnOffline;
    // Address, command, power and identity must remain stable across reads.
    // Full config comparison is conservative: changing status rejects the probe.
    for (size_t i = 0; i < sizeof(config); ++i) if (config[i] != after[i]) return kIOReturnNotReady;
    if (!equalRegisters(first, second)) return kIOReturnNotReady;
    mtvk_nv_identity identity {};
    const mtvk_status status = mtvk_nv_identify(&first, &identity);
    if (status != MTVK_OK) return status == MTVK_NOT_FOUND ? kIOReturnOffline : kIOReturnUnsupported;
    *out = identity;
    return kIOReturnSuccess;
#endif
}
} }
