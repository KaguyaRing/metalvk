// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "platform/context.hpp"
#include <machine/machine_routines.h>
#include <kern/sched_prim.h>
#include <kern/clock.h>
namespace metalvk { namespace platform {
bool sleepable() {
    return !ml_at_interrupt_context() && preemption_enabled() && ml_get_interrupts_enabled();
}
uint64_t uptimeNanoseconds() {
    uint64_t absolute = 0, nanoseconds = 0;
    clock_get_uptime(&absolute);
    absolutetime_to_nanoseconds(absolute, &nanoseconds);
    return nanoseconds;
}
} }
