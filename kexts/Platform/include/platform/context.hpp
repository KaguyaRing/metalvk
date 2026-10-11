// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_KERNEL_PLATFORM_CONTEXT_HPP
#define MTVK_KERNEL_PLATFORM_CONTEXT_HPP
#include <stdint.h>
namespace metalvk { namespace platform {
/* Necessary checks, not proof that the caller is outside a workloop gate. */
bool sleepable();
/* XNU uptime, not wall time; does not assert continuity over system sleep. */
uint64_t uptimeNanoseconds();
} }
#endif
