#!/usr/bin/env python3
"""Find every A64 BL/B call site that targets a given RVA in an IL2CPP image.

The 23.1.3 arm64 ``libil2cpp.so`` is a stripped ELF with no relocations for
internal managed calls, so a symbolic cross-reference is not available. Every
managed-to-managed call is a direct ``BL`` (or a tail ``B``) with a 26-bit
PC-relative immediate, which means the call graph can be recovered exactly by
decoding those two opcodes over the whole image.

For this build ``RVA == file offset`` (single PT_LOAD, no vaddr skew), which the
script verifies against the ELF program headers before it scans, so a future
build with a different layout fails loudly instead of printing wrong addresses.

Usage:
    python3 tools/find_callers.py libil2cpp.so 0x15ADB84 [0x1234 ...]
"""

from __future__ import annotations

import argparse
import struct
import sys


def load_image(path: str) -> bytes:
    with open(path, "rb") as handle:
        return handle.read()


def check_identity_mapping(image: bytes) -> None:
    """Verify RVA == file offset for every executable PT_LOAD segment."""
    if image[:4] != b"\x7fELF" or image[4] != 2:
        raise SystemExit("not a 64-bit ELF")
    e_phoff = struct.unpack_from("<Q", image, 0x20)[0]
    e_phentsize = struct.unpack_from("<H", image, 0x36)[0]
    e_phnum = struct.unpack_from("<H", image, 0x38)[0]
    for index in range(e_phnum):
        base = e_phoff + index * e_phentsize
        p_type, p_flags = struct.unpack_from("<II", image, base)
        if p_type != 1:  # PT_LOAD
            continue
        p_offset, p_vaddr = struct.unpack_from("<QQ", image, base + 0x08)
        if p_flags & 0x1 and p_offset != p_vaddr:
            raise SystemExit(
                "executable segment is not identity mapped "
                f"(offset 0x{p_offset:X} != vaddr 0x{p_vaddr:X}); "
                "this script's RVA == file offset assumption does not hold"
            )


def sign_extend_26(value: int) -> int:
    return value - (1 << 26) if value & (1 << 25) else value


def find_callers(image: bytes, targets: set[int]) -> dict[int, list[tuple[int, str]]]:
    """Decode every BL/B in the image and collect the ones hitting a target."""
    found: dict[int, list[tuple[int, str]]] = {target: [] for target in targets}
    limit = len(image) & ~3
    words = struct.unpack_from("<%dI" % (limit // 4), image, 0)
    for index, word in enumerate(words):
        opcode = word >> 26
        # 0b100101 = BL (call), 0b000101 = B (tail call).
        if opcode != 0x25 and opcode != 0x05:
            continue
        pc = index * 4
        target = pc + sign_extend_26(word & 0x03FFFFFF) * 4
        if target in found:
            found[target].append((pc, "bl" if opcode == 0x25 else "b"))
    return found


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", help="path to libil2cpp.so")
    parser.add_argument("targets", nargs="+", help="target RVAs, e.g. 0x15ADB84")
    args = parser.parse_args(argv)

    image = load_image(args.image)
    check_identity_mapping(image)

    targets = {int(value, 16) for value in args.targets}
    found = find_callers(image, targets)

    for target in sorted(found):
        sites = found[target]
        print("target 0x%08X: %d call site(s)" % (target, len(sites)))
        for pc, kind in sites:
            print("    0x%08X  %s" % (pc, kind))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
