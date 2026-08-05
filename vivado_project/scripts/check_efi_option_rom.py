#!/usr/bin/env python3
"""Validate the complete 32 KiB Simple Display X64 EFI Option ROM."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"FAIL: {message}")
    print(f"PASS: {message}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--driver", type=Path, required=True)
    args = parser.parse_args()

    image = args.image.read_bytes()
    driver = args.driver.read_bytes()
    require(len(image) == 32768, "image fills the 32 KiB aperture")
    require(image[:2] == b"\x55\xaa", "PCI ROM signature is 55aa")
    require(int.from_bytes(image[2:4], "little") == 64, "EFI header image length")
    require(int.from_bytes(image[4:8], "little") == 0x0EF1, "EFI ROM signature")
    require(int.from_bytes(image[8:10], "little") == 0x000B, "boot-service-driver subsystem")
    require(int.from_bytes(image[10:12], "little") == 0x8664, "X64 machine type")
    require(int.from_bytes(image[12:14], "little") == 0, "image is uncompressed")

    efi_offset = int.from_bytes(image[0x16:0x18], "little")
    pcir = int.from_bytes(image[0x18:0x1A], "little")
    require(image[pcir : pcir + 4] == b"PCIR", "PCIR signature")
    require(
        int.from_bytes(image[pcir + 4 : pcir + 6], "little") == 0x10EE
        and int.from_bytes(image[pcir + 6 : pcir + 8], "little") == 0x7024,
        "PCIR identity is 10ee:7024",
    )
    require(
        int.from_bytes(image[pcir + 0x0D : pcir + 0x10], "little")
        == 0x038000,
        "PCIR class code is 0x038000",
    )
    require(
        int.from_bytes(image[pcir + 0x10 : pcir + 0x12], "little") == 64,
        "PCIR image length fills 32 KiB",
    )
    require(image[pcir + 0x14] == 0x03, "PCIR code type is EFI")
    require(image[pcir + 0x15] & 0x80 != 0, "last-image flag is set")
    require(sum(image) % 256 == 0, "whole-image checksum is zero")
    require(
        image[efi_offset : efi_offset + len(driver)] == driver,
        "embedded driver is byte-exact and uncompressed",
    )

    require(image[efi_offset : efi_offset + 2] == b"MZ", "embedded PE DOS signature")
    pe_offset = int.from_bytes(
        image[efi_offset + 0x3C : efi_offset + 0x40], "little"
    )
    pe = efi_offset + pe_offset
    require(image[pe : pe + 4] == b"PE\0\0", "embedded PE signature")
    require(int.from_bytes(image[pe + 4 : pe + 6], "little") == 0x8664, "embedded PE is X64")
    optional = pe + 24
    require(int.from_bytes(image[optional : optional + 2], "little") == 0x20B, "embedded PE32+")
    require(
        int.from_bytes(image[optional + 0x44 : optional + 0x46], "little")
        == 0x000B,
        "embedded PE subsystem is EFI boot-service driver",
    )

    digest = hashlib.sha256(image).hexdigest()
    print(f"OPTION_ROM_SHA256={digest}")
    print("SIMPLE_DISPLAY_EFI_OPTION_ROM_CONTRACT_PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
