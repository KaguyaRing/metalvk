#!/usr/bin/env python3
"""Check the source and archive boundary of the SDK-free generic layer."""
from __future__ import annotations
import argparse
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CORE_SOURCES = ("src/base/status.c",)
CORE_HEADERS = ("include/platform.h", "include/metalvk.h",
                "include/base/init.h", "include/base/span.h", "include/base/status.h")
STANDARD_HEADERS = {"stdbool.h", "stddef.h", "stdint.h", "stdatomic.h", "string.h"}
# BSD/Darwin ar exposes its reserved symbol-index member in `ar t` output.
ARCHIVE_INDEX_MEMBERS = {"__.SYMDEF", "__.SYMDEF SORTED", "__.SYMDEF_64", "__.SYMDEF_64 SORTED"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=pathlib.Path)
    args = parser.parse_args()
    expected = set(CORE_SOURCES)
    makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
    matched = re.search(r"^SOURCES\s*:=\s*(.+)$", makefile, re.MULTILINE)
    if not matched or set(matched.group(1).split()) != expected:
        raise ValueError("Default library source list escaped the approved core boundary")
    local_headers = {p.removeprefix("include/") for p in CORE_HEADERS}
    for relative in (*CORE_SOURCES, *CORE_HEADERS):
        code = (ROOT / relative).read_text(encoding="utf-8")
        code = re.sub(r"/\*.*?\*/|//[^\n]*", "", code, flags=re.DOTALL)
        for delimiter, name in re.findall(r'^\s*#\s*include\s*([<"])([^>"\n]+)[>"]', code, re.MULTILINE):
            allowed = STANDARD_HEADERS if delimiter == "<" else local_headers
            if name not in allowed:
                raise ValueError(f"Unexpected core dependency: {relative}: {name}")
        forbidden = r"\b(?:NVIDIA|AMD|Intel|CUDA|NVK|NVKMS|GSP|MoltenVK|IOPCIDevice)\b|\bioctl\s*\("
        if re.search(forbidden, code, re.IGNORECASE) or re.search(r"\b(?:vk|nv|cu)[A-Z]\w*\s*\(", code):
            raise ValueError(f"Implementation-specific code in generic layer: {relative}")
        if re.search(r"^\s*#\s*(?:if|ifdef|ifndef).*\b(?:_WIN32|__APPLE__|__linux__|VK_USE_PLATFORM)\b", code, re.MULTILINE):
            raise ValueError(f"Platform/driver condition inside generic implementation: {relative}")
    if args.archive:
        archive = shutil.which("llvm-ar") or shutil.which("ar")
        if not archive:
            raise ValueError("llvm-ar/ar is needed to inspect archive membership")
        members = subprocess.check_output([archive, "t", str(args.archive)], text=True).splitlines()
        objects = [m for m in members if m not in ARCHIVE_INDEX_MEMBERS]
        if sorted(pathlib.PurePosixPath(m).name for m in objects) != ["status.o"]:
            raise ValueError(f"Unexpected archive members: {members}")
    print("Generic core boundary verified: standard C headers, isolated sources/archive")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"Core boundary check failed: {error}", file=sys.stderr)
        raise SystemExit(1)
