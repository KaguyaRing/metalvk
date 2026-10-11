// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
/* Framing/standard conformance only: NOT NVIDIA hardware execution evidence.
 * This exercises the shared codec that both the user-space nvkmd adapter and
 * the Darwin kernel endpoint compile, so a green run proves the two sides agree
 * on the wire standard, never that a GPU executed anything. */
#include "nvidia/init.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
#define PAYLOAD_BYTES 16u

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void write_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value & 0xffu);
    bytes[1] = (uint8_t)((value >> 8) & 0xffu);
    bytes[2] = (uint8_t)((value >> 16) & 0xffu);
    bytes[3] = (uint8_t)((value >> 24) & 0xffu);
}

/* Independent oracle for the op-class table; deliberately not derived from the
 * implementation so a misclassified op is caught. */
static const mtvk_nv_op_class expected_classes[MTVK_NV_OP_COUNT] = {
    MTVK_NV_CLASS_DISCOVERY, MTVK_NV_CLASS_DISCOVERY, MTVK_NV_CLASS_REPLACED,
    MTVK_NV_CLASS_DISCOVERY, MTVK_NV_CLASS_DISCOVERY, MTVK_NV_CLASS_DISCOVERY,
    MTVK_NV_CLASS_MEMORY, MTVK_NV_CLASS_MEMORY, MTVK_NV_CLASS_REPLACED,
    MTVK_NV_CLASS_MEMORY, MTVK_NV_CLASS_MEMORY, MTVK_NV_CLASS_MEMORY,
    MTVK_NV_CLASS_MEMORY, MTVK_NV_CLASS_MEMORY, MTVK_NV_CLASS_MEMORY,
    MTVK_NV_CLASS_REPLACED, MTVK_NV_CLASS_ADDRESS, MTVK_NV_CLASS_ADDRESS,
    MTVK_NV_CLASS_ADDRESS, MTVK_NV_CLASS_ADDRESS, MTVK_NV_CLASS_EXECUTION,
    MTVK_NV_CLASS_EXECUTION, MTVK_NV_CLASS_EXECUTION, MTVK_NV_CLASS_EXECUTION,
    MTVK_NV_CLASS_EXECUTION, MTVK_NV_CLASS_SYNC, MTVK_NV_CLASS_SYNC,
    MTVK_NV_CLASS_SYNC,
};

static int catalog_is_consistent(void) {
    for (uint32_t op = 0; op < (uint32_t)MTVK_NV_OP_COUNT; ++op) {
        const char *name = mtvk_nv_op_name(op);
        CHECK(name != NULL && name[0] != '\0');
        CHECK(mtvk_nv_op_class_of(op) == expected_classes[op]);
        for (uint32_t other = op + 1u; other < (uint32_t)MTVK_NV_OP_COUNT; ++other)
            CHECK(strcmp(name, mtvk_nv_op_name(other)) != 0);
    }
    CHECK(mtvk_nv_op_name((uint32_t)MTVK_NV_OP_COUNT) == NULL);
    CHECK(mtvk_nv_op_class_of((uint32_t)MTVK_NV_OP_COUNT) == MTVK_NV_CLASS_NONE);
    CHECK(mtvk_nv_op_name(UINT32_MAX) == NULL);
    CHECK(mtvk_nv_op_class_of(UINT32_MAX) == MTVK_NV_CLASS_NONE);
    return 0;
}

static int round_trip(void) {
    uint8_t bytes[sizeof(mtvk_nv_request) + PAYLOAD_BYTES];
    memset(bytes, 0, sizeof(bytes));
    size_t written = 0;
    CHECK(mtvk_nv_request_init(bytes, sizeof(bytes), (uint32_t)MTVK_NV_OP_CONTEXT_EXEC,
                               PAYLOAD_BYTES, 0u, &written) == MTVK_OK);
    CHECK(written == sizeof(bytes));
    CHECK(read_le32(bytes) == sizeof(mtvk_nv_request));
    CHECK(read_le32(bytes + 4) == MTVK_NV_TRANSPORT_VERSION);
    CHECK(read_le32(bytes + 8) == (uint32_t)MTVK_NV_OP_CONTEXT_EXEC);
    CHECK(read_le32(bytes + 12) == 0u);
    CHECK(read_le32(bytes + 16) == PAYLOAD_BYTES);
    for (unsigned i = 0; i < PAYLOAD_BYTES; ++i) bytes[sizeof(mtvk_nv_request) + i] = (uint8_t)(0xa0u + i);

    mtvk_nv_request_view view;
    memset(&view, 0, sizeof(view));
    CHECK(mtvk_nv_transport_validate(bytes, sizeof(bytes), &view) == MTVK_OK);
    CHECK(view.header.version == MTVK_NV_TRANSPORT_VERSION);
    CHECK(view.header.op == (uint32_t)MTVK_NV_OP_CONTEXT_EXEC);
    CHECK(view.payload_bytes == PAYLOAD_BYTES);
    CHECK(view.payload == bytes + sizeof(mtvk_nv_request));
    CHECK(view.raw == bytes && view.raw_bytes == sizeof(bytes));
    for (unsigned i = 0; i < PAYLOAD_BYTES; ++i) CHECK(view.payload[i] == (uint8_t)(0xa0u + i));
    return 0;
}

static int init_rejections(void) {
    uint8_t bytes[sizeof(mtvk_nv_request) + PAYLOAD_BYTES];
    size_t written = 0;
    const size_t capacity = sizeof(bytes);
    CHECK(mtvk_nv_request_init(NULL, capacity, 0u, 0u, 0u, &written) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_nv_request_init(bytes, capacity, 0u, 0u, 0u, NULL) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_nv_request_init(bytes, capacity, (uint32_t)MTVK_NV_OP_COUNT, 0u, 0u, &written) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_nv_request_init(bytes, capacity, 0u, PAYLOAD_BYTES + 1u, 0u, &written) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_nv_request_init(bytes, capacity, 0u, 0u, MTVK_NV_TRANSPORT_MAX_RESPONSE_BYTES + 4u, &written) == MTVK_LIMIT_EXCEEDED);
    CHECK(mtvk_nv_request_init(bytes, capacity, 0u, MTVK_NV_TRANSPORT_MAX_REQUEST_BYTES + 4u, 0u, &written) == MTVK_LIMIT_EXCEEDED);
    CHECK(mtvk_nv_request_init(bytes, capacity - 1u, 0u, PAYLOAD_BYTES, 0u, &written) == MTVK_BUFFER_TOO_SMALL);
    return 0;
}

static int validate_rejections(void) {
    uint8_t bytes[sizeof(mtvk_nv_request) + PAYLOAD_BYTES];
    memset(bytes, 0, sizeof(bytes));
    mtvk_nv_request_view view;
    const size_t size = sizeof(bytes);
    const mtvk_nv_request_view sentinel = { .payload_bytes = 0x1234u };
    for (size_t partial = 0; partial < sizeof(mtvk_nv_request); ++partial) {
        view = sentinel;
        CHECK(mtvk_nv_transport_validate(bytes, partial, &view) == MTVK_BUFFER_TOO_SMALL);
        CHECK(view.payload_bytes == sentinel.payload_bytes);
    }
    CHECK(mtvk_nv_transport_validate(NULL, size, &view) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_nv_transport_validate(bytes, size, NULL) == MTVK_INVALID_ARGUMENT);

    size_t written = 0;
    CHECK(mtvk_nv_request_init(bytes, size, (uint32_t)MTVK_NV_OP_VA_BIND, PAYLOAD_BYTES, 0u, &written) == MTVK_OK);
    view = sentinel;
    CHECK(mtvk_nv_transport_validate(bytes, size + 1u, &view) == MTVK_INVALID_ARGUMENT);
    CHECK(view.payload_bytes == sentinel.payload_bytes);
    CHECK(mtvk_nv_transport_validate(bytes, size - 1u, &view) == MTVK_INVALID_ARGUMENT);

    struct { uint32_t offset; uint32_t value; mtvk_status expected; } cases[] = {
        {0u, 23u, MTVK_INVALID_ARGUMENT},                                    /* header_bytes */
        {0u, 25u, MTVK_INVALID_ARGUMENT},
        {4u, MTVK_NV_TRANSPORT_VERSION + 1u, MTVK_INCOMPATIBLE_VERSION},     /* version */
        {4u, 1u, MTVK_INCOMPATIBLE_VERSION},                                /* old struct payload */
        {4u, 0u, MTVK_INCOMPATIBLE_VERSION},
        {8u, (uint32_t)MTVK_NV_OP_COUNT, MTVK_INVALID_ARGUMENT},             /* op */
        {8u, UINT32_MAX, MTVK_INVALID_ARGUMENT},
        {12u, 1u, MTVK_INVALID_ARGUMENT},                                    /* reserved */
        {16u, PAYLOAD_BYTES + 4u, MTVK_INVALID_ARGUMENT},                    /* request_bytes */
        {16u, 3u, MTVK_INVALID_ARGUMENT},
        {16u, MTVK_NV_TRANSPORT_MAX_REQUEST_BYTES + 4u, MTVK_LIMIT_EXCEEDED},
        {20u, MTVK_NV_TRANSPORT_MAX_RESPONSE_BYTES + 4u, MTVK_LIMIT_EXCEEDED}, /* response_bytes */
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        uint8_t mutated[sizeof(bytes)];
        memcpy(mutated, bytes, sizeof(bytes));
        write_le32(mutated + cases[i].offset, cases[i].value);
        view = sentinel;
        CHECK(mtvk_nv_transport_validate(mutated, size, &view) == cases[i].expected);
        CHECK(view.payload_bytes == sentinel.payload_bytes);
    }
    return 0;
}

/* Independent oracle for header acceptability, written from the standard text
 * rather than derived from the codec under test. */
static bool header_is_acceptable(const uint8_t *bytes, size_t size) {
    if (size < sizeof(mtvk_nv_request)) return false;
    const uint32_t header_bytes = read_le32(bytes);
    const uint32_t version = read_le32(bytes + 4);
    const uint32_t op = read_le32(bytes + 8);
    const uint32_t reserved = read_le32(bytes + 12);
    const uint32_t request_bytes = read_le32(bytes + 16);
    const uint32_t response_bytes = read_le32(bytes + 20);
    return header_bytes == (uint32_t)sizeof(mtvk_nv_request) &&
        version == MTVK_NV_TRANSPORT_VERSION &&
        op < (uint32_t)MTVK_NV_OP_COUNT &&
        reserved == 0u &&
        request_bytes <= MTVK_NV_TRANSPORT_MAX_REQUEST_BYTES &&
        response_bytes <= MTVK_NV_TRANSPORT_MAX_RESPONSE_BYTES &&
        request_bytes % 4u == 0u &&
        size == sizeof(mtvk_nv_request) + (size_t)request_bytes;
}

/* Deterministic byte mutation over one header/payload byte per iteration. The
 * codec must accept exactly the messages the oracle accepts, and every accepted
 * message must decode to a self-consistent view. */
static int mutation_invariant(void) {
    uint8_t valid[sizeof(mtvk_nv_request) + PAYLOAD_BYTES];
    size_t written = 0;
    CHECK(mtvk_nv_request_init(valid, sizeof(valid), (uint32_t)MTVK_NV_OP_MEMORY_MAP,
                               PAYLOAD_BYTES, 8u, &written) == MTVK_OK);
    uint32_t seed = UINT32_C(0x9e3779b9);
    unsigned accepted = 0, rejected = 0;
    for (unsigned iteration = 0; iteration < 20000u; ++iteration) {
        uint8_t mutated[sizeof(valid)];
        memcpy(mutated, valid, sizeof(valid));
        seed = seed * UINT32_C(1664525) + UINT32_C(1013904223);
        const size_t index = (size_t)(seed % (uint32_t)sizeof(mutated));
        seed = seed * UINT32_C(1664525) + UINT32_C(1013904223);
        mutated[index] = (uint8_t)(seed >> 24);
        mtvk_nv_request_view view;
        memset(&view, 0, sizeof(view));
        const mtvk_status result = mtvk_nv_transport_validate(mutated, sizeof(mutated), &view);
        CHECK((result == MTVK_OK) == header_is_acceptable(mutated, sizeof(mutated)));
        if (result == MTVK_OK) {
            CHECK(view.payload_bytes == read_le32(mutated + 16));
            CHECK(view.payload == mutated + sizeof(mtvk_nv_request));
            ++accepted;
        } else {
            ++rejected;
        }
    }
    CHECK(accepted > 0u && rejected > 0u);
    printf("PASS transport mutation invariant: %u accepted by oracle, %u rejected\n", accepted, rejected);
    return 0;
}

int main(void) {
    CHECK(sizeof(mtvk_nv_request) == 24u);
    CHECK(catalog_is_consistent() == 0);
    CHECK(round_trip() == 0);
    CHECK(init_rejections() == 0);
    CHECK(validate_rejections() == 0);
    CHECK(mutation_invariant() == 0);
    printf("PASS NVIDIA transport standard: framing, catalog and bounded mutation (no hardware)\n");
    return 0;
}
