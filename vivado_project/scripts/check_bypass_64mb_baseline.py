#!/usr/bin/env python3
"""Fail closed unless the recreate inputs match the uploaded 64 MiB design."""

from __future__ import annotations

import hashlib
import sys
from pathlib import Path


VIVADO_ROOT = Path(__file__).resolve().parents[1]
EXPORT = VIVADO_ROOT / "export/PCIe.tcl"

REQUIRED_EXPORT_TEXT = {
    "64-bit bypass BAR": "CONFIG.axi_bypass_64bit_en {true}",
    "prefetchable bypass BAR": "CONFIG.axi_bypass_prefetchable {true}",
    "64 MiB bypass aperture": "CONFIG.axist_bypass_size {64}",
    "bypass translation": (
        "CONFIG.pciebar2axibar_axist_bypass {0x3c000000}"
    ),
    "XDMA Gen2 link": "CONFIG.pl_link_cap_max_link_speed {5.0_GT/s}",
    "XDMA x4 link": "CONFIG.pl_link_cap_max_link_width {X4}",
    "pixel unpack map": (
        "assign_bd_address -offset 0x3C000000 -range 0x00010000"
    ),
    "VTC map": (
        "assign_bd_address -offset 0x3C010000 -range 0x00010000"
    ),
    "IIC map": (
        "assign_bd_address -offset 0x3C020000 -range 0x00010000"
    ),
    "UART map": (
        "assign_bd_address -offset 0x3C030000 -range 0x00010000"
    ),
    "VDMA register map": (
        "assign_bd_address -offset 0x3C040000 -range 0x00010000"
    ),
    "color-convert map": (
        "assign_bd_address -offset 0x3C050000 -range 0x00010000"
    ),
    "video-clock map": (
        "assign_bd_address -offset 0x3C060000 -range 0x00010000"
    ),
    "video-status map": (
        "assign_bd_address -offset 0x3C070000 -range 0x00010000"
    ),
    "shared DDR map": (
        "assign_bd_address -offset 0x3E000000 -range 0x02000000"
    ),
    "XDMA ILA": "xilinx.com:ip:system_ila:1.1 xdma_ila",
    "local HLS repository": (
        '[file normalize "$origin_dir/../hls"]'
    ),
    "local custom-IP repository": (
        '[file normalize "$origin_dir/../IPs/IP_Packages"]'
    ),
}

FORBIDDEN_BASELINE_TEXT = {
    "cross-domain video ILA": "video_stream_ila",
    "enabled Expansion ROM": "CONFIG.pf0_expansion_rom_enabled {true}",
    "BAR6 translation": "CONFIG.pciebar2axibar_6",
    "ROM module-reference cell": (
        "create_bd_cell -type module -reference simple_display_axi_rom"
    ),
    "identity module-reference cell": (
        "create_bd_cell -type module -reference simple_display_identity_regs"
    ),
    "ROM address segment": (
        "assign_bd_address -offset 0x01000000"
    ),
    "identity address segment": (
        "assign_bd_address -offset 0x3C080000"
    ),
}

PINNED_INPUT_HASHES = {
    "PCIe.srcs/constrs_1/new/PCIe.xdc": (
        "8349de68bb5cdd5b873d5b5d56d8dc7ca832bb8275cd39547e76bb088410e328"
    ),
    (
        "PCIe.srcs/sources_1/bd/PCIe/ip/"
        "PCIe_mig_7series_0_0/mig_a.prj"
    ): "db392c77774caef6c9aad72cc8d3f71350c502cd4bbd58c00434537efa777b32",
    (
        "PCIe.srcs/sources_1/bd/PCIe/ip/"
        "PCIe_mig_7series_0_0/mig_b.prj"
    ): "e03d5dfc9bb1311a005555d7823adfac2a9a5eb0564cce952c0d0251532d9a75",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    errors: list[str] = []
    if not EXPORT.is_file():
        print(f"ERROR: missing canonical Vivado export: {EXPORT}", file=sys.stderr)
        return 1

    text = EXPORT.read_text(encoding="utf-8", errors="strict")
    for description, needle in REQUIRED_EXPORT_TEXT.items():
        if needle not in text:
            errors.append(f"missing {description}: {needle}")
        else:
            print(f"PASS: {description}")

    for description, needle in FORBIDDEN_BASELINE_TEXT.items():
        if needle in text:
            errors.append(f"baseline contains {description}: {needle}")
        else:
            print(f"PASS: baseline excludes {description}")

    for relative, expected in PINNED_INPUT_HASHES.items():
        path = VIVADO_ROOT / relative
        if not path.is_file():
            errors.append(f"missing uploaded baseline input: {path}")
            continue
        actual = sha256(path)
        if actual != expected:
            errors.append(
                f"{relative}: expected SHA-256 {expected}, got {actual}"
            )
        else:
            print(f"PASS: uploaded input hash {relative}")

    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1

    print("SIMPLE_DISPLAY_BYPASS64_SOURCE_CONTRACT_PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
