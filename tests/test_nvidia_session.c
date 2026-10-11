// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
/* Capability-gating and refusal-path conformance only: NOT NVIDIA hardware
 * execution evidence. Exercises the shared session/dispatch source that the
 * kernel endpoint and the user-space adapter both build, so a green run proves
 * the two sides agree on capability advertisement and on how every unbacked
 * op is refused, never that a GPU executed anything. */
#include "nvidia/init.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
#define RESPONSE_BYTES (sizeof(mtvk_nv_response) + sizeof(mtvk_nv_device_info))

static void fixture(mtvk_nv_device_info *info) {
    mtvk_nv_device_info_init(info);
    /* Constructed transport fixture, not a hardware query or execution claim. */
    info->device_type = (uint32_t)MTVK_NV_DEVICE_DISCRETE;
    info->device_id = 0x28e0u;
    info->chipset = 0x0197u;
    info->sm = 89u;
    info->gpc_count = 3u;
    info->tpc_count = 12u;
    info->cls_eng3d = 0xc997u;
    info->vram_size_B = UINT64_C(8589934592);
    info->bind_align_B = 4096u;
    info->va_start = UINT64_C(0x1000);
    info->va_end = UINT64_C(0x1000000000);
    info->has_alloc_tiled = 1u;
    info->has_map_fixed = 1u;
    info->has_overmap = 1u;
    info->has_compression = 1u;
    info->has_dma_buf = 1u;
}

static int capability_masking(void) {
    mtvk_nv_device_info info;
    fixture(&info);

    const mtvk_nv_device_info none = mtvk_nv_effective_device_info(&info, 0u);
    CHECK(none.has_alloc_tiled == 0u && none.has_map_fixed == 0u && none.has_overmap == 0u);
    CHECK(none.has_compression == 0u && none.has_dma_buf == 0u);
    CHECK(none.device_id == info.device_id && none.va_start == info.va_start);

    const uint32_t all = MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_ALLOC_TILED) |
        MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_MAP) | MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_OVERMAP) |
        MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_IMPORT) | MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_EXPORT);
    const mtvk_nv_device_info kept = mtvk_nv_effective_device_info(&info, all);
    CHECK(memcmp(&kept, &info, sizeof(info)) == 0);

    const uint32_t tiled_only = MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_ALLOC_TILED);
    const mtvk_nv_device_info tiled = mtvk_nv_effective_device_info(&info, tiled_only);
    CHECK(tiled.has_alloc_tiled == 1u && tiled.has_compression == 1u);
    CHECK(tiled.has_map_fixed == 0u && tiled.has_overmap == 0u && tiled.has_dma_buf == 0u);
    const mtvk_nv_device_info import_only = mtvk_nv_effective_device_info(
        &info, MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_IMPORT));
    const mtvk_nv_device_info export_only = mtvk_nv_effective_device_info(
        &info, MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_EXPORT));
    CHECK(import_only.has_dma_buf == 0u && export_only.has_dma_buf == 0u);
    const mtvk_nv_device_info missing = mtvk_nv_effective_device_info(NULL, all);
    mtvk_nv_device_info zero;
    mtvk_nv_device_info_init(&zero);
    mtvk_nv_device_info_init(NULL);
    CHECK(memcmp(&missing, &zero, sizeof(zero)) == 0);
    return 0;
}

static int corrupt_session_and_response_masks_are_refused(void) {
    mtvk_nv_device_info info;
    fixture(&info);
    mtvk_nv_session session;
    mtvk_nv_session_init(&session, UINT32_MAX, &info);
    uint8_t request[sizeof(mtvk_nv_request)];
    uint8_t response[RESPONSE_BYTES];
    uint8_t untouched[RESPONSE_BYTES];
    size_t written = 0;
    CHECK(mtvk_nv_request_init(request, sizeof(request), MTVK_NV_OP_DEVICE_QUERY,
                               0u, 0u, &written) == MTVK_OK);
    memset(response, 0xa5, sizeof(response));
    memcpy(untouched, response, sizeof(response));
    session.version++;
    CHECK(mtvk_nv_session_dispatch(&session, request, sizeof(request), response,
                                   sizeof(response), &written) == MTVK_INCOMPATIBLE_VERSION);
    CHECK(written == 0u && memcmp(response, untouched, sizeof(response)) == 0);
    session.version = MTVK_NV_TRANSPORT_VERSION;
    for (uint32_t bit = 1u; bit < 32u; ++bit) {
        session.backed_op_mask = MTVK_NV_IMPLEMENTED_OPS | (UINT32_C(1) << bit);
        written = 123u;
        CHECK(mtvk_nv_session_dispatch(&session, request, sizeof(request), response,
                                       sizeof(response), &written) == MTVK_INVALID_ARGUMENT);
        CHECK(written == 0u && memcmp(response, untouched, sizeof(response)) == 0);
    }
    session.backed_op_mask = 0u;
    CHECK(mtvk_nv_session_dispatch(&session, request, sizeof(request), response,
                                   sizeof(response), &written) == MTVK_INVALID_ARGUMENT);
    CHECK(written == 0u && memcmp(response, untouched, sizeof(response)) == 0);
    for (uint32_t bit = (uint32_t)MTVK_NV_OP_COUNT; bit < 32u; ++bit) {
        const uint32_t unknown = UINT32_C(1) << bit;
        CHECK(mtvk_nv_response_init(response, sizeof(response), MTVK_NV_OP_DEVICE_QUERY,
                                    MTVK_OK, 0u, unknown, &written) == MTVK_INVALID_ARGUMENT);
        CHECK(memcmp(response, untouched, sizeof(response)) == 0);
        CHECK(mtvk_nv_response_init(response, sizeof(response), MTVK_NV_OP_DEVICE_QUERY,
                                    MTVK_OK, 0u, 0u, &written) == MTVK_OK);
        /* Independently forge wire bytes, bypassing the encoder. */
        response[20u + bit / 8u] = (uint8_t)(1u << (bit % 8u));
        mtvk_nv_response_view view;
        CHECK(mtvk_nv_transport_validate_response(response, written, &view) == MTVK_INVALID_ARGUMENT);
        memcpy(response, untouched, sizeof(response));
    }
    return 0;
}

static int session_gating(void) {
    mtvk_nv_device_info info;
    fixture(&info);
    mtvk_nv_session session;
    mtvk_nv_session_init(&session, UINT32_MAX, &info);
    CHECK(session.version == MTVK_NV_TRANSPORT_VERSION);
    /* Nothing beyond the implemented set may ever be advertised. */
    CHECK(session.backed_op_mask == MTVK_NV_IMPLEMENTED_OPS);
    CHECK(session.backed_op_mask == MTVK_NV_OP_BIT(MTVK_NV_OP_DEVICE_QUERY));
    CHECK(session.device.device_id == info.device_id && session.device.va_end == info.va_end);
    mtvk_nv_session_init(&session, 0u, NULL);
    CHECK(session.backed_op_mask == MTVK_NV_OP_BIT(MTVK_NV_OP_DEVICE_QUERY));
    CHECK(session.device.device_id == 0u);
    mtvk_nv_session_init(NULL, UINT32_MAX, &info);
    return 0;
}

static int device_query_path(void) {
    mtvk_nv_device_info info;
    fixture(&info);
    mtvk_nv_session session;
    mtvk_nv_session_init(&session, UINT32_MAX, &info);

    uint8_t request[sizeof(mtvk_nv_request)];
    uint8_t response[RESPONSE_BYTES];
    size_t written = 0;
    CHECK(mtvk_nv_request_init(request, sizeof(request), (uint32_t)MTVK_NV_OP_DEVICE_QUERY,
                               0u, 0u, &written) == MTVK_OK);
    written = 0;
    CHECK(mtvk_nv_session_dispatch(&session, request, sizeof(request), response, sizeof(response),
                                   &written) == MTVK_OK);
    CHECK(written == RESPONSE_BYTES);

    mtvk_nv_response_view view;
    CHECK(mtvk_nv_transport_validate_response(response, written, &view) == MTVK_OK);
    CHECK(view.header.op == (uint32_t)MTVK_NV_OP_DEVICE_QUERY);
    CHECK(view.header.status == (uint32_t)MTVK_OK);
    CHECK(view.header.backed_op_mask == MTVK_NV_IMPLEMENTED_OPS);
    CHECK(view.payload_bytes == (uint32_t)sizeof(mtvk_nv_device_info));

    /* The response must already be capability-clipped: only DEVICE_QUERY is
     * backed, so every memory capability the fixture requested is cleared. */
    const mtvk_nv_device_info effective =
        mtvk_nv_effective_device_info(&info, MTVK_NV_IMPLEMENTED_OPS);
    CHECK(memcmp(view.payload, &effective, sizeof(effective)) == 0);
    CHECK(effective.has_alloc_tiled == 0u && effective.has_dma_buf == 0u);
    CHECK(effective.device_id == info.device_id && effective.cls_eng3d == info.cls_eng3d);
    CHECK(effective.va_start == info.va_start && effective.bind_align_B == info.bind_align_B);

    /* A DEVICE_QUERY carrying a payload is malformed, not silently accepted. */
    uint8_t with_payload[sizeof(mtvk_nv_request) + 4u];
    size_t payload_written = 0;
    CHECK(mtvk_nv_request_init(with_payload, sizeof(with_payload), (uint32_t)MTVK_NV_OP_DEVICE_QUERY,
                               4u, 0u, &payload_written) == MTVK_OK);
    written = 0;
    CHECK(mtvk_nv_session_dispatch(&session, with_payload, sizeof(with_payload), response,
                                   sizeof(response), &written) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_nv_transport_validate_response(response, written, &view) == MTVK_OK);
    CHECK(view.header.status == (uint32_t)MTVK_INVALID_ARGUMENT && view.payload_bytes == 0u);
    return 0;
}

static int unbacked_ops_are_refused(void) {
    mtvk_nv_device_info info;
    fixture(&info);
    mtvk_nv_session session;
    mtvk_nv_session_init(&session, UINT32_MAX, &info);
    uint8_t request[sizeof(mtvk_nv_request)];
    uint8_t response[RESPONSE_BYTES];
    unsigned refused = 0;
    for (uint32_t op = 0; op < (uint32_t)MTVK_NV_OP_COUNT; ++op) {
        if (op == (uint32_t)MTVK_NV_OP_DEVICE_QUERY) continue;
        size_t request_written = 0;
        CHECK(mtvk_nv_request_init(request, sizeof(request), op, 0u, 0u, &request_written) == MTVK_OK);
        size_t written = 0;
        CHECK(mtvk_nv_session_dispatch(&session, request, request_written, response,
                                       sizeof(response), &written) == MTVK_NOT_SUPPORTED);
        mtvk_nv_response_view view;
        CHECK(mtvk_nv_transport_validate_response(response, written, &view) == MTVK_OK);
        CHECK(view.header.op == op && view.header.status == (uint32_t)MTVK_NOT_SUPPORTED);
        CHECK(view.payload_bytes == 0u);
        CHECK(view.header.backed_op_mask == MTVK_NV_IMPLEMENTED_OPS);
        ++refused;
    }
    CHECK(refused == (unsigned)MTVK_NV_OP_COUNT - 1u);
    return 0;
}

static int malformed_and_bounds(void) {
    mtvk_nv_device_info info;
    fixture(&info);
    mtvk_nv_session session;
    mtvk_nv_session_init(&session, UINT32_MAX, &info);
    uint8_t request[sizeof(mtvk_nv_request)];
    uint8_t response[RESPONSE_BYTES];
    size_t request_written = 0;
    CHECK(mtvk_nv_request_init(request, sizeof(request), (uint32_t)MTVK_NV_OP_DEVICE_QUERY,
                               0u, 0u, &request_written) == MTVK_OK);

    CHECK(mtvk_nv_session_dispatch(NULL, request, sizeof(request), response, sizeof(response),
                                   &request_written) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_nv_session_dispatch(&session, request, sizeof(request), NULL, sizeof(response),
                                   &request_written) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_nv_session_dispatch(&session, request, sizeof(request), response, sizeof(response),
                                   NULL) == MTVK_INVALID_ARGUMENT);

    /* A truncated request still yields a defined status response, never a pass. */
    size_t written = 0;
    CHECK(mtvk_nv_session_dispatch(&session, request, 5u, response, sizeof(response),
                                   &written) == MTVK_BUFFER_TOO_SMALL);
    mtvk_nv_response_view view;
    CHECK(mtvk_nv_transport_validate_response(response, written, &view) == MTVK_OK);
    CHECK(view.header.status == (uint32_t)MTVK_BUFFER_TOO_SMALL && view.payload_bytes == 0u);

    /* A response buffer too small for the header writes nothing at all. */
    written = 0xdeadbeefu;
    CHECK(mtvk_nv_session_dispatch(&session, request, sizeof(request), response,
                                   sizeof(mtvk_nv_response) - 1u, &written) == MTVK_BUFFER_TOO_SMALL);
    CHECK(written == 0u);
    return 0;
}

static bool known_status(mtvk_status status) {
    return status == MTVK_OK || status == MTVK_NOT_SUPPORTED || status == MTVK_BUFFER_TOO_SMALL ||
        status == MTVK_INVALID_ARGUMENT || status == MTVK_INCOMPATIBLE_VERSION ||
        status == MTVK_LIMIT_EXCEEDED;
}

static int mutation_invariant(void) {
    mtvk_nv_device_info info;
    fixture(&info);
    mtvk_nv_session session;
    mtvk_nv_session_init(&session, UINT32_MAX, &info);
    uint8_t valid[sizeof(mtvk_nv_request)];
    size_t valid_written = 0;
    CHECK(mtvk_nv_request_init(valid, sizeof(valid), (uint32_t)MTVK_NV_OP_DEVICE_QUERY,
                               0u, 0u, &valid_written) == MTVK_OK);
    const mtvk_nv_device_info effective =
        mtvk_nv_effective_device_info(&info, MTVK_NV_IMPLEMENTED_OPS);
    uint32_t seed = UINT32_C(0xc2b2ae35);
    unsigned accepted = 0, refused = 0;
    for (unsigned iteration = 0; iteration < 20000u; ++iteration) {
        uint8_t mutated[sizeof(valid)];
        memcpy(mutated, valid, sizeof(valid));
        seed = seed * UINT32_C(1664525) + UINT32_C(1013904223);
        mutated[(size_t)(seed % (uint32_t)sizeof(mutated))] = (uint8_t)(seed >> 24);
        uint8_t response[RESPONSE_BYTES];
        size_t written = 0;
        const mtvk_status result = mtvk_nv_session_dispatch(&session, mutated, sizeof(mutated),
                                                            response, sizeof(response), &written);
        CHECK(known_status(result));
        if (result == MTVK_OK) {
            /* Only a well-formed DEVICE_QUERY may succeed: with the message size
             * fixed, that pins header_bytes/version/op/reserved/request_bytes
             * (bytes 0..19). Only response_bytes may legitimately differ. */
            CHECK(memcmp(mutated, valid, 20u) == 0);
            mtvk_nv_response_view view;
            CHECK(mtvk_nv_transport_validate_response(response, written, &view) == MTVK_OK);
            CHECK(view.header.op == (uint32_t)MTVK_NV_OP_DEVICE_QUERY);
            CHECK(view.payload_bytes == (uint32_t)sizeof(mtvk_nv_device_info));
            CHECK(memcmp(view.payload, &effective, sizeof(effective)) == 0);
            ++accepted;
        } else {
            ++refused;
        }
    }
    CHECK(accepted > 0u && refused > 0u);
    printf("PASS session dispatch mutation invariant: %u accepted, %u refused\n", accepted, refused);
    return 0;
}

int main(void) {
    CHECK(sizeof(mtvk_nv_response) == 24u);
    CHECK(sizeof(mtvk_nv_device_info) % 4u == 0u);
    CHECK(capability_masking() == 0);
    CHECK(corrupt_session_and_response_masks_are_refused() == 0);
    CHECK(session_gating() == 0);
    CHECK(device_query_path() == 0);
    CHECK(unbacked_ops_are_refused() == 0);
    CHECK(malformed_and_bounds() == 0);
    CHECK(mutation_invariant() == 0);
    printf("PASS NVIDIA session standard: capability gating and refusal paths (no hardware)\n");
    return 0;
}
