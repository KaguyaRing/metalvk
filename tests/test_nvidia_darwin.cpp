// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
// Fault injection into the actual Darwin transport source. No real IOKit/GPU.
#include "nvidia/darwin.h"
#include "nvidia/init.h"
#include <IOKit/IOKitLib.h>
#include <cstdio>
#include <cstring>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
enum class Fault { None, Dictionary, Missing, Identity, Class, Open, Call, Close, Release, Oversize, Malformed };
static Fault fault;
static unsigned opens, calls, closes, releases;
static constexpr uint64_t RegistryId = 42u;
extern "C" {
CFMutableDictionaryRef IORegistryEntryIDMatching(uint64_t) { return fault == Fault::Dictionary ? nullptr : &fault; }
io_service_t IOServiceGetMatchingService(uint32_t, CFMutableDictionaryRef) { return fault == Fault::Missing ? 0u : 1u; }
kern_return_t IORegistryEntryGetRegistryEntryID(io_service_t, uint64_t *id) { *id = fault == Fault::Identity ? 43u : RegistryId; return 0; }
bool IOObjectConformsTo(io_service_t, const char *) { return fault != Fault::Class; }
uint32_t mach_task_self() { return 1u; }
kern_return_t IOServiceOpen(io_service_t, uint32_t, uint32_t type, io_connect_t *out) {
    ++opens;
    if (type != MTVK_NV_CONNECT_TYPE || fault == Fault::Open) return -1;
    *out = 2u; return 0;
}
kern_return_t IOConnectCallStructMethod(io_connect_t connection, uint32_t selector,
    const void *input, size_t inputSize, void *output, size_t *outputSize) {
    ++calls;
    if (fault == Fault::Call || connection != 2u || selector != MTVK_NV_QUERY_SELECTOR) return -2;
    mtvk_nv_device_info info;
    mtvk_nv_device_info_init(&info);
    info.device_type = MTVK_NV_DEVICE_DISCRETE; info.device_id = MTVK_NV_INITIAL_DEVICE_ID;
    mtvk_nv_session session;
    mtvk_nv_session_init(&session, 0u, &info);
    size_t written = 0;
    if (mtvk_nv_session_dispatch(&session, input, inputSize, output, *outputSize, &written) != MTVK_OK) return -3;
    *outputSize = fault == Fault::Oversize ? MTVK_NV_QUERY_RESPONSE_BYTES + 1u : written;
    if (fault == Fault::Malformed) static_cast<uint8_t *>(output)[4] = 1u;
    return 0;
}
kern_return_t IOServiceClose(io_connect_t) { ++closes; return fault == Fault::Close ? -4 : 0; }
kern_return_t IOObjectRelease(io_service_t) { ++releases; return fault == Fault::Release ? -5 : 0; }
}
int main() {
    const struct { Fault fault; mtvk_status status; mtvk_nv_darwin_stage stage; unsigned open, call, close, release; } cases[] = {
        {Fault::None, MTVK_OK, MTVK_NV_DARWIN_COMPLETE, 1u, 1u, 1u, 1u},
        {Fault::Dictionary, MTVK_OUT_OF_MEMORY, MTVK_NV_DARWIN_LOOKUP, 0u, 0u, 0u, 0u},
        {Fault::Missing, MTVK_NOT_FOUND, MTVK_NV_DARWIN_LOOKUP, 0u, 0u, 0u, 0u},
        {Fault::Identity, MTVK_INVALID_CONTEXT, MTVK_NV_DARWIN_LOOKUP, 0u, 0u, 0u, 1u},
        {Fault::Class, MTVK_INVALID_CONTEXT, MTVK_NV_DARWIN_LOOKUP, 0u, 0u, 0u, 1u},
        {Fault::Open, MTVK_INVALID_CONTEXT, MTVK_NV_DARWIN_OPEN, 1u, 0u, 0u, 1u},
        {Fault::Call, MTVK_INVALID_CONTEXT, MTVK_NV_DARWIN_CALL, 1u, 1u, 1u, 1u},
        {Fault::Close, MTVK_CLEANUP_REQUIRED, MTVK_NV_DARWIN_CLOSE, 1u, 1u, 1u, 1u},
        {Fault::Release, MTVK_CLEANUP_REQUIRED, MTVK_NV_DARWIN_RELEASE, 1u, 1u, 1u, 1u},
        {Fault::Oversize, MTVK_INVALID_ARGUMENT, MTVK_NV_DARWIN_RESPONSE, 1u, 1u, 1u, 1u},
        {Fault::Malformed, MTVK_INCOMPATIBLE_VERSION, MTVK_NV_DARWIN_RESPONSE, 1u, 1u, 1u, 1u}
    };
    for (const auto &test : cases) {
        fault = test.fault; opens = calls = closes = releases = 0u;
        uint8_t response[MTVK_NV_QUERY_RESPONSE_BYTES];
        std::memset(response, 0xa5, sizeof(response));
        const auto result = mtvk_nv_darwin_query(RegistryId, response, sizeof(response));
        CHECK(result.status == test.status && result.stage == test.stage);
        CHECK(opens == test.open && calls == test.call && closes == test.close && releases == test.release);
        if (result.status == MTVK_OK) {
            CHECK(result.response_bytes == sizeof(response));
            mtvk_nv_device_info info;
            CHECK(mtvk_nv_discovery_response_decode(response, result.response_bytes, &info) == MTVK_OK);
        } else {
            CHECK(result.response_bytes == 0u);
            for (uint8_t byte : response) CHECK(byte == 0xa5u);
        }
    }
    uint8_t response[MTVK_NV_QUERY_RESPONSE_BYTES];
    opens = calls = closes = releases = 0u;
    CHECK(mtvk_nv_darwin_query(0u, response, sizeof(response)).status == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_nv_darwin_query(RegistryId, nullptr, sizeof(response)).status == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_nv_darwin_query(RegistryId, response, sizeof(response) - 1u).status == MTVK_BUFFER_TOO_SMALL);
    CHECK(opens == 0u && calls == 0u && closes == 0u && releases == 0u);
    std::puts("PASS Darwin transport injection: cleanup, identity binding and transactional output (mock IOKit only)");
    return 0;
}
