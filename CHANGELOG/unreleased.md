# Unreleased

## Removed

- The home-grown `mtvk_gpu_*` backend registration/dispatch ABI (`include/gpu/`,
  `src/gpu/`) and its contract, plugin and legacy host tests. The neutral
  interface is the Vulkan specification; a self-made GPU ABI duplicated it.
- `include/base/limits.h`: its remaining contents were the removed ABI's limits.

## Added

- Versioned vendor-neutral runtime ABI with platform-supplied allocation and locking.
- Backend replacement, capability enumeration, dispatch forwarding, opaque lifetime-safe handles and rollback/quarantine for backend failures.
- Concurrent lifecycle and independently compiled binary-provider integration tests.
- SDK-free core CI and explicit source/archive dependency boundary checks.

## Changed

- Default `libmetalvk.a` now contains only the base implementation.
- Diagnostic protocol is a separate archive.
- Vendor kext prototype moved to `backends/`; Vulkan probes moved to `validation/`.
- App/kext reference workflow is manual-only and excluded from generic acceptance.

## Earlier development components

- Vendor-neutral C11 backend registration and host tests.
- Versioned, little-endian PCI diagnostic protocol with exact-size and semantic validation.
- IOKit diagnostic kext matching NVIDIA 10DE:28E0 without programming the GPU.
- AppKit diagnostic App with asynchronous reads, error reporting and atomic report export.
- Native macOS CI, ad-hoc signing verification and development DMG generation.
- Real Vulkan 1.2 context integration using official Vulkan handles and a hardware ICD.
- SPIR-V compute readback integration test with Khronos and synchronization validation, hardware-only selection and untouched-tail checks.

The Windows Vulkan compute path has been verified on NVIDIA and AMD hardware. macOS GPU execution, display management, system Metal integration, Metal-to-SPIR-V conversion and privileged installation are not implemented. No 0.1.0 release has been published.
