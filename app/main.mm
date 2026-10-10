// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import "Diagnostics.hpp"
#include <stdio.h>
#include <string.h>

@interface MetalVKController : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) NSTextView *details;
@property(nonatomic, strong) NSTextField *status;
@property(nonatomic, strong) NSButton *refreshButton;
@property(nonatomic, strong) NSButton *exportButton;
@end
@implementation MetalVKController
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 760, 520)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                  NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"MetalVK — Driver diagnostics";
    self.window.minSize = NSMakeSize(600, 380);
    self.window.releasedWhenClosed = NO;
    NSView *content = self.window.contentView;
    NSTextField *title = [NSTextField labelWithString:@"Driver diagnostics"];
    title.font = [NSFont systemFontOfSize:24 weight:NSFontWeightSemibold];
    self.status = [NSTextField wrappingLabelWithString:@"Checking driver…"];
    self.status.textColor = NSColor.secondaryLabelColor;
    self.refreshButton = [NSButton buttonWithTitle:@"Refresh" target:self action:@selector(refresh:)];
    self.refreshButton.keyEquivalent = @"r";
    self.exportButton = [NSButton buttonWithTitle:@"Export report…" target:self action:@selector(exportReport:)];
    self.exportButton.enabled = NO;
    NSScrollView *scroll = [NSScrollView new];
    scroll.hasVerticalScroller = YES;
    scroll.borderType = NSBezelBorder;
    self.details = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 680, 360)];
    self.details.editable = NO;
    self.details.selectable = YES;
    self.details.font = [NSFont monospacedSystemFontOfSize:13 weight:NSFontWeightRegular];
    self.details.textColor = NSColor.textColor;
    self.details.backgroundColor = NSColor.textBackgroundColor;
    self.details.autoresizingMask = NSViewWidthSizable;
    self.details.textContainer.widthTracksTextView = YES;
    self.details.textContainerInset = NSMakeSize(12, 12);
    self.details.accessibilityLabel = @"Driver diagnostic report";
    scroll.documentView = self.details;
    NSStackView *buttons = [NSStackView stackViewWithViews:@[self.refreshButton, self.exportButton]];
    buttons.spacing = 12;
    for (NSView *view in @[title, self.status, buttons, scroll]) {
        view.translatesAutoresizingMaskIntoConstraints = NO;
        [content addSubview:view];
    }
    [NSLayoutConstraint activateConstraints:@[
        [title.topAnchor constraintEqualToAnchor:content.topAnchor constant:24],
        [title.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:24],
        [self.status.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:8],
        [self.status.leadingAnchor constraintEqualToAnchor:title.leadingAnchor],
        [self.status.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-24],
        [buttons.topAnchor constraintEqualToAnchor:self.status.bottomAnchor constant:16],
        [buttons.leadingAnchor constraintEqualToAnchor:title.leadingAnchor],
        [scroll.topAnchor constraintEqualToAnchor:buttons.bottomAnchor constant:16],
        [scroll.leadingAnchor constraintEqualToAnchor:title.leadingAnchor],
        [scroll.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-24],
        [scroll.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-24]
    ]];
    NSMenu *menu = [NSMenu new];
    NSMenuItem *root = [NSMenuItem new];
    NSMenu *application = [NSMenu new];
    [application addItemWithTitle:@"Quit MetalVK" action:@selector(terminate:) keyEquivalent:@"q"];
    root.submenu = application;
    [menu addItem:root];
    NSApp.mainMenu = menu;
    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    [self refresh:nil];
}
- (void)refresh:(id)sender {
    (void)sender;
    if (!self.refreshButton.enabled) return;
    self.refreshButton.enabled = NO;
    self.exportButton.enabled = NO;
    self.status.stringValue = @"Reading the driver's cached PCI snapshot…";
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        @autoreleasepool {
            MetalVKReport *report = MetalVKReadDiagnostics();
            dispatch_async(dispatch_get_main_queue(), ^{
                self.details.string = report.text;
                self.status.stringValue = report.failed ? @"Diagnostic request failed — see the report." :
                    report.devices ? @"Driver connected. Read-only diagnostics; GPU acceleration is not available yet." :
                    @"Driver not detected on this machine.";
                self.refreshButton.enabled = YES;
                self.exportButton.enabled = YES;
            });
        }
    });
}
- (void)exportReport:(id)sender {
    (void)sender;
    NSSavePanel *panel = [NSSavePanel savePanel];
    panel.allowedContentTypes = @[UTTypePlainText];
    panel.nameFieldStringValue = @"MetalVK-diagnostics.txt";
    NSString *report = [self.details.string copy];
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response) {
        if (response != NSModalResponseOK || !panel.URL) return;
        NSError *error = nil;
        if (![report writeToURL:panel.URL atomically:YES encoding:NSUTF8StringEncoding error:&error]) {
            NSAlert *alert = [NSAlert alertWithError:error];
            [alert beginSheetModalForWindow:self.window completionHandler:nil];
        }
    }];
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)application {
    (void)application;
    return YES;
}
@end
int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (argc == 2 && strcmp(argv[1], "--diagnose") == 0) {
            MetalVKReport *report = MetalVKReadDiagnostics();
            fputs(report.text.UTF8String, stdout);
            return report.failed ? 1 : report.devices ? 0 : 2;
        }
        if (argc != 1) {
            fputs("Usage: MetalVK [--diagnose]\nExit codes: 0 connected, 1 error, 2 no device\n", stderr);
            return 64;
        }
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        MetalVKController *controller = [MetalVKController new];
        NSApp.delegate = controller;
        [NSApp run];
    }
    return 0;
}
