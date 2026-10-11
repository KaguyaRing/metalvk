# Unreleased

## NVIDIA transport and module maintenance

- Separate discovery initialization/capability projection from session dispatch;
  update host, Mesa and native kernel build graphs to compile the shared module.
  Explicit portable discovery payload encoding is still pending.
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
