// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#import "Diagnostics.hpp"
#import <IOKit/IOKitLib.h>
#include "ipc/init.h"

@implementation MetalVKReport
@end
namespace {
constexpr NSUInteger MaxDevices = 64;
class Object final {
    io_object_t value_;
public:
    explicit Object(io_object_t value) : value_(value) {}
    ~Object() { if (value_) IOObjectRelease(value_); }
    Object(const Object &) = delete;
    Object &operator=(const Object &) = delete;
    io_object_t get() const { return value_; }
};
class Connection final {
    io_connect_t value_ = IO_OBJECT_NULL;
public:
    kern_return_t open(io_service_t service) {
        return IOServiceOpen(service, mach_task_self(), MTVK_DIAG_CONNECT_TYPE, &value_);
    }
    ~Connection() { if (value_) IOServiceClose(value_); }
    io_connect_t get() const { return value_; }
};
NSString *failure(NSString *action, kern_return_t code) {
    const char *message = mach_error_string(code);
    return [NSString stringWithFormat:@"%@: 0x%08x (%s)\n", action,
            (unsigned int)code, message ? message : "Unknown IOKit error"];
}
}
MetalVKReport *MetalVKReadDiagnostics(void) {
    MetalVKReport *report = [MetalVKReport new];
    NSMutableString *text = [NSMutableString stringWithString:
        @"MetalVK diagnostics — protocol 1\n\n"
         "Current driver capability: read-only PCI diagnostics.\n"
         "GPU execution, Metal acceleration and display control are not implemented yet.\n\n"];
    io_iterator_t iterator = IO_OBJECT_NULL;
    CFMutableDictionaryRef matching = IOServiceMatching(MTVK_DIAG_SERVICE_CLASS);
    if (!matching) {
        report.failed = YES; report.text = @"Could not allocate the IOKit matching dictionary.";
        return report;
    }
    const kern_return_t result = IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator);
    Object iteratorOwner(iterator);
    if (result != KERN_SUCCESS) {
        [text appendString:failure(@"Driver enumeration failed", result)];
        report.failed = YES;
    } else {
        for (NSUInteger count = 0; ; ++count) {
            Object service(IOIteratorNext(iterator));
            if (!service.get()) break;
            if (count >= MaxDevices) {
                [text appendString:@"Device enumeration limit exceeded; refresh after checking IOKit state.\n"];
                report.failed = YES; break;
            }
            Connection connection;
            kern_return_t code = connection.open(service.get());
            if (code != KERN_SUCCESS) {
                [text appendString:failure(@"Driver connection denied or unavailable", code)];
                report.failed = YES; continue;
            }
            uint8_t request[MTVK_DIAG_REQUEST_SIZE], response[MTVK_DIAG_RESPONSE_SIZE];
            if (mtvk_diag_make_request(request, sizeof(request)) != MTVK_OK) {
                report.failed = YES; [text appendString:@"Request encoding failed.\n"]; break;
            }
            size_t size = sizeof(response);
            code = IOConnectCallStructMethod(connection.get(), MTVK_DIAG_SELECTOR,
                request, sizeof(request), response, &size);
            if (code != KERN_SUCCESS) {
                [text appendString:failure(@"Diagnostic request failed", code)];
                report.failed = YES; continue;
            }
            mtvk_diag_snapshot s {};
            const mtvk_status status = mtvk_diag_decode(response, size, &s);
            if (status != MTVK_OK) {
                [text appendFormat:@"Driver protocol rejected: %s. Update App and kext together.\n",
                    mtvk_status_name(status)];
                report.failed = YES; continue;
            }
            ++report.devices;
            [text appendFormat:@"Device %lu: %04x:%04x  subsystem %04x:%04x\n"
                 "PCI %02x:%02x.%x  class/revision %08x\n"
                 "Registry ID: %llu\nState: %@\n",
                 (unsigned long)report.devices, s.vendor_id, s.device_id,
                 s.subsystem_vendor_id, s.subsystem_device_id,
                 (s.pci_address >> 16) & 0xffu, (s.pci_address >> 8) & 0x1fu,
                 s.pci_address & 7u, s.class_revision, (unsigned long long)s.registry_id,
                 s.phase == MTVK_DIAG_PCI_READY ? @"PCI snapshot available" : @"Offline"];
            for (size_t i = 0; i < MTVK_DIAG_BAR_COUNT; ++i) {
                [text appendFormat:@"BAR register 0x%02x: %@, %llu bytes\n",
                    s.bars[i].config_register,
                    s.bars[i].flags == MTVK_DIAG_BAR_PRESENT ? @"resource present" : @"no independent resource",
                    (unsigned long long)s.bars[i].length];
            }
            [text appendString:@"\n"];
        }
        if (!IOIteratorIsValid(iterator)) {
            report.failed = YES; [text appendString:@"IOKit registry changed while enumerating; refresh.\n"];
        }
        if (!report.devices && !report.failed)
            [text appendString:@"No MetalVK diagnostic service found.\n"
                "The current kext matches NVIDIA 10DE:28E0 only.\n"
                "Check the installed kext, boot approval and device match before retrying.\n"];
    }
    report.text = text;
    return report;
}
