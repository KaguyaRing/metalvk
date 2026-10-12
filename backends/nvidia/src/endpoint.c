// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "nvidia/endpoint.h"
#include "nvidia/boot.h"
#include "nvidia/session.h"
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

mtvk_status mtvk_nv_discovery_response_decode(const void *response, size_t size,
                                             mtvk_nv_device_info *out) {
    if (!response || !out || !mtvk_span_valid(response, size) || !mtvk_span_valid(out, sizeof(*out)) ||
        mtvk_spans_overlap(response, size, out, sizeof(*out))) return MTVK_INVALID_ARGUMENT;
    mtvk_nv_response_view view;
    const mtvk_status framed = mtvk_nv_transport_validate_response(response, size, &view);
    if (framed != MTVK_OK) return framed;
    if (view.header.op != MTVK_NV_OP_DEVICE_QUERY || view.header.backed_op_mask != MTVK_NV_IMPLEMENTED_OPS)
        return MTVK_INVALID_ARGUMENT;
    if (view.header.status != MTVK_OK)
        return view.payload_bytes ? MTVK_INVALID_ARGUMENT : (mtvk_status)view.header.status;
    mtvk_nv_device_info info;
    const mtvk_status decoded = mtvk_nv_device_info_decode(view.payload, view.payload_bytes, &info);
    if (decoded != MTVK_OK) return decoded;
    if (info.device_type != MTVK_NV_DEVICE_DISCRETE || info.device_id != MTVK_NV_INITIAL_DEVICE_ID)
        return MTVK_NOT_SUPPORTED;
    *out = mtvk_nv_effective_device_info(&info, view.header.backed_op_mask);
    return MTVK_OK;
}
