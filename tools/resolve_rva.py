#!/usr/bin/env python3
"""Resolve raw IL2CPP RVAs to the managed methods that contain them.

``tools/find_callers.py`` recovers call sites as bare addresses. This tool turns
those addresses into ``Type.Method (+0xNN)`` using the RVA index that
``tools/symbolize_log.py`` already builds from ``dump.cs``, so both tools stay
consistent and there is only one dump parser in the repository.

Usage:
    python3 tools/resolve_rva.py analys2313/dump2313.cs 0x267C8A0 0x44743EC
"""

from __future__ import annotations

import argparse
import bisect
import io
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from symbolize_log import parse_dump  # noqa: E402


def build_index(dump_path: str) -> tuple[list[int], list[str]]:
    with io.open(dump_path, encoding="utf-8", errors="replace") as handle:
        methods = sorted(parse_dump(handle))
    return [m.rva for m in methods], [m.qualified_name for m in methods]


def resolve(rvas: list[int], names: list[str], address: int) -> tuple[str, int]:
    index = bisect.bisect_right(rvas, address) - 1
    if index < 0:
        return "<below first method>", 0
    return names[index], address - rvas[index]


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dump", help="path to dump.cs")
    parser.add_argument("addresses", nargs="+", help="RVAs, e.g. 0x267C8A0")
    args = parser.parse_args(argv)

    rvas, names = build_index(args.dump)
    print("indexed methods: %d" % len(rvas))
    for raw in args.addresses:
        address = int(raw, 16)
        name, offset = resolve(rvas, names, address)
        print("0x%08X  ->  %s  (+0x%X)" % (address, name, offset))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
