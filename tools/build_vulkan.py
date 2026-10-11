#!/usr/bin/env python3
"""Build the real-ICD Vulkan integration tests with an installed Vulkan SDK.

Builds two hardware probes that share the same context:
  * vulkan-compute: compute dispatch and readback.
  * vulkan-render:  offscreen graphics pipeline and pixel readback.

Run either with --run / --run-render.
"""
from __future__ import annotations
import argparse
import pathlib
import shutil
import subprocess
import sys


def resolve_sdk(parser: argparse.ArgumentParser, sdk: pathlib.Path) -> tuple[pathlib.Path, pathlib.Path]:
    include = sdk / ("Include" if sys.platform == "win32" else "include")
    lib = sdk / ("Lib" if sys.platform == "win32" else "lib")
    if not (include / "vulkan/vulkan.h").is_file():
        parser.error("SDK path must contain its platform's include/vulkan/vulkan.h")
    return include, lib


def compile_shader(glslang: str, validator: str, source: pathlib.Path, spv: pathlib.Path) -> None:
    subprocess.run([glslang, "-V", "--target-env", "vulkan1.2", "-o", str(spv), str(source)], check=True)
    subprocess.run([validator, "--target-env", "vulkan1.2", str(spv)], check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", required=True, type=pathlib.Path)
    parser.add_argument("--run", action="store_true", help="run the compute probe")
    parser.add_argument("--run-render", action="store_true", help="run the graphics-pipeline probe")
    parser.add_argument("--vendor", default="0x10de")
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parent.parent
    include, lib = resolve_sdk(parser, args.sdk.resolve())
    compiler = shutil.which("clang")
    glslang = shutil.which("glslangValidator")
    validator = shutil.which("spirv-val")
    archive = shutil.which("llvm-ar") or shutil.which("ar")
    if not all((compiler, glslang, validator, archive)):
        parser.error("clang, glslangValidator, spirv-val and llvm-ar/ar are required on PATH")

    build = root / "target/vulkan"
    build.mkdir(parents=True, exist_ok=True)
    shaders = build / "shaders"
    shaders.mkdir(parents=True, exist_ok=True)
    compute_spv = shaders / "readback.spv"
    vert_spv = shaders / "pattern.vert.spv"
    frag_spv = shaders / "pattern.frag.spv"
    compile_shader(glslang, validator, root / "validation/vulkan/shaders/readback.comp", compute_spv)
    compile_shader(glslang, validator, root / "validation/vulkan/shaders/pattern.vert", vert_spv)
    compile_shader(glslang, validator, root / "validation/vulkan/shaders/pattern.frag", frag_spv)

    exe_suffix = ".exe" if sys.platform == "win32" else ""
    library = [str(lib / "vulkan-1.lib")] if sys.platform == "win32" else ["-L", str(lib), "-lvulkan"]
    flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
             "-Wconversion", "-Wshadow", "-isystem", str(include),
             "-I", str(root / "include"), "-I", str(root / "validation/vulkan"),
             "-I", str(root / "validation/common")]

    context_library = build / "libmetalvk-vulkan.a"
    subprocess.run([compiler, *flags, "-c", str(root / "src/vk/context.c"),
                    "-o", str(build / "context.o")], check=True)
    subprocess.run([archive, "rcs", str(context_library), str(build / "context.o")], check=True)

    compute_exe = build / ("vulkan-compute" + exe_suffix)
    render_exe = build / ("vulkan-render" + exe_suffix)
    test_exe = build / ("test-vulkan-inputs" + exe_suffix)
    subprocess.run([compiler, *flags, str(root / "tests/test_vulkan_inputs.c"),
                    str(root / "validation/vulkan/input.c"), str(root / "validation/common/pattern.c"),
                    "-o", str(test_exe)], check=True)
    subprocess.run([str(test_exe), str(compute_spv)], check=True, timeout=60)
    subprocess.run([compiler, *flags, str(root / "validation/vulkan/compute.c"),
                    str(root / "validation/vulkan/input.c"),
                    str(context_library), *library, "-o", str(compute_exe)], check=True)
    subprocess.run([compiler, *flags, str(root / "validation/vulkan/render.c"),
                    str(root / "validation/vulkan/input.c"), str(root / "validation/common/pattern.c"),
                    str(context_library), *library, "-o", str(render_exe)], check=True)
    faults_exe = build / ("test-vulkan-render-faults" + exe_suffix)
    subprocess.run([compiler, *flags, "-Dmain=mtvk_render_cli_main", "-include",
                    str(root / "tests/vulkan_render_faults.h"), "-c",
                    str(root / "validation/vulkan/render.c"), "-o", str(build / "render-faults.o")], check=True)
    subprocess.run([compiler, *flags, str(root / "tests/test_vulkan_render_faults.c"),
                    str(build / "render-faults.o"), str(root / "validation/vulkan/input.c"),
                    str(root / "validation/common/pattern.c"), str(context_library), *library,
                    "-o", str(faults_exe)], check=True)

    if args.run:
        subprocess.run([str(compute_exe), str(compute_spv), args.vendor], check=True, timeout=60)
    if args.run_render:
        subprocess.run([str(render_exe), str(vert_spv), str(frag_spv), args.vendor], check=True, timeout=60)
        for fault in ("allocation-failure", "wait-timeout", "drain-failure"):
            subprocess.run([str(faults_exe), str(vert_spv), str(frag_spv), args.vendor, fault],
                           check=True, timeout=60)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, subprocess.SubprocessError) as error:
        print(f"Vulkan integration failed: {error}", file=sys.stderr)
        raise SystemExit(1)
