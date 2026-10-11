// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "platform/pci.hpp"
namespace metalvk { namespace platform {
namespace {
constexpr UInt8 VendorRegister = 0x00, DeviceRegister = 0x02;
constexpr UInt8 ClassRegister = 0x08, SubsystemVendorRegister = 0x2c;
constexpr UInt8 SubsystemDeviceRegister = 0x2e, BarRegister = 0x10, BarStride = 4;
constexpr uint32_t DisplayClass = 3;
}
IOReturn capturePci(IOPCIDevice *pci, uint64_t serviceId, mtvk_diag_snapshot *out) {
    if (!pci || !out || !serviceId) return kIOReturnBadArgument;
    if (pci->isInactive()) return kIOReturnOffline;
    mtvk_diag_snapshot snapshot {};
    snapshot.vendor_id = pci->configRead16(VendorRegister);
    snapshot.device_id = pci->configRead16(DeviceRegister);
    snapshot.class_revision = pci->configRead32(ClassRegister);
    if (!snapshot.vendor_id || snapshot.vendor_id == UINT16_MAX ||
        snapshot.device_id == UINT16_MAX || snapshot.class_revision == UINT32_MAX)
        return kIOReturnOffline;
    if ((snapshot.class_revision >> 24) != DisplayClass) return kIOReturnUnsupported;
    snapshot.subsystem_vendor_id = pci->configRead16(SubsystemVendorRegister);
    snapshot.subsystem_device_id = pci->configRead16(SubsystemDeviceRegister);
    snapshot.pci_address = (static_cast<uint32_t>(pci->getBusNumber()) << 16) |
        (static_cast<uint32_t>(pci->getDeviceNumber()) << 8) | pci->getFunctionNumber();
    snapshot.registry_id = serviceId;
    snapshot.phase = MTVK_DIAG_PCI_READY;
    snapshot.capabilities = MTVK_DIAG_CAP_PCI;
    for (size_t i = 0; i < MTVK_DIAG_BAR_COUNT; ++i) {
        const auto reg = static_cast<UInt8>(BarRegister + i * BarStride);
        snapshot.bars[i].config_register = reg;
        auto *memory = pci->getDeviceMemoryWithRegister(reg); // borrowed, never mapped
        if (memory && memory->getLength()) {
            snapshot.bars[i].flags = MTVK_DIAG_BAR_PRESENT;
            snapshot.bars[i].length = memory->getLength();
        }
    }
    if (pci->isInactive() || pci->configRead16(VendorRegister) != snapshot.vendor_id ||
        pci->configRead16(DeviceRegister) != snapshot.device_id) return kIOReturnOffline;
    uint8_t encoded[MTVK_DIAG_RESPONSE_SIZE];
    if (mtvk_diag_encode(&snapshot, encoded, sizeof(encoded)) != MTVK_OK) return kIOReturnError;
    *out = snapshot;
    return kIOReturnSuccess;
}
} }
