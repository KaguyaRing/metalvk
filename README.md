# metalvk

[中文](README_ZN.md)

**A vendor-neutral GPU layer and rendering pipeline.** The goal is to translate Metal semantics to Vulkan and integrate supported devices with system Metal. A Vulkan driver alone does not provide macOS system integration.

The neutral execution interface is the **Vulkan specification**, not a custom GPU ABI. The earlier `mtvk_gpu_*` backend registration and dispatch ABI has been removed.

## Current status

The default artifact, `libmetalvk.a`, contains the base layer, pure host-memory quota accounting, bounded CPU work accounting and deadline arithmetic, using standard C headers. It does not include vendor SDKs, hardware drivers, kernel code, private ioctls, a Vulkan implementation, or a shader compiler. The diagnostics protocol is built separately as `libmetalvk-diagnostics.a`.

It also implements a [display backend control API](docs/display-backend.md) (Chinese): mode/HDR validation and forwarding, display reset/replay, runtime signals and fail-closed state management. Hardware callbacks must be supplied by a vendor backend; none is silently simulated. No physical display/HDR support is claimed yet.

[EDID/CTA/DisplayID parsing](docs/edid.md) (Chinese) now decodes actual byte snapshots and intersects identified timings/HDR/depth with backend-verified capabilities. Supported formats are bounded and explicitly listed; omitted features are reported, not fabricated. Windows monitor-cache samples have been tested, not hardware link control.

```text
include/                     src/                      (mirrored module layout)
  platform.h                   base/status.c
  metalvk.h                    ipc/diagnostics.c
  base/  init.h status.h span.h
  memory/ init.h budget.h       memory/budget.c
  runtime/ init.h work.h time.h runtime/work.c time.c
  display/ init.h control.h edid.h display/control.c validation.c edid.c cta.c displayid.c edid-capabilities.c
  ipc/   init.h diagnostics.h
```

## Languages

The base layer uses **C11**, standard C headers, and no platform dependencies. Components requiring IOKit or the Objective-C runtime, such as kexts and Metal integration, may use **C++17 / Objective-C++** in separate translation units.

Each module has an `init.h` aggregating its headers; `metalvk.h` aggregates the public modules. Future modules follow the same structure.

## Build and verification

The default build requires a C11 compiler, ar, and make. **No vendor, Vulkan, or kernel SDK is required.**

Host tests also require a C++17 compiler for the owner-lock concurrency test; the library itself remains C11.

```sh
make CONFIG=Release
make test CONFIG=Debug
make test CONFIG=Release
make test CONFIG=Alpha
python3 tools/check_core_boundary.py --archive target/Release/libmetalvk.a
```

On Windows, use `mingw32-make` and `AR=llvm-ar`.

`tools/check_core_boundary.py` checks the default library's source set, header dependencies, archive members, and absence of vendor/platform conditional compilation. `.github/workflows/core.yml` runs these checks on Linux and macOS, including freestanding syntax and C++ public-header checks.

## Separate components (excluded from the default library)

- `backends/nvidia/`: separate PCI/AD107 boot-register validation library and opt-in, read-only BAR0 kext probe. Disabled by default; no RM/GSP startup, DMA, GPU execution or display support. See [NVIDIA backend boundaries](docs/nvidia-backend.md) (Chinese). `make test-nvidia CONFIG=Release` runs host fixture tests, not hardware tests.
- `include/vk/`, `src/vk/`: hardware Vulkan context and reusable [offscreen render/readback targets](docs/offscreen-rendering.md) (Chinese), built separately as `libmetalvk-vulkan.a`; requires the Vulkan SDK/loader and a working hardware ICD. Real native pipelines, fences and multi-frame readback have passed on Windows NVIDIA, not on the macOS NVIDIA backend.
- [NVK/NAK baseline](docs/nvidia-nvk.md) (Chinese): pinned Mesa 26.2.4 full Linux ICD build and offline compilation of repository vertex/fragment shaders into SM89 native code. Darwin kernel transport, RM/GSP execution and macOS ICD remain unimplemented; Linux artifacts are not macOS drivers.
- `kexts/Platform/`: shared PCI snapshot, service lifecycle, read-only UserClient, bounded wired host-memory domains, deferred CPU work and uptime services, built as `libmetalvk-kernel-platform.a` and linked into the diagnostic kext. Memory/work services are not yet bound to RM or exposed to userspace. No GPU execution yet. See [ownership and runtime limits](docs/kernel-runtime.md) (Chinese).
- `app/`, `include/ipc`, `src/ipc`: a read-only diagnostics app and a separate diagnostics protocol.
- `validation/vulkan/`: validation tools for real Vulkan ICDs, not the neutral layer or a production backend. Successful compilation does not prove GPU execution or Metal integration.
- `.github/workflows/reference-macos.yml`: a manually triggered platform reference build. See [separate components](docs/reference-components.md) (Chinese).

The RTX 4060 Laptop GPU has passed real compute readback and a 64×64 offscreen graphics test with all 4096 pixels checked, with zero validation errors/warnings on Windows. This is not evidence of macOS or system Metal support. See the [rendering plan and acceptance gates](docs/rendering-plan.md) (Chinese).

## Planned modules (not implemented)

Implement these incrementally as functional modules, rather than empty drivers:

```text
include|src/  platform/  OS detection and private ABI table lookup
              display/   Further EDID/DisplayID parsing, link encoding and presentation
              accel/     IOGraphicsAccelerator2 integration
              agdc/      AppleGraphicsDeviceControl integration
              metal/     Metal semantics (Metal → Vulkan)
              vk/        Further Vulkan resource/submission modules
              backend/   Vendor-specific platform integration
data/abi/     kext ABI tables keyed by OS build, separate from logic
kexts/Platform/   Further kernel resource and recovery services
kexts/Display/    framebuffer / accelerator / agdc
```

Hardware support still requires vendor-specific kexts, shared platform integration, and real-device testing for each supported combination. **There is no usable driver yet, and `0.1.0` has not been released.**
