// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
/* System Metal acceptance client, independent of metalvk implementation.
 * A matching registry ID must be explicitly selected; an unrelated Apple,
 * Intel or AMD device must never satisfy a metalvk integration claim. */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <dispatch/dispatch.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "pattern.h"

namespace {
constexpr uint64_t CompletionTimeoutNs = UINT64_C(5000000000);
constexpr NSUInteger Width = MTVK_PATTERN_EXTENT;
constexpr NSUInteger Height = MTVK_PATTERN_EXTENT;
constexpr NSUInteger RowBytes = Width * MTVK_PATTERN_CHANNELS;
constexpr NSUInteger ImageBytes = RowBytes * Height;
static_assert(RowBytes % 256 == 0, "Metal copy row alignment must be respected");
const char *text(NSString *value) {
    const char *result = value.UTF8String;
    return result ? result : "unknown";
}
NSString *const ShaderSource = @"#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct Vertex { float4 position [[position]]; float2 uv; };\n"
    "vertex Vertex vertex_main(uint i [[vertex_id]]) {\n"
    "  const float2 uv[3] = {float2(0,0), float2(2,0), float2(0,2)};\n"
    "  Vertex v; v.uv = uv[i];\n"
    "  v.position = float4(v.uv.x * 2 - 1, 1 - v.uv.y * 2, 0, 1);\n"
    "  return v;\n"
    "}\n"
    "fragment float4 fragment_main(Vertex v [[stage_in]]) {\n"
    "  return float4(v.uv.x < 0.5 ? 0.0 : 1.0,\n"
    "                v.uv.y < 0.5 ? 0.0 : 1.0, 0.0, 1.0);\n"
    "}\n";

int render(id<MTLDevice> device) {
    NSError *error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:ShaderSource options:nil error:&error];
    if (!library) {
        std::fprintf(stderr, "Metal shader compile failed: %s\n", text(error.localizedDescription));
        return 1;
    }
    MTLRenderPipelineDescriptor *pipelineInfo = [MTLRenderPipelineDescriptor new];
    pipelineInfo.vertexFunction = [library newFunctionWithName:@"vertex_main"];
    pipelineInfo.fragmentFunction = [library newFunctionWithName:@"fragment_main"];
    pipelineInfo.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
    if (!pipelineInfo.vertexFunction || !pipelineInfo.fragmentFunction) return 1;
    id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:pipelineInfo error:&error];
    if (!pipeline) {
        std::fprintf(stderr, "Metal pipeline creation failed: %s\n", text(error.localizedDescription));
        return 1;
    }
    MTLTextureDescriptor *textureInfo = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:Width height:Height mipmapped:NO];
    textureInfo.storageMode = MTLStorageModePrivate;
    textureInfo.usage = MTLTextureUsageRenderTarget;
    id<MTLTexture> texture = [device newTextureWithDescriptor:textureInfo];
    id<MTLBuffer> readback = [device newBufferWithLength:ImageBytes options:MTLResourceStorageModeShared];
    id<MTLCommandQueue> queue = [device newCommandQueue];
    if (!texture || !readback || !queue || !readback.contents) {
        std::fputs("Metal resource allocation failed\n", stderr); return 1;
    }
    std::memset(readback.contents, 0xa5, ImageBytes);
    id<MTLCommandBuffer> command = [queue commandBuffer]; // default retained references
    if (!command) return 1;
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 1, 1);
    id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
    if (!encoder) return 1;
    [encoder setRenderPipelineState:pipeline];
    [encoder setViewport:MTLViewport{0, 0, static_cast<double>(Width), static_cast<double>(Height), 0, 1}];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [encoder endEncoding];
    id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
    if (!blit) return 1;
    [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
        sourceSize:MTLSizeMake(Width, Height, 1) toBuffer:readback destinationOffset:0
        destinationBytesPerRow:RowBytes destinationBytesPerImage:ImageBytes];
    [blit endEncoding];
    dispatch_semaphore_t completed = dispatch_semaphore_create(0);
    if (!completed) return 1;
    [command addCompletedHandler:^(id<MTLCommandBuffer>) { dispatch_semaphore_signal(completed); }];
    [command commit];
    if (dispatch_semaphore_wait(completed, dispatch_time(DISPATCH_TIME_NOW,
        static_cast<int64_t>(CompletionTimeoutNs))) != 0) {
        std::fputs("Metal command timed out; no successful rendering claim\n", stderr);
        return 1; // Framework retains submitted references; process teardown is the outer boundary.
    }
    if (command.status != MTLCommandBufferStatusCompleted) {
        std::fprintf(stderr, "Metal command failed: %s\n", text(command.error.localizedDescription));
        return 1;
    }
    size_t mismatch = SIZE_MAX;
    if (!mtvk_probe_check_pattern(static_cast<const uint8_t *>(readback.contents), ImageBytes,
        static_cast<uint32_t>(Width), static_cast<uint32_t>(Height), &mismatch)) {
        std::fprintf(stderr, "Metal image mismatch at pixel %zu\n", mismatch); return 1;
    }
    std::printf("PASS system Metal offscreen rendering: %lu pixels\n", static_cast<unsigned long>(Width * Height));
    return 0;
}
}

int main(int argc, char **argv) {
    @autoreleasepool {
        const bool list = argc == 2 && std::strcmp(argv[1], "--list-devices") == 0;
        if (!list && (argc != 3 || std::strcmp(argv[1], "--registry-id") != 0)) {
            std::fprintf(stderr, "usage: %s --list-devices | --registry-id <decimal/hex>\n", argv[0]);
            return 64;
        }
        uint64_t requested = 0;
        if (!list) {
            if (argv[2][0] < '0' || argv[2][0] > '9') return 64;
            char *end = nullptr;
            errno = 0;
            const unsigned long long value = std::strtoull(argv[2], &end, 0);
            if (errno || end == argv[2] || *end || !value) return 64;
            requested = static_cast<uint64_t>(value);
        }
        NSArray<id<MTLDevice>> *devices = MTLCopyAllDevices();
        for (id<MTLDevice> device in devices) {
            if (list || device.registryID == requested) {
                std::printf("Metal device: %s registry-id=0x%llx\n", text(device.name),
                    static_cast<unsigned long long>(device.registryID));
                if (!list) return render(device);
            }
        }
        if (list) {
            std::printf("Metal device count: %lu (listing only, no render claim)\n",
                static_cast<unsigned long>(devices.count));
            return 0;
        }
        std::fputs("Requested Metal device is not exposed by the system\n", stderr);
        return 2;
    }
}
