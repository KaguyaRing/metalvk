// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
// Test-only declarations. Never used for native builds or shipped libraries.
#ifndef MTVK_TEST_IOKIT_H
#define MTVK_TEST_IOKIT_H
#include <stddef.h>
#include <stdint.h>
typedef int kern_return_t;
typedef uint32_t io_service_t;
typedef uint32_t io_connect_t;
typedef void *CFMutableDictionaryRef;
#define IO_OBJECT_NULL 0u
#define KERN_SUCCESS 0
#define kIOMainPortDefault 0u
extern "C" {
CFMutableDictionaryRef IORegistryEntryIDMatching(uint64_t id);
io_service_t IOServiceGetMatchingService(uint32_t port, CFMutableDictionaryRef dictionary);
kern_return_t IORegistryEntryGetRegistryEntryID(io_service_t service, uint64_t *id);
bool IOObjectConformsTo(io_service_t service, const char *name);
uint32_t mach_task_self(void);
kern_return_t IOServiceOpen(io_service_t service, uint32_t task, uint32_t type, io_connect_t *out);
kern_return_t IOConnectCallStructMethod(io_connect_t connection, uint32_t selector,
    const void *input, size_t inputSize, void *output, size_t *outputSize);
kern_return_t IOServiceClose(io_connect_t connection);
kern_return_t IOObjectRelease(io_service_t service);
}
#endif
