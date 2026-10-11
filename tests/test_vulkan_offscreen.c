// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "vk/init.h"
#include "input.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
enum { Width = 7, Height = 5, PixelBytes = Width * Height * MTVK_VK_OFFSCREEN_CHANNELS,
    TailBytes = 16, TargetLimit = MTVK_VK_OFFSCREEN_MAX_TARGETS };
static bool unchanged(const uint8_t *p, size_t size) {
    for (size_t i = 0; i < size; ++i) if (p[i] != 0xa5) return false;
    return true;
}
int main(int argc, char **argv) {
    mtvk_vk_offscreen *target = NULL;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    const VkClearColorValue red = {{1.0f, 0.0f, 0.0f, 1.0f}};
    uint8_t bytes[PixelBytes + TailBytes]; memset(bytes, 0xa5, sizeof(bytes));
    CHECK(mtvk_vk_offscreen_open(NULL, Width, Height, &target) == VK_ERROR_INITIALIZATION_FAILED && !target);
    CHECK(mtvk_vk_offscreen_open(NULL, Width, Height, NULL) == VK_ERROR_INITIALIZATION_FAILED);
    CHECK(mtvk_vk_offscreen_begin(NULL, &red, &commands) == VK_ERROR_INITIALIZATION_FAILED);
    CHECK(mtvk_vk_offscreen_submit(NULL) == VK_ERROR_INITIALIZATION_FAILED);
    CHECK(mtvk_vk_offscreen_wait(NULL, 0) == VK_ERROR_INITIALIZATION_FAILED);
    CHECK(mtvk_vk_offscreen_copy(NULL, bytes, sizeof(bytes)) == VK_ERROR_INITIALIZATION_FAILED);
    CHECK(unchanged(bytes, sizeof(bytes)));
    CHECK(mtvk_vk_offscreen_drain(NULL) == VK_ERROR_INITIALIZATION_FAILED);
    CHECK(mtvk_vk_offscreen_close(NULL) == VK_SUCCESS);
    CHECK(mtvk_vk_offscreen_render_pass(NULL) == VK_NULL_HANDLE);
    if (argc == 1) { puts("PASS offscreen null/input contract (no GPU execution)"); return 0; }
    if (argc != 2) return 64;
    uint32_t vendor = 0;
    CHECK(mtvk_probe_parse_vendor(argv[1], &vendor));
    mtvk_vk_context *context = NULL;
    CHECK(mtvk_vk_open_for_queues(vendor, true, VK_QUEUE_GRAPHICS_BIT, &context) == VK_SUCCESS);
    CHECK(mtvk_vk_offscreen_open(context, 0, Height, &target) == VK_ERROR_INITIALIZATION_FAILED);
    CHECK(mtvk_vk_offscreen_open(context, MTVK_VK_OFFSCREEN_MAX_EXTENT + 1, Height, &target) == VK_ERROR_INITIALIZATION_FAILED);
    CHECK(mtvk_vk_offscreen_open(context, Width, Height, &target) == VK_SUCCESS);
    CHECK(mtvk_vk_close(context) == VK_NOT_READY);
    CHECK(mtvk_vk_offscreen_copy(target, bytes, sizeof(bytes)) == VK_NOT_READY && unchanged(bytes, sizeof(bytes)));
    CHECK(mtvk_vk_offscreen_wait(target, 0) == VK_NOT_READY);
    CHECK(mtvk_vk_offscreen_submit(target) == VK_NOT_READY);
    VkClearColorValue invalid = red; invalid.float32[0] = NAN;
    CHECK(mtvk_vk_offscreen_begin(target, &invalid, &commands) == VK_ERROR_INITIALIZATION_FAILED);
    CHECK(mtvk_vk_offscreen_begin(target, &red, &commands) == VK_SUCCESS && commands);
    CHECK(mtvk_vk_offscreen_begin(target, &red, &commands) == VK_NOT_READY && !commands);
    CHECK(mtvk_vk_offscreen_copy(target, bytes, sizeof(bytes)) == VK_NOT_READY && unchanged(bytes, sizeof(bytes)));
    CHECK(mtvk_vk_offscreen_submit(target) == VK_SUCCESS); // Real GPU render-pass clear, no shaders or CPU fallback.
    CHECK(mtvk_vk_offscreen_close(target) == VK_NOT_READY);
    CHECK(mtvk_vk_offscreen_wait(target, MTVK_VK_OFFSCREEN_MAX_WAIT_NS + 1) == VK_ERROR_INITIALIZATION_FAILED);
    CHECK(mtvk_vk_offscreen_begin(target, &red, &commands) == VK_NOT_READY);
    VkResult polled = mtvk_vk_offscreen_wait(target, 0);
    CHECK(polled == VK_SUCCESS || polled == VK_TIMEOUT);
    CHECK(mtvk_vk_offscreen_wait(target, MTVK_VK_OFFSCREEN_MAX_WAIT_NS) == VK_SUCCESS);
    CHECK(mtvk_vk_offscreen_copy(target, bytes, PixelBytes - 1) == VK_ERROR_INITIALIZATION_FAILED);
    CHECK(unchanged(bytes, sizeof(bytes)));
    CHECK(mtvk_vk_offscreen_copy(target, bytes, sizeof(bytes)) == VK_SUCCESS);
    for (size_t i = 0; i < PixelBytes; i += MTVK_VK_OFFSCREEN_CHANNELS)
        CHECK(bytes[i] == 255 && bytes[i + 1] == 0 && bytes[i + 2] == 0 && bytes[i + 3] == 255);
    CHECK(unchanged(bytes + PixelBytes, TailBytes));
    CHECK(mtvk_vk_offscreen_close(target) == VK_SUCCESS);
    mtvk_vk_offscreen *targets[TargetLimit] = {0};
    for (unsigned i = 0; i < TargetLimit; ++i)
        CHECK(mtvk_vk_offscreen_open(context, 1, 1, &targets[i]) == VK_SUCCESS);
    target = NULL;
    CHECK(mtvk_vk_offscreen_open(context, 1, 1, &target) == VK_ERROR_TOO_MANY_OBJECTS && !target);
    CHECK(mtvk_vk_close(context) == VK_NOT_READY);
    for (unsigned i = 0; i < TargetLimit; ++i) CHECK(mtvk_vk_offscreen_close(targets[i]) == VK_SUCCESS);
    CHECK(mtvk_vk_offscreen_open(context, 1, 1, &target) == VK_SUCCESS);
    CHECK(mtvk_vk_offscreen_begin(target, &red, &commands) == VK_SUCCESS);
    CHECK(mtvk_vk_offscreen_close(target) == VK_SUCCESS); // Recording but never submitted: legal destruction.
    CHECK(!mtvk_vk_validation_errors(context) && !mtvk_vk_validation_warnings(context));
    CHECK(mtvk_vk_close(context) == VK_SUCCESS);
    puts("PASS real GPU odd-size clear/readback, guards, state ordering, target quota and context lifetime");
    return 0;
}
