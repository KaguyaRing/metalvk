// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_RUNTIME_TIME_H
#define MTVK_RUNTIME_TIME_H
#include <stdint.h>
#include "base/status.h"
MTVK_EXTERN_C_BEGIN
/* Same monotonic nanosecond domain for both operands. Error leaves out intact. */
mtvk_status mtvk_time_deadline(uint64_t now, uint64_t delay, uint64_t *out);
MTVK_EXTERN_C_END
#endif
