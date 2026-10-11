#!/usr/bin/env python3
"""Check that the NVIDIA transport standard and the pinned upstream NVK nvkmd
operation surface still agree.

Two independent sides are compared:

  1. upstream: the operations declared in the pinned Mesa nvkmd.h, parsed from
     the ops structs. This is the authority for what NVK will call.
  2. metalvk:  backends/nvidia/nvkmd-alignment.json (the mapping) and the
     MTVK_NV_OP_* catalog in transport.h plus the class table in transport.c.

The check fails on any drift: an upstream op with no mapping, a mapping for an
op that no longer exists, a transport op missing from the catalog, a catalog op
with no mapping, or a class that disagrees between the manifest, the header and
the implementation. It never inspects a running GPU and makes no execution
claim.
"""
from __future__ import annotations
import argparse
import json
import pathlib
import re
import sys

OPS_STRUCT = re.compile(r"struct\s+nvkmd_(\w+)_ops\s*\{")
OP_POINTER = re.compile(r"\(\s*\*\s*(\w+)\s*\)\s*\(")
ENTRY_FUNCTION = re.compile(r"\b(\w+)\s*\(")
OP_ENUM = re.compile(r"MTVK_NV_OP_(\w+)")
CLASS_TABLE = re.compile(r"\[MTVK_NV_OP_(\w+)\]\s*=\s*MTVK_NV_CLASS_(\w+)")


def fail(message: str) -> None:
    print(f"alignment error: {message}", file=sys.stderr)
    raise SystemExit(1)


def upstream_operations(header: pathlib.Path) -> tuple[set[str], set[str]]:
    text = header.read_text(encoding="utf-8", errors="replace")
    operations: set[str] = set()
    for match in OPS_STRUCT.finditer(text):
        name = match.group(1)
        depth = 0
        end = match.end() - 1
        for index in range(match.end() - 1, len(text)):
            if text[index] == "{":
                depth += 1
            elif text[index] == "}":
                depth -= 1
                if depth == 0:
                    end = index
                    break
        block = text[match.end():end]
        found = {f"{name}.{op}" for op in OP_POINTER.findall(block)}
        if not found:
            fail(f"upstream ops struct nvkmd_{name}_ops declared no function pointers")
        operations |= found
    entries = set(ENTRY_FUNCTION.findall(text))
    return operations, entries


def catalog(header: pathlib.Path, source: pathlib.Path) -> tuple[set[str], dict[str, str]]:
    names = set(OP_ENUM.findall(header.read_text(encoding="utf-8", errors="replace")))
    names.discard("COUNT")
    classes = {op: klass.lower() for op, klass in CLASS_TABLE.findall(
        source.read_text(encoding="utf-8", errors="replace"))}
    return names, classes


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=pathlib.Path)
    parser.add_argument("--transport-header", required=True, type=pathlib.Path)
    parser.add_argument("--transport-source", required=True, type=pathlib.Path)
    parser.add_argument("--mesa-header", required=True, type=pathlib.Path)
    parser.add_argument("--mesa-version", required=True)
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    if str(manifest.get("transport_version")) != "1":
        fail("manifest transport_version must match MTVK_NV_TRANSPORT_VERSION")
    if manifest.get("mesa_version") != args.mesa_version:
        fail(f"manifest pins Mesa {manifest.get('mesa_version')}, expected {args.mesa_version}")
    for path in (args.transport_header, args.transport_source, args.mesa_header):
        if not path.is_file():
            fail(f"required input is missing: {path}")

    operations = manifest["operations"]
    for entry in operations:
        if entry["kind"] not in ("op", "entry"):
            fail(f"unknown kind {entry['kind']!r} for {entry['upstream']}")

    upstream_ops, upstream_functions = upstream_operations(args.mesa_header)
    mapped_ops = {e["upstream"] for e in operations if e["kind"] == "op"}
    mapped_entries = [e for e in operations if e["kind"] == "entry"]

    if mapped_ops != upstream_ops:
        fail(f"op mapping drift; missing={sorted(upstream_ops - mapped_ops)} "
             f"extra={sorted(mapped_ops - upstream_ops)}")
    if len(mapped_entries) != 1:
        fail("exactly one discovery entry mapping is required")
    if mapped_entries[0]["upstream"] != manifest["discovery_entry"]:
        fail("discovery entry mapping disagrees with discovery_entry")
    if manifest["discovery_entry"] not in upstream_functions:
        fail(f"discovery entry {manifest['discovery_entry']} is absent from the pinned header")

    names, classes = catalog(args.transport_header, args.transport_source)
    mapped_transport = {e["transport"] for e in operations if e["transport"] is not None}
    if mapped_transport != names:
        fail(f"transport catalog drift; unmapped={sorted(names - mapped_transport)} "
             f"unknown={sorted(mapped_transport - names)}")
    for name in names:
        if name not in classes:
            fail(f"transport.c has no class table entry for {name}")

    # Several upstream ops may share one transport op (e.g. the two Linux fd
    # accessors collapse into DRM_FD); the class must still agree everywhere.
    for entry in operations:
        transport = entry.get("transport")
        if transport is None:
            continue
        if classes[transport] != entry["class"]:
            fail(f"{entry['upstream']}: manifest class {entry['class']} != "
                 f"transport.c class {classes[transport]}")

    print(f"PASS nvkmd alignment: {len(upstream_ops)} upstream ops, "
          f"{len(names)} transport ops, Mesa {manifest['mesa_version']}, "
          f"transport version {manifest['transport_version']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
