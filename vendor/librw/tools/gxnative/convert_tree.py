#!/usr/bin/env python3
"""Bulk-convert extracted GTA DFFs to librw GX-native geometry.

Usage:
    python convert_tree.py /path/to/gxnative input_dir output_dir

Non-DFF files are copied verbatim. DFFs are passed through gxnative. The input
and output trees must be different so the original assets always remain intact.
"""
from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 4:
        print(f"usage: {Path(sys.argv[0]).name} gxnative input_dir output_dir", file=sys.stderr)
        return 1

    converter = Path(sys.argv[1]).resolve()
    source = Path(sys.argv[2]).resolve()
    destination = Path(sys.argv[3]).resolve()

    if not converter.is_file():
        print(f"converter not found: {converter}", file=sys.stderr)
        return 1
    if not source.is_dir():
        print(f"input directory not found: {source}", file=sys.stderr)
        return 1
    if source == destination or source in destination.parents:
        print("output_dir must be outside input_dir", file=sys.stderr)
        return 1

    converted = 0
    warnings = 0
    copied = 0
    for src in source.rglob("*"):
        if not src.is_file():
            continue
        rel = src.relative_to(source)
        dst = destination / rel
        dst.parent.mkdir(parents=True, exist_ok=True)

        if src.suffix.lower() != ".dff":
            shutil.copy2(src, dst)
            copied += 1
            continue

        result = subprocess.run([str(converter), str(src), str(dst)])
        if result.returncode == 0:
            converted += 1
        elif result.returncode == 2 and dst.exists():
            # gxnative writes all convertible geometries and uses 2 to report
            # one or more geometry-level failures. Preserve the output but make
            # the partial conversion visible to the caller.
            converted += 1
            warnings += 1
        else:
            print(f"failed: {rel}", file=sys.stderr)
            return result.returncode or 1

    print(f"tree conversion complete: dff={converted} warnings={warnings} copied={copied}")
    return 2 if warnings else 0


if __name__ == "__main__":
    raise SystemExit(main())
