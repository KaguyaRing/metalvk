# metalvk

[中文](README_ZN.md)

**A vendor-neutral GPU layer and rendering pipeline under development.** The target is real Metal rendering on NVIDIA, AMD and Intel GPUs on macOS 15/26, with support verified per GPU architecture, PCI ID and OS build. This is a coverage goal, not a claim that all models are supported. A Vulkan driver alone does not provide macOS system integration.

The neutral execution interface is the **Vulkan specification**, not a custom GPU ABI. The earlier `mtvk_gpu_*` backend registration and dispatch ABI has been removed.

## Current status

**The macOS rendering pipeline is not connected end to end. There is no usable GPU driver or verified macOS Metal hardware configuration yet.** Build success, offline shader compilation, kext loading, GPU execution and system display are separate acceptance gates.

| Component / evidence | Verified scope | What it does not prove |
|---|---|---|
| Vulkan compute and graphics | Windows RTX 4060 Laptop, existing NVIDIA ICD; full readback, zero validation errors/warnings | The project's Darwin driver or Metal translation |
| NVK/NAK baseline | Full Linux ICD build, CPU suites, offline SM89 shader compilation and host adapter conformance | A Darwin ICD or execution of NAK output on macOS |
| NVIDIA diagnostic kext | Native macOS 15/26 Intel build and ad-hoc signature checks; old CI artifacts' imports match reference export lists | Target Kernel Collection acceptance, real-machine loading, PCI service startup or GPU execution |
| System Metal acceptance client | Apple Paravirtual reference render and full-image verification | Project-provided Metal acceleration or physical GPU support |
| Metal translation and hardware backend | Not implemented end to end | Metal rendering, WindowServer presentation or display output |

The NVIDIA kext currently matches **`10de:28e0` only**, with its read-only BAR0 probe **disabled by default**. AMD/Intel hardware paths have not been accepted. None of the independent results above may be combined into a claim that the complete driver works.

## Intended rendering path and acceptance

```text
Metal application / system client
  → Project Metal device integration and Metal → Vulkan translation
  → Vulkan Loader / vendor ICD
  → Vendor Darwin resource, submission and synchronization backend
  → GPU execution
  → Readback or IOSurface / presentation / display
```

Vulkan is the internal execution interface; this is **not Vulkan → Apple Metal**. MoltenVK cannot supply a missing NVIDIA macOS driver and is rejected by the hardware context. MSL source translation and precompiled AIR/metallib compatibility require separate implementation and verification.

- Select the project's target registry ID explicitly and verify the actual PCI/GPU identity. No fallback to another system GPU, software rasterization or MoltenVK may satisfy acceptance.
- On each declared GPU/OS combination, create resources and shaders through Metal, execute a real draw, wait for completion and verify the entire readback image. A minimal pattern proves only its tested feature subset, not compatibility with arbitrary Metal applications.
- Record compilation, loading, Vulkan execution, Metal offscreen rendering, unmodified-app discovery and system display separately. Offscreen rendering does not establish WindowServer, HDR, high refresh rate, sleep/wake or GPU reset support.
- Implement and verify ownership, power management, trusted firmware, DMA/IOMMU, GPU memory/VA, submission and synchronization before advertising hardware execution capabilities.

See [rendering gates](docs/rendering-plan.md), [NVIDIA integration](docs/nvidia-nvk.md) and [kernel ownership/runtime limits](docs/kernel-runtime.md) (Chinese). Missing target hardware or test access means **not verified**, not passed.

## Neutral core

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

- `backends/nvidia/`: separate PCI/AD107 boot-register validation library, shared transport/discovery/session support and opt-in, read-only BAR0 kext probe. The shared dispatcher is not wired to a live NVIDIA UserClient. No RM/GSP startup, DMA, GPU execution or display support. See [NVIDIA backend scope](docs/nvidia-backend.md) (Chinese). `make test-nvidia CONFIG=Release` runs host fixture tests, not hardware tests.
- `include/vk/`, `src/vk/`: hardware Vulkan context and reusable [offscreen render/readback targets](docs/offscreen-rendering.md) (Chinese), built separately as `libmetalvk-vulkan.a`; requires the Vulkan SDK/loader and a working hardware ICD. Real native pipelines, fences and multi-frame readback have passed on Windows NVIDIA, not on the macOS NVIDIA backend.
- [NVK/NAK baseline](docs/nvidia-nvk.md) (Chinese): pinned Mesa 26.2.4 full Linux ICD build and offline compilation of repository vertex/fragment shaders into SM89 native code. The checker verifies operation names/categories (30 upstream operations mapped to 28 transport operations), not complete field or RM semantics. The adapter test is a host loopback, not kernel IPC. Darwin endpoint wiring, RM/GSP execution and macOS ICD remain unimplemented; Linux artifacts are not macOS drivers.
- `kexts/Platform/`: shared PCI snapshot, service lifecycle, read-only UserClient, bounded wired host-memory domains, deferred CPU work and uptime services, built as `libmetalvk-kernel-platform.a` and linked into the diagnostic kext. Memory/work services are not yet bound to RM or exposed to userspace. No GPU execution yet. See [ownership and runtime limits](docs/kernel-runtime.md) (Chinese).
- `app/`, `include/ipc`, `src/ipc`: a read-only diagnostics app and a separate diagnostics protocol.
- `validation/vulkan/`: validation tools for real Vulkan ICDs, not the neutral layer or a production backend. Successful compilation does not prove GPU execution or Metal integration.
- `.github/workflows/reference-macos.yml`: a manually triggered/reusable platform reference build. See [separate components](docs/reference-components.md) (Chinese).
- `.github/workflows/release.yml`: immutable-tag macOS 15/26 Intel builds and diagnostic-only GitHub prereleases. Archives preserve bundle permissions and include metadata, warnings, license and checksums. No automatic installation or kext loading. See [development publishing](docs/development-releases.md) (Chinese).

The RTX 4060 Laptop GPU has passed real compute readback and a 64×64 offscreen graphics test with all 4096 pixels checked, with zero validation errors/warnings on Windows. This is not evidence of macOS or system Metal support. See the [rendering plan and acceptance gates](docs/rendering-plan.md) (Chinese).

## Planned modules (not implemented)

Research: [Pinned LinuxKPI/Darwin feasibility study](docs/research/linuxkpi/README.md)
(Linux 6.14 xe / Intel DG2; research only, no backend migration or Darwin GPU execution claim).

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

Hardware support still requires vendor-specific kexts, shared platform integration, and real-device testing for each supported combination. **`v0.1.0` marks a development milestone, not a usable GPU driver or a verified end-to-end Metal rendering release.**

## Module and kext development rules

- Preserve mirrored headers/sources and module aggregation headers. Keep the neutral core, Vulkan userspace, shared kernel platform, vendor backend and diagnostics App independently maintainable.
- Separate framing, discovery capability policy and session dispatch. Future lifecycle, RM/GSP, DMA/BO, GPU VA, queue/IRQ, sync and display managers must have explicit ownership and cleanup order; do not introduce empty success handlers.
- Keep diagnostic UserClient operations read-only. GPU execution needs a separate, permission-checked vendor interface with connection-owned handles, quotas and exit/reset behavior; never expose raw kernel pointers.
- A static export match or ad-hoc signature is not a loading guarantee. Target-machine authorization, Kernel Collection linkage, module presence, PCI attachment and diagnostic round-trip each need evidence. Inspecting a historical CI artifact does not validate subsequent working-tree changes.
- Never load a kext, enable MMIO writes/DMA, alter startup security or edit OpenCore merely to make a build/test pass. Hardware testing requires an authorized isolated target and a recovery plan.

Mesa and its dependencies are **not uniformly MIT**. The project's neutral core license does not replace upstream notices; formal binary redistribution still requires a final dependency/license review.
