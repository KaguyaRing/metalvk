// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_NVIDIA_PROBE_HPP
#define MTVK_NVIDIA_PROBE_HPP
#include <IOKit/pci/IOPCIDevice.h>
#include "nvidia/init.h"
namespace metalvk { namespace nvidia {
/* start thread only; provider attached for entire call. All acquired resources
 * are released before return. No persistent MMIO, IRQ or work-queue readers. */
IOReturn probeBoot(IOPCIDevice *pci, IOService *owner, mtvk_nv_identity *out);
} }
#endif
