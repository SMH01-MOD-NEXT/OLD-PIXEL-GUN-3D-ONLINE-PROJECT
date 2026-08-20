#!/usr/bin/env python3
"""Annotate OPG3D caller RVAs with managed method names from an IL2CPP dump.cs.

The native logger writes call sites as ``pc=libil2cpp.so+0x...``. This tool
builds a sorted RVA index from dump.cs and appends the nearest containing
managed method to each matching log line. No game binaries or credentials are
read, copied, or uploaded.
"""

from __future__ import annotations

import argparse
import bisect
import dataclasses
import pathlib
import re
import sys
from collections.abc import Iterable, Iterator

RVA_RE = re.compile(r"//\s*RVA:\s*0x([0-9A-Fa-f]+)\b")
NAMESPACE_RE = re.compile(r"^//\s*Namespace:\s*(.*)$")
TYPE_RE = re.compile(
    r"^(?:(?:public|private|protected|internal)\s+)?"
    r"(?:(?:static|sealed|abstract|partial)\s+)*"
    r"(?:class|struct|interface|enum)\s+([^:{]+?)\s*(?::|$)"
)
CALLSITE_RE = re.compile(r"pc=libil2cpp\.so\+0x([0-9A-Fa-f]+)\b")
ANNOTATION_RE = re.compile(r"\s+\[managed=[^\]]+\]\s*$")


@dataclasses.dataclass(frozen=True, order=True)
class Method:
    rva: int
    qualified_name: str


def _method_name_from_declaration(line: str) -> str | None:
    stripped = line.strip()
    if not stripped or stripped.startswith(("//", "[")) or "(" not in stripped:
        return None
    prefix = stripped.split("(", 1)[0].rstrip()
    if not prefix:
        return None
    candidate = prefix.rsplit(None, 1)[-1]
    if candidate in {"if", "for", "while", "switch", "catch"}:
        return None
    return candidate


def parse_dump(lines: Iterable[str]) -> list[Method]:
    namespace = ""
    current_type = "<unknown-type>"
    pending_rva: int | None = None
    methods: list[Method] = []

    for raw_line in lines:
        line = raw_line.rstrip("\n")

        namespace_match = NAMESPACE_RE.match(line)
        if namespace_match:
            namespace = namespace_match.group(1).strip()
            pending_rva = None
            continue

        type_match = TYPE_RE.match(line.strip())
        if type_match:
            # Il2CppDumper appends comments such as `// TypeDefIndex: 4860`
            # to type declarations. They are metadata, not part of the name.
            current_type = type_match.group(1).split("//", 1)[0].strip()
            pending_rva = None
            continue

        rva_match = RVA_RE.search(line)
        if rva_match:
            pending_rva = int(rva_match.group(1), 16)
            continue

        if pending_rva is None:
            continue

        method_name = _method_name_from_declaration(line)
        if method_name is None:
            # Attributes and blank lines may occur between RVA and declaration.
            # A new comment that is not another RVA invalidates the candidate.
            if line.strip().startswith("//"):
                pending_rva = None
            continue

        owner = f"{namespace}.{current_type}" if namespace else current_type
        methods.append(Method(pending_rva, f"{owner}.{method_name}"))
        pending_rva = None

    # Generic sharing can give several managed methods one native address.
    # Keep all names, but collapse exact duplicate dump entries.
    return sorted(set(methods))


class RvaIndex:
    def __init__(self, methods: Iterable[Method]) -> None:
        grouped: dict[int, list[str]] = {}
        for method in methods:
            grouped.setdefault(method.rva, []).append(method.qualified_name)
        self._rvas = sorted(grouped)
        self._names = {rva: sorted(set(names)) for rva, names in grouped.items()}

    def resolve(self, rva: int, max_offset: int) -> str | None:
        position = bisect.bisect_right(self._rvas, rva) - 1
        if position < 0:
            return None
        start = self._rvas[position]
        offset = rva - start
        if offset > max_offset:
            return None
        names = self._names[start]
        rendered = "|".join(names[:3])
        if len(names) > 3:
            rendered += f"|+{len(names) - 3} more"
        return f"{rendered}+0x{offset:x}"


def symbolize_lines(
    lines: Iterable[str], index: RvaIndex, max_offset: int
) -> Iterator[str]:
    for raw_line in lines:
        line = raw_line.rstrip("\n")
        match = CALLSITE_RE.search(line)
        if match and not ANNOTATION_RE.search(line):
            resolved = index.resolve(int(match.group(1), 16), max_offset)
            if resolved:
                line += f" [managed={resolved}]"
        yield line + "\n"


def parse_int(value: str) -> int:
    return int(value, 0)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump", required=True, type=pathlib.Path,
                        help="Path to the matching IL2CPP dump.cs")
    parser.add_argument("--log", type=pathlib.Path,
                        help="Log file to annotate; stdin when omitted")
    parser.add_argument("--output", type=pathlib.Path,
                        help="Output file; stdout when omitted")
    parser.add_argument("--max-offset", type=parse_int, default=0x10000,
                        help="Maximum distance from a method RVA (default: 0x10000)")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.max_offset < 0:
        raise SystemExit("--max-offset must be non-negative")

    with args.dump.open("r", encoding="utf-8-sig", errors="replace") as dump_file:
        index = RvaIndex(parse_dump(dump_file))

    input_file = args.log.open("r", encoding="utf-8", errors="replace") if args.log else sys.stdin
    output_file = args.output.open("w", encoding="utf-8") if args.output else sys.stdout
    try:
        output_file.writelines(symbolize_lines(input_file, index, args.max_offset))
    finally:
        if args.log:
            input_file.close()
        if args.output:
            output_file.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
