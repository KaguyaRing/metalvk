# metalvk

[中文](README_ZN.md)

**A vendor-neutral GPU layer and rendering pipeline.** The goal is to translate Metal semantics to Vulkan and integrate supported devices with system Metal. A Vulkan driver alone does not provide macOS system integration.

The neutral execution interface is the **Vulkan specification**, not a custom GPU ABI. The earlier `mtvk_gpu_*` backend registration and dispatch ABI has been removed.

## Current status

The default artifact, `libmetalvk.a`, contains only the base layer and uses standard C headers. It does not include vendor SDKs, hardware drivers, kernel code, private ioctls, a Vulkan implementation, or a shader compiler. The diagnostics protocol is built separately as `libmetalvk-diagnostics.a`.

```text
include/                     src/                      (mirrored module layout)
  platform.h                   base/status.c
  metalvk.h                    ipc/diagnostics.c
  base/  init.h status.h span.h
  ipc/   init.h diagnostics.h
```

## Languages

The base layer uses **C11**, standard C headers, and no platform dependencies. Components requiring IOKit or the Objective-C runtime, such as kexts and Metal integration, may use **C++17 / Objective-C++** in separate translation units.

Each module has an `init.h` aggregating its headers; `metalvk.h` aggregates the public modules. Future modules follow the same structure.

## Build and verification

The default build requires a C11 compiler, ar, and make. **No vendor, Vulkan, or kernel SDK is required.**

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

- `backends/`: vendor kext prototype directories; not yet integrated.
- `include/vk/`, `src/vk/`: shared hardware Vulkan context, built separately as `libmetalvk-vulkan.a`; requires the Vulkan SDK/loader.
- `kexts/Platform/`: shared PCI snapshot, service lifecycle, and read-only UserClient modules, built as `libmetalvk-kernel-platform.a` and linked into the diagnostic kext. No GPU execution yet.
- `app/`, `include/ipc`, `src/ipc`: a read-only diagnostics app and a separate diagnostics protocol.
- `validation/vulkan/`: validation tools for real Vulkan ICDs, not the neutral layer or a production backend. Successful compilation does not prove GPU execution or Metal integration.
- `.github/workflows/reference-macos.yml`: a manually triggered platform reference build. See [separate components](docs/reference-components.md) (Chinese).

The RTX 4060 Laptop GPU has passed real compute readback and a 64×64 offscreen graphics test with all 4096 pixels checked, with zero validation errors/warnings on Windows. This is not evidence of macOS or system Metal support. See the [rendering plan and acceptance gates](docs/rendering-plan.md) (Chinese).

## Planned modules (not implemented)

Implement these incrementally as functional modules, rather than empty drivers:

```text
include|src/  platform/  OS detection and private ABI table lookup
              display/   IOFramebuffer, EDID/DisplayID, display modes, HDR metadata
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
