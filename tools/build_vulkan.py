#!/usr/bin/env python3
"""Build the real-ICD compute integration test with an installed Vulkan SDK."""
from __future__ import annotations
import argparse
import pathlib
import shutil
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", required=True, type=pathlib.Path)
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--vendor", default="0x10de")
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parent.parent
    sdk = args.sdk.resolve()
    include = sdk / ("Include" if sys.platform == "win32" else "include")
    lib = sdk / ("Lib" if sys.platform == "win32" else "lib")
    if not (include / "vulkan/vulkan.h").is_file():
        parser.error("SDK path must contain its platform's include/vulkan/vulkan.h")
    compiler = shutil.which("clang")
    glslang = shutil.which("glslangValidator")
    validator = shutil.which("spirv-val")
    archive = shutil.which("llvm-ar") or shutil.which("ar")
    if not all((compiler, glslang, validator, archive)):
        parser.error("clang, glslangValidator, spirv-val and llvm-ar/ar are required on PATH")
    build = root / "target/vulkan"
    build.mkdir(parents=True, exist_ok=True)
    shader = build / "readback.spv"
    subprocess.run([glslang, "-V", "--target-env", "vulkan1.2", "-o", str(shader),
        str(root / "validation/vulkan/shaders/readback.comp")], check=True)
    subprocess.run([validator, "--target-env", "vulkan1.2", str(shader)], check=True)
    exe = build / ("vulkan-compute.exe" if sys.platform == "win32" else "vulkan-compute")
    library = [str(lib / "vulkan-1.lib")] if sys.platform == "win32" else ["-L", str(lib), "-lvulkan"]
    flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
        "-Wconversion", "-Wshadow", "-isystem", str(include),
        "-I", str(root / "include"),
        "-I", str(root / "validation/vulkan")]
    context_object = build / "context.o"
    context_library = build / "libmetalvk-vulkan.a"
    subprocess.run([compiler, *flags, "-c", str(root / "validation/vulkan/context.c"),
        "-o", str(context_object)], check=True)
    subprocess.run([archive, "rcs", str(context_library), str(context_object)], check=True)
    subprocess.run([compiler, *flags, str(root / "validation/vulkan/compute.c"),
        str(context_library), *library, "-o", str(exe)], check=True)
    if args.run:
        subprocess.run([str(exe), str(shader), args.vendor], check=True, timeout=60)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, subprocess.SubprocessError) as error:
        print(f"Vulkan integration failed: {error}", file=sys.stderr)
        raise SystemExit(1)
