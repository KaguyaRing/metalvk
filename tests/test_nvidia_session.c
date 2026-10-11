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
#define RESPONSE_BYTES (sizeof(mtvk_nv_response) + MTVK_NV_DEVICE_WIRE_BYTES)

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

static int discovery_wire_golden_and_failures(void) {
    mtvk_nv_device_info info;
    fixture(&info);
    uint8_t encoded[MTVK_NV_DEVICE_WIRE_BYTES + 2u];
    memset(encoded, 0xa5, sizeof(encoded));
    CHECK(mtvk_nv_device_info_encode(&info, encoded + 1u, MTVK_NV_DEVICE_WIRE_BYTES) == MTVK_OK);
    /* Independent bytes, not a second call to the codec under test. */
    const uint8_t golden[MTVK_NV_DEVICE_WIRE_BYTES] = {
        [0] = 1u, [4] = 0x48u, [5] = 1u, [8] = 1u,
        [12] = 0xe0u, [13] = 0x28u, [16] = 0x97u, [17] = 1u,
        [40] = 89u, [44] = 3u, [48] = 12u, [80] = 0x97u, [81] = 0xc9u,
        [100] = 2u, [288] = 1u, [292] = 1u, [296] = 1u, [300] = 1u, [304] = 1u,
        [309] = 0x10u, [313] = 0x10u, [324] = 0x10u
    };
    CHECK(memcmp(encoded + 1u, golden, sizeof(golden)) == 0);
    CHECK(encoded[0] == 0xa5u && encoded[sizeof(encoded) - 1u] == 0xa5u);
    mtvk_nv_device_info decoded;
    CHECK(mtvk_nv_device_info_decode(encoded + 1u, MTVK_NV_DEVICE_WIRE_BYTES, &decoded) == MTVK_OK);
    CHECK(decoded.device_id == info.device_id && decoded.va_end == info.va_end);
    CHECK(decoded.vram_size_B == info.vram_size_B && decoded.has_dma_buf == 1u);
    /* Full host-field preservation, including high 64-bit words and arrays. */
    mtvk_nv_device_info full;
    fixture(&full);
    full.pci_domain = UINT16_MAX; full.pci_bus = UINT8_MAX;
    full.pci_device = 31u; full.pci_function = 7u; full.pci_revision = 0xa1u;
    full.mp_per_tpc = 2u; full.max_warps_per_mp = 48u;
    full.has_transfer_queue = 1u; full.has_zcull_info = 1u; full.nc_atom_size_B = 64u;
    full.cls_copy = 0xc7b5u; full.cls_eng2d = 0xc5b5u; full.cls_m2mf = 0xc5b0u;
    full.cls_compute = 0xc7c0u; full.cls_gpfifo = 0xc46fu;
    full.vram_size_B = UINT64_C(0xfedcba9876543210);
    full.bar_size_B = UINT64_C(0x123456789abcdef0);
    full.max_smem_per_wg_kB = 100u;
    full.sm_smem_size_count = MTVK_NV_SMEM_CONFIGS;
    for (size_t i = 0; i < MTVK_NV_SMEM_CONFIGS; ++i) full.sm_smem_sizes_kB[i] = (uint16_t)(i + 1u);
    for (size_t i = 0; i < MTVK_NV_ZCULL_FIELDS; ++i) full.zcull_info[i] = UINT32_MAX - (uint32_t)i;
    memcpy(full.device_name, "AD107 fixture", sizeof("AD107 fixture"));
    memcpy(full.chipset_name, "AD107", sizeof("AD107"));
    CHECK(mtvk_nv_device_info_encode(&full, encoded + 1u, MTVK_NV_DEVICE_WIRE_BYTES) == MTVK_OK);
    CHECK(mtvk_nv_device_info_decode(encoded + 1u, MTVK_NV_DEVICE_WIRE_BYTES, &decoded) == MTVK_OK);
#define SAME(field) CHECK(decoded.field == full.field)
    SAME(device_type); SAME(device_id); SAME(chipset); SAME(pci_domain); SAME(pci_bus);
    SAME(pci_device); SAME(pci_function); SAME(pci_revision); SAME(sm); SAME(gpc_count);
    SAME(tpc_count); SAME(mp_per_tpc); SAME(max_warps_per_mp); SAME(has_transfer_queue);
    SAME(has_zcull_info); SAME(nc_atom_size_B); SAME(cls_copy); SAME(cls_eng2d); SAME(cls_eng3d);
    SAME(cls_m2mf); SAME(cls_compute); SAME(cls_gpfifo); SAME(vram_size_B); SAME(bar_size_B);
    SAME(max_smem_per_wg_kB); SAME(sm_smem_size_count); SAME(has_alloc_tiled); SAME(has_map_fixed);
    SAME(has_overmap); SAME(has_compression); SAME(has_dma_buf); SAME(bind_align_B); SAME(va_start); SAME(va_end);
#undef SAME
    CHECK(memcmp(decoded.sm_smem_sizes_kB, full.sm_smem_sizes_kB, sizeof(full.sm_smem_sizes_kB)) == 0);
    CHECK(memcmp(decoded.zcull_info, full.zcull_info, sizeof(full.zcull_info)) == 0);
    CHECK(strcmp(decoded.device_name, full.device_name) == 0 && strcmp(decoded.chipset_name, full.chipset_name) == 0);
    /* Poison host padding and bytes after a valid name terminator. Neither
     * may escape into the wire payload. */
    uint8_t *host = (uint8_t *)&info;
    for (size_t i = offsetof(mtvk_nv_device_info, cls_gpfifo) + sizeof(info.cls_gpfifo);
         i < offsetof(mtvk_nv_device_info, vram_size_B); ++i) host[i] = 0xa5u;
    memset(info.device_name + 1u, 0xa5, sizeof(info.device_name) - 1u);
    CHECK(mtvk_nv_device_info_encode(&info, encoded + 1u, MTVK_NV_DEVICE_WIRE_BYTES) == MTVK_OK);
    CHECK(memcmp(encoded + 1u, golden, sizeof(golden)) == 0);

    const struct { size_t offset; uint8_t byte; mtvk_status status; } invalid[] = {
        {0u, 2u, MTVK_INCOMPATIBLE_VERSION}, {4u, 0u, MTVK_INVALID_ARGUMENT},
        {8u, 3u, MTVK_INVALID_ARGUMENT}, {14u, 1u, MTVK_INVALID_ARGUMENT},
        {25u, 1u, MTVK_INVALID_ARGUMENT}, {28u, 32u, MTVK_INVALID_ARGUMENT},
        {32u, 8u, MTVK_INVALID_ARGUMENT}, {60u, 2u, MTVK_INVALID_ARGUMENT},
        {64u, 2u, MTVK_INVALID_ARGUMENT}, {68u, 3u, MTVK_INVALID_ARGUMENT},
        {74u, 1u, MTVK_INVALID_ARGUMENT}, {118u, 1u, MTVK_INVALID_ARGUMENT},
        {116u, 1u, MTVK_INVALID_ARGUMENT}, {156u, 11u, MTVK_INVALID_ARGUMENT},
        {160u, 1u, MTVK_INVALID_ARGUMENT}, {208u, 1u, MTVK_INVALID_ARGUMENT},
        {209u, 65u, MTVK_INVALID_ARGUMENT}, {272u, 0x80u, MTVK_INVALID_ARGUMENT},
        {288u, 2u, MTVK_INVALID_ARGUMENT}, {308u, 3u, MTVK_INVALID_ARGUMENT},
        {312u, 1u, MTVK_INVALID_ARGUMENT}, {324u, 0u, MTVK_INVALID_ARGUMENT}
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        uint8_t bad[MTVK_NV_DEVICE_WIRE_BYTES];
        memcpy(bad, golden, sizeof(bad));
        bad[invalid[i].offset] = invalid[i].byte;
        memset(&decoded, 0xa5, sizeof(decoded));
        uint8_t before[sizeof(decoded)];
        memcpy(before, &decoded, sizeof(before));
        CHECK(mtvk_nv_device_info_decode(bad, sizeof(bad), &decoded) == invalid[i].status);
        CHECK(memcmp(before, &decoded, sizeof(before)) == 0);
    }
    for (size_t size = 0; size < MTVK_NV_DEVICE_WIRE_BYTES; ++size) {
        CHECK(mtvk_nv_device_info_decode(golden, size, &decoded) == MTVK_BUFFER_TOO_SMALL);
        CHECK(mtvk_nv_device_info_encode(&info, encoded, size) == MTVK_BUFFER_TOO_SMALL);
    }
    CHECK(mtvk_nv_device_info_decode(encoded, sizeof(encoded), &decoded) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_nv_device_info_encode(NULL, encoded, sizeof(encoded)) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_nv_device_info_decode(NULL, 0u, &decoded) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_nv_device_info_decode(golden, sizeof(golden), NULL) == MTVK_INVALID_ARGUMENT);
    union { mtvk_nv_device_info info; uint8_t bytes[MTVK_NV_DEVICE_WIRE_BYTES]; } alias;
    fixture(&alias.info);
    CHECK(mtvk_nv_device_info_encode(&alias.info, alias.bytes, sizeof(alias.bytes)) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_nv_device_info_decode(alias.bytes, sizeof(alias.bytes), &alias.info) == MTVK_INVALID_ARGUMENT);
    fixture(&info);
    info.has_transfer_queue = 2u;
    memset(encoded, 0xa5, sizeof(encoded));
    CHECK(mtvk_nv_device_info_encode(&info, encoded, sizeof(encoded)) == MTVK_INVALID_ARGUMENT);
    for (size_t i = 0; i < sizeof(encoded); ++i) CHECK(encoded[i] == 0xa5u);
    uint32_t seed = UINT32_C(0x42fa1973);
    unsigned accepted = 0u, rejected = 0u;
    for (unsigned iteration = 0; iteration < 20000u; ++iteration) {
        uint8_t mutated[MTVK_NV_DEVICE_WIRE_BYTES], reencoded[MTVK_NV_DEVICE_WIRE_BYTES];
        memcpy(mutated, golden, sizeof(mutated));
        seed = seed * UINT32_C(1664525) + UINT32_C(1013904223);
        mutated[seed % MTVK_NV_DEVICE_WIRE_BYTES] = (uint8_t)(seed >> 24u);
        memset(&decoded, 0xa5, sizeof(decoded));
        uint8_t before[sizeof(decoded)];
        memcpy(before, &decoded, sizeof(before));
        const mtvk_status status = mtvk_nv_device_info_decode(mutated, sizeof(mutated), &decoded);
        if (status == MTVK_OK) {
            CHECK(mtvk_nv_device_info_encode(&decoded, reencoded, sizeof(reencoded)) == MTVK_OK);
            CHECK(memcmp(mutated, reencoded, sizeof(mutated)) == 0);
            ++accepted;
        } else {
            CHECK(status == MTVK_INVALID_ARGUMENT || status == MTVK_INCOMPATIBLE_VERSION);
            CHECK(memcmp(before, &decoded, sizeof(before)) == 0);
            ++rejected;
        }
    }
    CHECK(accepted && rejected);
    printf("PASS discovery canonical mutation: %u accepted, %u rejected\n", accepted, rejected);
    return 0;
}

static int session_budget_alias_and_invalid_device(void) {
    mtvk_nv_device_info info;
    fixture(&info);
    mtvk_nv_session session;
    mtvk_nv_session_init(&session, 0u, &info);
    uint8_t request[sizeof(mtvk_nv_request)], response[RESPONSE_BYTES];
    const uint32_t budgets[] = {4u, MTVK_NV_DEVICE_WIRE_BYTES - 4u, MTVK_NV_DEVICE_WIRE_BYTES,
                               MTVK_NV_DEVICE_WIRE_BYTES + 4u};
    for (size_t i = 0; i < sizeof(budgets) / sizeof(budgets[0]); ++i) {
        size_t written = 0;
        CHECK(mtvk_nv_request_init(request, sizeof(request), MTVK_NV_OP_DEVICE_QUERY,
                                   0u, budgets[i], &written) == MTVK_OK);
        CHECK(mtvk_nv_session_dispatch(&session, request, sizeof(request), response,
                                       sizeof(response), &written) == (i < 2u ? MTVK_BUFFER_TOO_SMALL : MTVK_OK));
        CHECK(written == (i < 2u ? sizeof(mtvk_nv_response) : RESPONSE_BYTES));
    }
    size_t written = 123u;
    uint8_t before[sizeof(request)];
    memcpy(before, request, sizeof(before));
    CHECK(mtvk_nv_session_dispatch(&session, request, sizeof(request), request,
                                   sizeof(request), &written) == MTVK_INVALID_ARGUMENT);
    CHECK(written == 123u && memcmp(before, request, sizeof(before)) == 0);
    mtvk_nv_session_init(&session, 0u, &session.device);
    CHECK(session.device.device_id == info.device_id && session.device.va_end == info.va_end);
    CHECK(mtvk_nv_session_dispatch(&session, request, sizeof(request), &session,
                                   sizeof(session), &written) == MTVK_INVALID_ARGUMENT);
    union { size_t written; uint8_t response[RESPONSE_BYTES]; } alias;
    CHECK(mtvk_nv_session_dispatch(&session, request, sizeof(request), alias.response,
                                   sizeof(alias.response), &alias.written) == MTVK_INVALID_ARGUMENT);
    session.device.pci_device = 32u;
    CHECK(mtvk_nv_session_dispatch(&session, request, sizeof(request), response,
                                   sizeof(response), &written) == MTVK_INVALID_ARGUMENT);
    mtvk_nv_response_view view;
    CHECK(mtvk_nv_transport_validate_response(response, written, &view) == MTVK_OK);
    CHECK(view.payload_bytes == 0u && view.header.status == MTVK_INVALID_ARGUMENT);
    return 0;
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
    CHECK(view.payload_bytes == MTVK_NV_DEVICE_WIRE_BYTES);

    /* The response must already be capability-clipped: only DEVICE_QUERY is
     * backed, so every memory capability the fixture requested is cleared. */
    const mtvk_nv_device_info effective =
        mtvk_nv_effective_device_info(&info, MTVK_NV_IMPLEMENTED_OPS);
    mtvk_nv_device_info decoded;
    CHECK(mtvk_nv_device_info_decode(view.payload, view.payload_bytes, &decoded) == MTVK_OK);
    CHECK(decoded.device_id == effective.device_id && decoded.va_end == effective.va_end);
    CHECK(decoded.has_alloc_tiled == 0u && decoded.has_dma_buf == 0u);
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
            CHECK(view.payload_bytes == MTVK_NV_DEVICE_WIRE_BYTES);
            mtvk_nv_device_info decoded;
            CHECK(mtvk_nv_device_info_decode(view.payload, view.payload_bytes, &decoded) == MTVK_OK);
            CHECK(decoded.device_id == effective.device_id && decoded.va_end == effective.va_end);
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
    /* Real snapshot projection fixture. No PCI/MMIO access in host tests. */
    mtvk_diag_snapshot snapshot = {0};
    snapshot.vendor_id = MTVK_NV_VENDOR_ID; snapshot.device_id = MTVK_NV_INITIAL_DEVICE_ID;
    snapshot.class_revision = UINT32_C(0x030000a1); snapshot.pci_address = UINT32_C(0x021907);
    snapshot.phase = MTVK_DIAG_PCI_READY; snapshot.capabilities = MTVK_DIAG_CAP_PCI;
    snapshot.registry_id = 42u;
    mtvk_nv_device_info projected;
    CHECK(mtvk_nv_discovery_from_pci(&snapshot, &projected) == MTVK_OK);
    CHECK(projected.device_id == snapshot.device_id && projected.pci_bus == 2u &&
          projected.pci_device == 25u && projected.pci_function == 7u && projected.pci_revision == 0xa1u);
    CHECK(projected.vram_size_B == 0u && projected.sm == 0u && projected.chipset == 0u &&
          projected.cls_eng3d == 0u && projected.has_dma_buf == 0u && projected.device_name[0] == '\0');
    mtvk_nv_session projectedSession;
    mtvk_nv_session_init(&projectedSession, 0u, &projected);
    uint8_t queryRequest[MTVK_NV_QUERY_REQUEST_BYTES], queryResponse[MTVK_NV_QUERY_RESPONSE_BYTES];
    size_t queryWritten = 0;
    CHECK(mtvk_nv_request_init(queryRequest, sizeof(queryRequest), MTVK_NV_OP_DEVICE_QUERY,
                               0u, MTVK_NV_DEVICE_WIRE_BYTES, &queryWritten) == MTVK_OK);
    CHECK(mtvk_nv_session_dispatch(&projectedSession, queryRequest, sizeof(queryRequest),
                                   queryResponse, sizeof(queryResponse), &queryWritten) == MTVK_OK);
    mtvk_nv_response_view projectedView;
    CHECK(mtvk_nv_transport_validate_response(queryResponse, queryWritten, &projectedView) == MTVK_OK);
    mtvk_nv_device_info projectedDecoded;
    CHECK(mtvk_nv_device_info_decode(projectedView.payload, projectedView.payload_bytes, &projectedDecoded) == MTVK_OK);
    CHECK(projectedDecoded.device_id == projected.device_id && projectedDecoded.pci_device == 25u &&
          projectedView.header.backed_op_mask == MTVK_NV_IMPLEMENTED_OPS);
    const mtvk_diag_snapshot valid = snapshot;
    for (unsigned fault = 0; fault < 8u; ++fault) {
        snapshot = valid;
        switch (fault) {
        case 0: snapshot.vendor_id = 0x1002u; break;
        case 1: snapshot.device_id++; break;
        case 2: snapshot.registry_id = 0u; break;
        case 3: snapshot.phase = MTVK_DIAG_OFFLINE; break;
        case 4: snapshot.capabilities |= 2u; break;
        case 5: snapshot.class_revision = 0u; break;
        case 6: snapshot.pci_address = UINT32_C(0x020008); break;
        default: snapshot.pci_address = UINT32_C(0x022000); break;
        }
        uint8_t before[sizeof(projected)];
        memcpy(before, &projected, sizeof(before));
        CHECK(mtvk_nv_discovery_from_pci(&snapshot, &projected) == (fault < 2u ? MTVK_NOT_SUPPORTED : MTVK_INVALID_ARGUMENT));
        CHECK(memcmp(before, &projected, sizeof(before)) == 0);
    }
    CHECK(mtvk_nv_discovery_from_pci(NULL, &projected) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_nv_discovery_from_pci(&valid, NULL) == MTVK_INVALID_ARGUMENT);
    CHECK(discovery_wire_golden_and_failures() == 0);
    CHECK(session_budget_alias_and_invalid_device() == 0);
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
