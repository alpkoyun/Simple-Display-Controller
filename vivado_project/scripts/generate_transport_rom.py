#!/usr/bin/env python3
"""Generate the deterministic 4 KiB non-executable XDMA transport ROM."""

from __future__ import annotations

import hashlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT_DIR = ROOT / "rom"
ROM_BIN = OUTPUT_DIR / "simple_display_transport_rom.bin"
ROM_MEM = OUTPUT_DIR / "simple_display_transport_rom_32.mem"
SHA_FILE = OUTPUT_DIR / "SHA256SUMS"

ROM_SIZE = 4096
PCIR_OFFSET = 0x20
MARKER = b"XDMA-ROM-TEST"
EXPECTED_SHA256 = "4d792f7289cd46328cf9ffdd98074fae64e3ea77e504b7c8e85e3187abd8e983"


def put_u16(image: bytearray, offset: int, value: int) -> None:
    image[offset : offset + 2] = value.to_bytes(2, "little")


def build_image() -> bytes:
    image = bytearray([0xFF] * ROM_SIZE)
    image[0x00:0x40] = bytes(0x40)
    image[0x00:0x02] = b"\x55\xaa"
    image[0x02] = ROM_SIZE // 512
    put_u16(image, 0x18, PCIR_OFFSET)

    image[PCIR_OFFSET : PCIR_OFFSET + 4] = b"PCIR"
    put_u16(image, PCIR_OFFSET + 0x04, 0x10EE)
    put_u16(image, PCIR_OFFSET + 0x06, 0x7024)
    put_u16(image, PCIR_OFFSET + 0x08, 0)
    put_u16(image, PCIR_OFFSET + 0x0A, 0x18)
    image[PCIR_OFFSET + 0x0C] = 0x03
    image[PCIR_OFFSET + 0x0D : PCIR_OFFSET + 0x10] = (
        0x038000
    ).to_bytes(3, "little")
    put_u16(image, PCIR_OFFSET + 0x10, ROM_SIZE // 512)
    put_u16(image, PCIR_OFFSET + 0x12, 0)
    image[PCIR_OFFSET + 0x14] = 0xFF
    image[PCIR_OFFSET + 0x15] = 0x80
    put_u16(image, PCIR_OFFSET + 0x16, 0)

    image[0x40 : 0x40 + len(MARKER)] = MARKER
    image[0x40 + len(MARKER)] = 0
    for offset, marker in (
        (0x060, b"MARK0060"),
        (0x07C, b"MARK007C"),
        (0x0FC, b"MARK00FC"),
        (0x7FC, b"MARK07FC"),
        (0xF7C, b"MARK0F7C"),
    ):
        image[offset : offset + len(marker)] = marker
    image[-4:] = b"END!"
    image[-5] = 0
    image[-5] = (-sum(image)) & 0xFF
    return bytes(image)


def main() -> int:
    image = build_image()
    digest = hashlib.sha256(image).hexdigest()
    if digest != EXPECTED_SHA256:
        raise SystemExit(
            f"generated SHA-256 {digest} does not match pinned {EXPECTED_SHA256}"
        )

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    ROM_BIN.write_bytes(image)
    words = [
        image[offset : offset + 4][::-1].hex()
        for offset in range(0, len(image), 4)
    ]
    ROM_MEM.write_text("\n".join(words) + "\n", encoding="ascii")
    SHA_FILE.write_text(f"{digest}  {ROM_BIN.name}\n", encoding="ascii")

    print(f"ROM_BIN={ROM_BIN}")
    print(f"ROM_MEM={ROM_MEM}")
    print(f"ROM_SIZE={len(image)}")
    print(f"ROM_SHA256={digest}")
    print("SIMPLE_DISPLAY_TRANSPORT_ROM_GENERATION_PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
