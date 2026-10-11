#!/usr/bin/env python3
"""Build development-only x86_64 App/kext bundles using native Apple tools.

No signing identity is implied: output is explicitly ad-hoc signed. Hardware
loading, user approval, Installer integration and production signing are separate.
"""
from __future__ import annotations

import argparse
import pathlib
import plistlib
import shutil
import subprocess
import sys


def run(*args: str) -> None:
    subprocess.run(args, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel-sdk", type=pathlib.Path, required=True)
    options = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("Native macOS tools are required; use the macOS CI job.")
    root = pathlib.Path(__file__).resolve().parent.parent
    sdk = options.kernel_sdk.resolve()
    if not (sdk / "Headers/IOKit/IOService.h").is_file():
        parser.error("--kernel-sdk must contain Headers/IOKit/IOService.h")
    build = root / "target/macos"
    build.mkdir(parents=True, exist_ok=True)
    app = build / "MetalVK.app/Contents"
    kext = build / "MetalVK.kext/Contents"
    for bundle in (app, kext):
        (bundle / "MacOS").mkdir(parents=True, exist_ok=True)
    for source, target in (("app/Info.plist", app), ("backends/nvidia/kext/Info.plist", kext)):
        content = (root / source).read_bytes()
        plistlib.loads(content)
        (target / "Info.plist").write_bytes(content)
    includes = ["-I", str(root / "include")]
    warnings = ["-Wall", "-Wextra", "-Werror"]
    user = ["-arch", "x86_64", "-mmacosx-version-min=13.0", "-O2", *warnings, *includes]
    run("xcrun", "clang", "-std=c11", *user, "-c", str(root / "src/ipc/diagnostics.c"),
        "-o", str(build / "diagnostics-user.o"))
    run("xcrun", "clang", "-std=c11", *user, "-c", str(root / "src/base/status.c"),
        "-o", str(build / "status-user.o"))
    display_units = ("control", "validation", "edid", "cta", "displayid", "edid-capabilities")
    display_user_objects = []
    for unit in display_units:
        obj = build / f"display-{unit}-user.o"
        run("xcrun", "clang", "-std=c11", *user, "-Wpedantic", "-c", str(root / f"src/display/{unit}.c"), "-o", str(obj))
        display_user_objects.append(obj)
    run("xcrun", "clang", "-std=c11", *user, "-Wpedantic", str(root / "tools/edid-inspect.c"),
        *(str(obj) for obj in display_user_objects), str(build / "status-user.o"), "-o", str(build / "edid-inspect"))
    run("xcrun", "clang++", "-std=c++17", "-fobjc-arc", *user,
        str(root / "app/main.mm"), str(root / "app/Diagnostics.mm"),
        str(build / "diagnostics-user.o"), str(build / "status-user.o"),
        "-framework", "AppKit", "-framework", "IOKit", "-framework", "UniformTypeIdentifiers",
        "-o", str(app / "MacOS/MetalVK"))
    # Independent system Metal acceptance client; never selects an unrelated
    # default device to claim metalvk works. No GPU execution in the build step.
    probe_include = ["-I", str(root / "validation/common")]
    run("xcrun", "clang", "-std=c11", *user, "-Wpedantic", *probe_include, "-c",
        str(root / "validation/common/pattern.c"), "-o", str(build / "pattern-user.o"))
    run("xcrun", "clang++", "-std=c++17", "-fobjc-arc", *user, "-Wpedantic", *probe_include,
        str(root / "validation/metal/render.mm"), str(build / "pattern-user.o"),
        "-framework", "Foundation", "-framework", "Metal", "-o", str(build / "metal-render"))
    kernel = ["-arch", "x86_64", "-mmacosx-version-min=13.0", "-O2",
              "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
              "-mno-red-zone", "-mno-sse", "-mno-sse2", "-mno-mmx",
               "-DKERNEL", "-DKERNEL_PRIVATE", *warnings, *includes,
               "-I", str(root / "kexts/Platform/include"),
               "-isystem", str(sdk / "Headers")]
    run("xcrun", "clang", "-std=c11", *kernel, "-c", str(root / "src/ipc/diagnostics.c"),
        "-o", str(build / "diagnostics-kernel.o"))
    # IOPCIDevice is deprecated in Apple's newer SDK, but needed for the selected
    # IOKit graphics-driver route. Suppression is limited to this native target;
    # upstream SDK headers are system headers, project warnings remain errors.
    kernel_cpp = ["-std=c++17", "-mkernel", "-fapple-kext",
        "-fno-exceptions", "-fno-rtti", "-fno-use-cxa-atexit", "-Wno-deprecated-declarations",
        "-Wno-unused-parameter", *kernel]
    platform_objects = [build / "diagnostics-kernel.o"]
    run("xcrun", "clang", "-std=c11", *kernel, "-c", str(root / "src/memory/budget.c"),
        "-o", str(build / "budget-kernel.o"))
    platform_objects.append(build / "budget-kernel.o")
    for unit in ("work", "time"):
        obj = build / f"runtime-{unit}-kernel.o"
        run("xcrun", "clang", "-std=c11", *kernel, "-c", str(root / f"src/runtime/{unit}.c"), "-o", str(obj))
        platform_objects.append(obj)
    for unit in display_units:
        obj = build / f"display-{unit}-kernel.o"
        run("xcrun", "clang", "-std=c11", *kernel, "-c", str(root / f"src/display/{unit}.c"), "-o", str(obj))
        platform_objects.append(obj)
        run("xcrun", "clang", "-std=c11", *kernel, "--analyze", "-Xanalyzer", "-analyzer-output=text",
            str(root / f"src/display/{unit}.c"))
    for unit in ("pci", "device", "client", "memory", "context", "work"):
        obj = build / f"platform-{unit}.o"
        run("xcrun", "clang++", *kernel_cpp, "-c",
            str(root / f"kexts/Platform/src/platform/{unit}.cpp"), "-o", str(obj))
        platform_objects.append(obj)
    for unit in ("memory", "context", "work"):
        run("xcrun", "clang++", *kernel_cpp, "--analyze", "-Xanalyzer", "-analyzer-output=text",
            str(root / f"kexts/Platform/src/platform/{unit}.cpp"))
    platform_library = build / "libmetalvk-kernel-platform.a"
    # libtool replaces the archive, avoiding stale members from previous builds.
    run("xcrun", "libtool", "-static", "-o", str(platform_library),
        *(str(obj) for obj in platform_objects))
    nvidia_includes = ["-I", str(root / "backends/nvidia/include")]
    run("xcrun", "clang", "-std=c11", *kernel, *nvidia_includes, "-c",
        str(root / "backends/nvidia/src/boot.c"), "-o", str(build / "nvidia-boot.o"))
    run("xcrun", "clang++", *kernel_cpp, *nvidia_includes, "-c",
        str(root / "backends/nvidia/kext/Probe.cpp"), "-o", str(build / "nvidia-probe.o"))
    for source, language in (("src/boot.c", "c"), ("kext/Probe.cpp", "c++")):
        flags = ["-std=c11", *kernel] if language == "c" else kernel_cpp
        run("xcrun", "clang" if language == "c" else "clang++", *flags, *nvidia_includes,
            "--analyze", "-Xanalyzer", "-analyzer-output=text", str(root / f"backends/nvidia/{source}"))
    nvidia_library = build / "libmetalvk-nvidia-kernel.a"
    run("xcrun", "libtool", "-static", "-o", str(nvidia_library),
        str(build / "nvidia-boot.o"), str(build / "nvidia-probe.o"))
    run("xcrun", "clang++", *kernel_cpp, *nvidia_includes, "-c", str(root / "backends/nvidia/kext/Driver.cpp"),
        "-o", str(build / "Driver.o"))
    run("xcrun", "ld", "-arch", "x86_64", "-kext", "-undefined", "dynamic_lookup",
        str(build / "Driver.o"), "-force_load", str(nvidia_library), "-force_load", str(platform_library),
        "-o", str(kext / "MacOS/MetalVK"))
    imports = subprocess.check_output(
        ["xcrun", "nm", "-u", str(kext / "MacOS/MetalVK")], text=True)
    # XNU Libkern.exports legitimately provides ___bzero, ___cxa_pure_virtual
    # and stack-check helpers. Only exclude known user-runtime imports here;
    # this is not a substitute for target-KC dependency/linkage validation.
    forbidden = ("___cxa_atexit", "___dso_handle", "__Unwind", "___cxa_throw",
                 "_malloc", "_calloc", "_realloc", "_free", "_pthread", "_objc")
    for line in imports.splitlines():
        symbol = line.split()[-1] if line.split() else ""
        if symbol.startswith(forbidden) or symbol in ("_thread_call_cancel_wait", "_thread_call_allocate_with_priority"):
            raise ValueError(f"Kernel binary imports a user/runtime symbol: {symbol}")
    symbols = subprocess.check_output(
        ["xcrun", "nm", "-g", str(kext / "MacOS/MetalVK")], text=True)
    for name in ("MetalVKPlatformDevice", "MetalVKDiagnosticClient", "capturePci",
                 "MetalVKMemoryDomain", "MetalVKWiredBuffer", "mtvk_memory_budget_reserve",
                  "MetalVKWorkQueue", "uptimeNanoseconds", "mtvk_work_push",
                  "mtvk_display_apply", "mtvk_display_notify", "mtvk_display_recover",
                  "mtvk_edid_parse", "mtvk_edid_select_capabilities", "probeBoot", "mtvk_nv_identify"):
        definitions = [line for line in symbols.splitlines() if name in line and " U " not in line]
        if not definitions:
            raise ValueError(f"Shared kernel module was not linked: {name}")
    for bundle in (kext.parent, app.parent):
        run("codesign", "--force", "--sign", "-", str(bundle))
        run("codesign", "--verify", "--strict", "--verbose=2", str(bundle))
    resources = app / "Resources"
    resources.mkdir(exist_ok=True)
    # Only deployment payload, no automatic privileged copy/load in this stage.
    payload = resources / "MetalVK.kext"
    if payload.exists():
        shutil.rmtree(payload)
    shutil.copytree(kext.parent, payload)
    shutil.copy2(root / "LICENSE", resources / "LICENSE.txt")
    run("codesign", "--force", "--sign", "-", str(app.parent))
    run("codesign", "--verify", "--strict", "--verbose=2", str(app.parent))
    run("xcrun", "nm", "-g", str(kext / "MacOS/MetalVK"))
    run("xcrun", "otool", "-hv", str(kext / "MacOS/MetalVK"))
    run("hdiutil", "create", "-ov", "-format", "UDZO", "-volname", "MetalVK Development",
        "-srcfolder", str(app.parent), str(build / "MetalVK-development.dmg"))
    print("Built diagnostic-only development artifacts; no GPU acceleration claimed.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError, plistlib.InvalidFileException) as error:
        print(f"Build failed: {error}", file=sys.stderr)
        raise SystemExit(1)
