// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
// Real IOKit client, never a loopback/default GPU substitute. No installation.
#include "nvidia/init.h"
#include "nvidia/darwin.h"
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
int query(uint64_t id) {
    uint8_t response[MTVK_NV_QUERY_RESPONSE_BYTES];
    const mtvk_nv_darwin_result queried = mtvk_nv_darwin_query(id, response, sizeof(response));
    if (queried.status != MTVK_OK) {
        std::fprintf(stderr, "NVIDIA query refused: %s stage=%u native=0x%08x\n",
            mtvk_status_name(queried.status), static_cast<unsigned>(queried.stage), queried.native_error);
        return queried.status == MTVK_NOT_FOUND ? Missing : Failure;
    }
    mtvk_nv_device_info info;
    if (mtvk_nv_discovery_response_decode(response, queried.response_bytes, &info) != MTVK_OK) {
        std::fputs("NVIDIA discovery response rejected; no valid result published.\n", stderr);
        return Failure;
    }
    std::printf("PASS NVIDIA discovery IPC registry-id=0x%" PRIx64
        " PCI 10de:%04x bus=%u device=%u function=%u revision=%02x transport=%u\n",
        id, static_cast<unsigned>(info.device_id), static_cast<unsigned>(info.pci_bus),
        static_cast<unsigned>(info.pci_device), static_cast<unsigned>(info.pci_function),
        static_cast<unsigned>(info.pci_revision), MTVK_NV_TRANSPORT_VERSION);
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
    return query(id);
}
