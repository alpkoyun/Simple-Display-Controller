#!/usr/bin/env python3
"""Resize EfiRom output to 32 KiB, checksum it, and emit 32-bit ROM memory."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


ROM_SIZE = 32 * 1024
ROM_BLOCKS = ROM_SIZE // 512


def put_u16(image: bytearray, offset: int, value: int) -> None:
    image[offset : offset + 2] = value.to_bytes(2, "little")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--efirom-output", type=Path, required=True)
    parser.add_argument("--driver", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    raw = args.efirom_output.read_bytes()
    driver = args.driver.read_bytes()
    if len(raw) > ROM_SIZE:
        raise SystemExit(f"EfiRom output is {len(raw)} bytes, exceeds 32 KiB")
    if raw[:2] != b"\x55\xaa":
        raise SystemExit("EfiRom output lacks the 55aa signature")

    pcir = int.from_bytes(raw[0x18:0x1A], "little")
    efi_image = int.from_bytes(raw[0x16:0x18], "little")
    if raw[pcir : pcir + 4] != b"PCIR":
        raise SystemExit("EfiRom output lacks a valid PCIR structure")
    if raw[efi_image : efi_image + len(driver)] != driver:
        raise SystemExit("uncompressed driver is not byte-exact in EfiRom output")

    image = bytearray(raw)
    image.extend(b"\xff" * (ROM_SIZE - len(image)))
    put_u16(image, 0x02, ROM_BLOCKS)
    put_u16(image, pcir + 0x10, ROM_BLOCKS)
    image[-1] = 0
    image[-1] = (-sum(image)) & 0xFF

    args.output_dir.mkdir(parents=True, exist_ok=True)
    bin_path = args.output_dir / "simple_display_gop_option_rom.bin"
    mem_path = args.output_dir / "simple_display_gop_option_rom_32.mem"
    sha_path = args.output_dir / "GOP_SHA256SUMS"
    metadata_path = args.output_dir / "gop_option_rom_metadata.json"

    bin_path.write_bytes(image)
    words = [
        image[offset : offset + 4][::-1].hex()
        for offset in range(0, len(image), 4)
    ]
    mem_path.write_text("\n".join(words) + "\n", encoding="ascii")

    digest = hashlib.sha256(image).hexdigest()
    driver_digest = hashlib.sha256(driver).hexdigest()
    sha_path.write_text(
        f"{digest}  {bin_path.name}\n"
        f"{driver_digest}  {args.driver.name}\n",
        encoding="ascii",
    )
    metadata = {
        "aperture_bytes": ROM_SIZE,
        "driver_bytes": len(driver),
        "driver_sha256": driver_digest,
        "efirom_unpadded_bytes": len(raw),
        "option_rom_sha256": digest,
        "word_bits": 32,
    }
    metadata_path.write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    print(f"OPTION_ROM_BIN={bin_path}")
    print(f"OPTION_ROM_MEM={mem_path}")
    print(f"OPTION_ROM_SIZE={len(image)}")
    print(f"OPTION_ROM_SHA256={digest}")
    print("SIMPLE_DISPLAY_EFI_OPTION_ROM_FINALIZE_PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
