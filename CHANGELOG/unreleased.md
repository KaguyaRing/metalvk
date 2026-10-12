# Unreleased

## NVIDIA transport and module maintenance

- Extract native IOKit queries into libmetalvk-nvidia-darwin.a; share response
  validation/device policy/capability projection with the CLI and Mesa adapter.
- Publish native wire output only after response validation and close/release;
  report structured failure stage/native error with transactional output.
- Add explicit registry-ID Mesa native discovery ingress without a default GPU
  or loopback fallback. ICD physical enumeration and execution remain pending.
- Test actual Darwin source using isolated mock IOKit faults, and compile/test
  native-ingress mapping in the Linux Mesa conformance binary with a test-only
  provider. Neither fixture path is part of a native shipped transport/ICD.

- Wire a dedicated NVIDIA read-only UserClient (connect type 1) to the captured
  PCI snapshot, private transport session and versioned discovery codec.
  Existing generic diagnostic connect type 0 remains unchanged.
- Add the real IOKit `metalvk-nvidia-discovery` client to the App bundle. It
  requires an explicit registry ID; no default GPU, privileged install or
  loopback fallback. Native CI checks argument and no-service failure paths.
- Share read-only IOKit locking policy and snapshot-copy lifecycle checks.
  Export only observed PCI identity/address/revision, leaving RM fields unknown.
  Hardware loading and a real UserClient round-trip still require target testing.

- Separate discovery initialization/capability projection from session dispatch;
  update host, Mesa and native kernel build graphs to compile the shared module.
  Portable discovery payloads now use a fixed 328-byte little-endian codec.
- Bump the private NVIDIA transport to version 2; reject version 1 host-struct
  discovery payloads. Validate narrow values, PCI addressing, boolean flags,
  array counts, names, alignment and VA ranges before publishing host output.
- DEVICE_QUERY respects a nonzero response payload budget. Reject dangerous
  session buffer overlap; snapshot self-initialization before state mutation.
- Mesa adapter decodes wire bytes before constructing its pdev, rejects missing
  query backing and unimplemented operation bits, and still refuses execution.
- Add independent golden bytes, full-field preservation, padding/noncanonical
  name handling, all truncations, malformed values, aliasing and 20,000 bounded
  payload mutations. These are host regression evidence, not hardware discovery.
- Reject unknown response capability bits and incompatible/corrupted session
  state; require both import/export operations for dma-buf advertisement.
- Add malformed-session, forged-response and catalog-parser regressions.
  Operation-name/category mapping is not complete RM semantic alignment.
- Record fixed Apple/NVIDIA lifecycle research: provider ownership, PM,
  termination, DMA and RM integration prerequisites. No hardware writes added.

## Corrections and verification

- Clarify Vulkan close ownership: live targets and unexpected drain failures
  retain the context; device loss and dispose-time validation/invariant errors
  destroy it and must not be retried.
- Add native CTA VIC 16/64 and extended VIC 193/225 regressions. Extended VICs
  must not be folded into low VICs by stripping bit 7.
- Keep NVIDIA/AMD/Intel Metal rendering as a per-device/per-OS coverage target,
  not an existing support claim. Windows NVIDIA, offline NAK, Apple Paravirtual
  and kext build results remain independent evidence.

## Development release automation

- Build immutable tag source on macOS 15/26 Intel before publishing diagnostic
  packages as GitHub prereleases. Include source/build metadata, license,
  development warnings and SHA-256 checksums; preserve bundle permissions.
- Packages are ad-hoc signed, not notarized, and do not install/load a kext.
- Preserve historical v0.1.0 at 318d24a. Subsequent development tags use
  vMAJOR.MINOR.PATCH-<last-pushed-short-sha>; no existing tag is moved.

See [v0.1.0](0.1.0.md) for the tagged milestone. Entries above describe later
development and do not retroactively become part of v0.1.0.
