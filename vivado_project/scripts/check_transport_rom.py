#!/usr/bin/env python3
"""Validate the pinned Simple Display non-executable transport ROM."""

from __future__ import annotations

import hashlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ROM_BIN = ROOT / "rom/simple_display_transport_rom.bin"
ROM_MEM = ROOT / "rom/simple_display_transport_rom_32.mem"
EXPECTED_SHA256 = "4d792f7289cd46328cf9ffdd98074fae64e3ea77e504b7c8e85e3187abd8e983"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"FAIL: {message}")
    print(f"PASS: {message}")


def main() -> int:
    image = ROM_BIN.read_bytes()
    lines = ROM_MEM.read_text(encoding="ascii").splitlines()
    pcir = int.from_bytes(image[0x18:0x1A], "little")

    require(len(image) == 4096, "ROM is exactly 4096 bytes")
    require(image[:2] == b"\x55\xaa", "PCI ROM signature is 55aa")
    require(image[2] == 8, "header length is eight 512-byte blocks")
    require(pcir == 0x20 and image[pcir : pcir + 4] == b"PCIR", "PCIR header")
    require(
        int.from_bytes(image[pcir + 4 : pcir + 6], "little") == 0x10EE
        and int.from_bytes(image[pcir + 6 : pcir + 8], "little") == 0x7024,
        "PCI identity is 10ee:7024",
    )
    require(
        int.from_bytes(image[pcir + 0x0A : pcir + 0x0C], "little") == 0x18,
        "PCIR structure length is 0x18",
    )
    require(
        int.from_bytes(image[pcir + 0x10 : pcir + 0x12], "little") == 8,
        "PCIR image length is eight blocks",
    )
    require(image[pcir + 0x14] == 0xFF, "code type is reserved non-executable 0xff")
    require(image[pcir + 0x15] == 0x80, "last-image flag is set")
    require(sum(image) % 256 == 0, "whole-image checksum is zero")
    require(image[0x40:0x4D] == b"XDMA-ROM-TEST", "identity marker")
    require(image[-4:] == b"END!", "end marker")

    require(
        len(lines) == 1024
        and all(len(line) == 8 for line in lines)
        and bytes.fromhex(lines[0])[::-1] == image[:4],
        "32-bit little-endian memory initialization matches binary",
    )
    digest = hashlib.sha256(image).hexdigest()
    require(digest == EXPECTED_SHA256, "ROM SHA-256 matches pinned contract")
    print(f"ROM_SHA256={digest}")
    print("SIMPLE_DISPLAY_TRANSPORT_ROM_CONTRACT_PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
