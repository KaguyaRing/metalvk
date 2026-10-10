// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_IPC_DIAGNOSTICS_H
#define MTVK_IPC_DIAGNOSTICS_H
#include "base/status.h"

#define MTVK_DIAG_VERSION 1u
#define MTVK_DIAG_REQUEST_SIZE 16u
#define MTVK_DIAG_RESPONSE_SIZE 144u
#define MTVK_DIAG_BAR_COUNT 6u
#define MTVK_DIAG_SELECTOR 0u
#define MTVK_DIAG_CONNECT_TYPE 0u
#define MTVK_DIAG_SERVICE_CLASS "MetalVKDevice"
#define MTVK_DIAG_CAP_PCI 1u
#define MTVK_DIAG_BAR_PRESENT 1u

MTVK_EXTERN_C_BEGIN
typedef enum mtvk_diag_phase {
    MTVK_DIAG_OFFLINE = 0,
    MTVK_DIAG_PCI_READY = 1
} mtvk_diag_phase;

typedef struct mtvk_diag_bar {
    uint32_t config_register;
    uint32_t flags;
    uint64_t length;
} mtvk_diag_bar;

/* Internal representation; never cast wire bytes to this structure.
 * Only resource lengths are exported, never physical/kernel addresses. */
typedef struct mtvk_diag_snapshot {
    uint16_t vendor_id, device_id, subsystem_vendor_id, subsystem_device_id;
    uint32_t pci_address; /* bus:16, device:8, function:0 */
    uint32_t class_revision; /* PCI class/subclass/interface:31..8, revision:7..0 */
    uint32_t phase, capabilities;
    uint64_t registry_id;
    mtvk_diag_bar bars[MTVK_DIAG_BAR_COUNT];
} mtvk_diag_snapshot;

/* All wire fields are little-endian. Exact lengths and reserved bytes are
 * checked; failed decode leaves the caller's output untouched. */
mtvk_status mtvk_diag_make_request(void *out, size_t size);
mtvk_status mtvk_diag_validate_request(const void *data, size_t size);
mtvk_status mtvk_diag_encode(const mtvk_diag_snapshot *snapshot, void *out, size_t size);
mtvk_status mtvk_diag_decode(const void *data, size_t size, mtvk_diag_snapshot *out);
MTVK_EXTERN_C_END
#endif
