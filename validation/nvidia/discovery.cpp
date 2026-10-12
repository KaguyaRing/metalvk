// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
// Real IOKit client, never a loopback/default GPU substitute. No installation.
#include "nvidia/init.h"
#include <IOKit/IOKitLib.h>
#include <cstdio>
#include <cstring>
#include <cinttypes>

namespace {
constexpr unsigned ServiceLimit = 32u;
constexpr int Failure = 1, Missing = 2, Usage = 64;
bool registryId(const char *text, uint64_t *out) {
    size_t length = 0;
    if (text) while (length < 19u && text[length]) ++length;
    if (!text || length < 3u || length > 18u ||
        text[0] != '0' || text[1] != 'x') return false;
    uint64_t value = 0;
    for (const char *p = text + 2; *p; ++p) {
        unsigned digit;
        if (*p >= '0' && *p <= '9') digit = static_cast<unsigned>(*p - '0');
        else if (*p >= 'a' && *p <= 'f') digit = static_cast<unsigned>(*p - 'a') + 10u;
        else if (*p >= 'A' && *p <= 'F') digit = static_cast<unsigned>(*p - 'A') + 10u;
        else return false;
        value = (value << 4u) | digit;
    }
    if (!value) return false;
    *out = value;
    return true;
}
int listServices() {
    auto *matching = IOServiceMatching(MTVK_DIAG_SERVICE_CLASS);
    if (!matching) return Failure;
    io_iterator_t iterator = IO_OBJECT_NULL;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator) != KERN_SUCCESS)
        return Failure; // matching dictionary consumed by IOKit
    unsigned count = 0;
    int result = Missing;
    while (io_service_t service = IOIteratorNext(iterator)) {
        uint64_t id = 0;
        const bool valid = IORegistryEntryGetRegistryEntryID(service, &id) == KERN_SUCCESS && id;
        const bool released = IOObjectRelease(service) == KERN_SUCCESS;
        if (!valid || !released || count == ServiceLimit) { result = Failure; break; }
        std::printf("NVIDIA diagnostic service registry-id=0x%" PRIx64 "\n", id);
        ++count;
        result = 0;
    }
    if (IOObjectRelease(iterator) != KERN_SUCCESS) result = Failure;
    std::printf("Service count: %u (listing only; no loading or GPU execution)\n", count);
    return result;
}
int query(io_service_t service, uint64_t id) {
    io_connect_t connection = IO_OBJECT_NULL;
    const kern_return_t opened = IOServiceOpen(service, mach_task_self(), MTVK_NV_CONNECT_TYPE, &connection);
    if (opened != KERN_SUCCESS) {
        std::fprintf(stderr, "NVIDIA UserClient open failed: 0x%08x\n", static_cast<unsigned>(opened));
        return Failure;
    }
    uint8_t request[MTVK_NV_QUERY_REQUEST_BYTES], response[MTVK_NV_QUERY_RESPONSE_BYTES] {};
    size_t requestSize = 0, responseSize = sizeof(response);
    const mtvk_status prepared = mtvk_nv_request_init(request, sizeof(request), MTVK_NV_OP_DEVICE_QUERY,
        0u, MTVK_NV_DEVICE_WIRE_BYTES, &requestSize);
    kern_return_t called = kIOReturnBadArgument;
    if (prepared == MTVK_OK)
        called = IOConnectCallStructMethod(connection, MTVK_NV_QUERY_SELECTOR,
            request, requestSize, response, &responseSize);
    const kern_return_t closed = IOServiceClose(connection);
    if (called != KERN_SUCCESS || closed != KERN_SUCCESS || responseSize > sizeof(response)) {
        std::fprintf(stderr, "NVIDIA query/close failed: 0x%08x / 0x%08x\n",
            static_cast<unsigned>(called), static_cast<unsigned>(closed));
        return Failure;
    }
    mtvk_nv_response_view view;
    mtvk_nv_device_info info;
    if (mtvk_nv_transport_validate_response(response, responseSize, &view) != MTVK_OK ||
        view.header.op != MTVK_NV_OP_DEVICE_QUERY || view.header.status != MTVK_OK ||
        view.header.backed_op_mask != MTVK_NV_IMPLEMENTED_OPS ||
        mtvk_nv_device_info_decode(view.payload, view.payload_bytes, &info) != MTVK_OK ||
        info.device_id != MTVK_NV_INITIAL_DEVICE_ID || info.has_alloc_tiled || info.has_map_fixed ||
        info.has_overmap || info.has_compression || info.has_dma_buf) {
        std::fputs("NVIDIA discovery response rejected; no valid result published.\n", stderr);
        return Failure;
    }
    std::printf("PASS NVIDIA discovery IPC registry-id=0x%" PRIx64
        " PCI 10de:%04x bus=%u device=%u function=%u revision=%02x transport=%u\n",
        id, static_cast<unsigned>(info.device_id), static_cast<unsigned>(info.pci_bus),
        static_cast<unsigned>(info.pci_device), static_cast<unsigned>(info.pci_function),
        static_cast<unsigned>(info.pci_revision), view.header.version);
    std::puts("Cached PCI snapshot only; RM fields unknown. No GPU execution, Metal or display claim.");
    return 0;
}
}
int main(int argc, char **argv) {
    if (argc == 2 && std::strcmp(argv[1], "--list-services") == 0) return listServices();
    uint64_t id = 0;
    if (argc != 3 || std::strcmp(argv[1], "--registry-id") != 0 || !registryId(argv[2], &id)) {
        std::fputs("Usage: metalvk-nvidia-discovery --list-services | --registry-id 0xHEX\n", stderr);
        return Usage;
    }
    auto *matching = IORegistryEntryIDMatching(id);
    if (!matching) return Failure;
    io_service_t service = IOServiceGetMatchingService(kIOMainPortDefault, matching);
    if (!service) { std::fputs("Selected diagnostic service not found.\n", stderr); return Missing; }
    uint64_t actualId = 0;
    int result = Failure;
    if (IOObjectConformsTo(service, MTVK_DIAG_SERVICE_CLASS) &&
        IORegistryEntryGetRegistryEntryID(service, &actualId) == KERN_SUCCESS && actualId == id)
        result = query(service, id);
    else std::fputs("Selected registry entry is not the expected diagnostic service.\n", stderr);
    if (IOObjectRelease(service) != KERN_SUCCESS) result = Failure;
    return result;
}
