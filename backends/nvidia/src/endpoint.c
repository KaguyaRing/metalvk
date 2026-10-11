// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "nvidia/endpoint.h"
#include "nvidia/boot.h"
#include "base/span.h"
enum { SnapshotDisplayClass = 3u };
_Static_assert(MTVK_NV_QUERY_REQUEST_BYTES == sizeof(mtvk_nv_request), "IOKit request size");
_Static_assert(MTVK_NV_QUERY_RESPONSE_BYTES == sizeof(mtvk_nv_response) + MTVK_NV_DEVICE_WIRE_BYTES,
               "IOKit response size");
mtvk_status mtvk_nv_discovery_from_pci(const mtvk_diag_snapshot *snapshot,
                                      mtvk_nv_device_info *out) {
    if (!snapshot || !out || !mtvk_span_valid(snapshot, sizeof(*snapshot)) ||
        !mtvk_span_valid(out, sizeof(*out)) ||
        mtvk_spans_overlap(snapshot, sizeof(*snapshot), out, sizeof(*out)))
        return MTVK_INVALID_ARGUMENT;
    if (snapshot->phase != MTVK_DIAG_PCI_READY || snapshot->capabilities != MTVK_DIAG_CAP_PCI ||
        !snapshot->registry_id) return MTVK_INVALID_ARGUMENT;
    if (snapshot->vendor_id != MTVK_NV_VENDOR_ID || snapshot->device_id != MTVK_NV_INITIAL_DEVICE_ID)
        return MTVK_NOT_SUPPORTED;
    if ((snapshot->class_revision >> 24u) != SnapshotDisplayClass || (snapshot->pci_address & UINT32_C(0xff000000)) ||
        ((snapshot->pci_address >> 8u) & 0xffu) > 31u || (snapshot->pci_address & 0xffu) > 7u)
        return MTVK_INVALID_ARGUMENT;
    mtvk_nv_device_info info;
    mtvk_nv_device_info_init(&info);
    info.device_type = MTVK_NV_DEVICE_DISCRETE;
    info.device_id = snapshot->device_id;
    info.pci_bus = (uint8_t)(snapshot->pci_address >> 16u);
    info.pci_device = (uint8_t)(snapshot->pci_address >> 8u);
    info.pci_function = (uint8_t)snapshot->pci_address;
    info.pci_revision = (uint8_t)snapshot->class_revision;
    *out = info;
    return MTVK_OK;
}
