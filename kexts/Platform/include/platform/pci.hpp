// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_KERNEL_PLATFORM_PCI_HPP
#define MTVK_KERNEL_PLATFORM_PCI_HPP
#include <IOKit/pci/IOPCIDevice.h>
#include "ipc/init.h"
namespace metalvk { namespace platform {
/* Provider is borrowed for the call and must remain attached. The snapshot is
 * published only after complete validation; failure leaves output unchanged. */
IOReturn capturePci(IOPCIDevice *pci, uint64_t serviceId, mtvk_diag_snapshot *out);
} }
#endif
