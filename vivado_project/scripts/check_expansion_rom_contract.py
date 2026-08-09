#!/usr/bin/env python3
"""Check the fixed 32 KiB EFI/GOP XDMA Expansion-ROM configuration."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parent
EXPORT = ROOT / "export/PCIe.tcl"
INTEGRATION_TCL = ROOT / "scripts/integrate_expansion_rom_bd.tcl"
CONFIGURATION_TCL = ROOT / "scripts/configure_xdma_expansion_rom.tcl"
ROM_RTL = ROOT / "rtl/simple_display_axi_rom.sv"
IDENTITY_RTL = ROOT / "rtl/simple_display_identity_regs.sv"
EFI_BIN = ROOT / "rom/simple_display_gop_option_rom.bin"
EFI_MEM = ROOT / "rom/simple_display_gop_option_rom_32.mem"
ROM_SIZE_KIB = 32


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"FAIL: {message}")
    print(f"PASS: {message}")


def parameters(path: Path) -> dict[str, object]:
    document = json.loads(path.read_text(encoding="utf-8"))
    groups = document["ip_inst"]["parameters"]
    merged: dict[str, object] = {}
    for group in groups.values():
        for key, records in group.items():
            merged[key] = records[0]["value"]
    return merged


def component_reference(path: Path) -> str:
    document = json.loads(path.read_text(encoding="utf-8"))
    return str(document["ip_inst"].get("component_reference", ""))


def single(paths: list[Path], label: str) -> Path:
    require(len(paths) == 1, f"one active {label} exists (found {len(paths)})")
    return paths[0]


def source_contract() -> None:
    size_kib = ROM_SIZE_KIB
    export = EXPORT.read_text(encoding="utf-8")
    integration = INTEGRATION_TCL.read_text(encoding="utf-8")
    configuration = CONFIGURATION_TCL.read_text(encoding="utf-8")
    rom_rtl = ROM_RTL.read_text(encoding="utf-8")

    for fragment, description in (
        ("CONFIG.axi_bypass_64bit_en {true}", "64-bit BAR2 bypass"),
        ("CONFIG.axi_bypass_prefetchable {true}", "prefetchable BAR2"),
        ("CONFIG.axist_bypass_size {64}", "64 MiB BAR2 aperture"),
        (
            "CONFIG.pciebar2axibar_axist_bypass {0x3c000000}",
            "BAR2 translation is 0x3c000000",
        ),
        (
            "assign_bd_address -offset 0x3E000000 -range 0x02000000",
            "shared 32 MiB DDR map remains at 0x3e000000",
        ),
        (
            "CONFIG.NUM_MI {9}",
            "validated display/control interconnect remains nine-output",
        ),
    ):
        require(fragment in export, description)
    require(
        "video_stream_ila" not in export,
        "cross-domain video ILA is absent from the production GOP checkpoint",
    )
    require(
        export.count("CONFIG.C_INPUT_PIPE_STAGES {2}") == 1,
        "the XDMA debug probe uses two input pipeline stages",
    )

    for fragment, description in (
        ("set size_kib 32", "outer and nested apertures are fixed to 32 KiB"),
        (
            "CONFIG.pf0_expansion_rom_type {Bypass_AXI_Master}",
            "Expansion ROM uses bypass master",
        ),
        (
            "CONFIG.pciebar2axibar_6 {0x0000000001000000}",
            "BAR ID 6 translation is 0x01000000",
        ),
        (
            "set rom_range [format \"0x%08X\" [expr {$size_kib * 1024}]]",
            "ROM AXI segment follows the fixed aperture",
        ),
    ):
        require(fragment in configuration, description)

    for fragment, description in (
        (
            "[get_bd_intf_pins $splitter_name/M01_AXI]",
            "splitter M01_AXI is dedicated to the ROM endpoint",
        ),
        (
            "assign_bd_address -offset 0x01000000 -range 0x00008000",
            "32 KiB ROM AXI segment starts at 0x01000000",
        ),
        (
            "CONFIG.NUM_MI {3}",
            "same-clock Expansion ROM splitter has three outputs",
        ),
        (
            "set_property CONFIG.NUM_MI {9} $bypass_interconnect",
            "original display/control topology remains nine-output",
        ),
    ):
        require(fragment in integration, description)

    identity = IDENTITY_RTL.read_text(encoding="utf-8")
    for fragment, description in (
        (
            "[get_bd_intf_pins $splitter_name/M02_AXI]",
            "splitter M02_AXI is dedicated to the SDC1 identity endpoint",
        ),
        (
            "assign_bd_address -offset 0x3C080000 -range 0x00010000",
            "SDC1 identity segment is reserved at 0x3c080000",
        ),
    ):
        require(fragment in integration, description)
    require(
        "32'h3143_4453" in identity
        and "32'h0001_0000" in identity
        and "32'h0000_0003" in identity
        and "32'd32768" in identity,
        "SDC1 ABI magic, version 1.0, features, and 32 KiB aperture are fixed",
    )

    for target, expected in (
        ("axi_iic_0/S_AXI/Reg", "0x3C020000"),
        ("axi_uartlite_0/S_AXI/Reg", "0x3C030000"),
        ("axi_vdma_0/S_AXI_LITE/Reg", "0x3C040000"),
        ("hdmi_out/color_convert/s_axi_control/Reg", "0x3C050000"),
        ("hdmi_out/pixel_unpack/s_axi_control/Reg", "0x3C000000"),
        ("hdmi_out/v_tc_0/ctrl/Reg", "0x3C010000"),
        ("hdmi_out/video_clk_wiz/s_axi_lite/Reg", "0x3C060000"),
        ("hdmi_out/video_lock_monitor/S_AXI/Reg", "0x3C070000"),
    ):
        require(
            re.search(
                rf"assign_bd_address -offset {expected} .*"
                rf"\[get_bd_addr_segs {re.escape(target)}\]",
                export,
            )
            is not None,
            f"{target} remains mapped at {expected.lower()}",
        )

    require(
        "parameter integer DATA_WIDTH = 32" in rom_rtl
        and "parameter integer ADDR_WIDTH = 32" in rom_rtl,
        "ROM presents 32-bit data with full 32-bit translated addresses",
    )
    require(
        "read_beats_remaining" in rom_rtl
        and "s_axi_rlast" in rom_rtl
        and "s_axi_rready" in rom_rtl
        and "s_axi_bid" in rom_rtl,
        "ROM RTL implements bursts, RLAST, backpressure, and AXI IDs",
    )
    require(
        "write_response_valid" in rom_rtl
        and "$readmemh" in rom_rtl
        and "rom_storage" in rom_rtl,
        "writes complete without a ROM storage write path",
    )

    image = EFI_BIN.read_bytes()
    mem_lines = EFI_MEM.read_text(encoding="ascii").splitlines()
    require(
        len(image) == size_kib * 1024,
        f"selected ROM is exactly {size_kib} KiB",
    )
    require(
        len(mem_lines) == size_kib * 256
        and b"".join(bytes.fromhex(line)[::-1] for line in mem_lines) == image,
        "32-bit initialization is byte-exact",
    )

    rom_region = (0x01000000, 0x01000000 + size_kib * 1024 - 1)
    bar2_region = (0x3C000000, 0x3FFFFFFF)
    require(
        rom_region[1] < bar2_region[0] or bar2_region[1] < rom_region[0],
        "BAR6 ROM destination cannot alias BAR2 destinations",
    )
    identity_region = (0x3C080000, 0x3C08FFFF)
    occupied_bar2_regions = [
        (0x3C000000, 0x3C07FFFF),
        (0x3E000000, 0x3FFFFFFF),
    ]
    require(
        all(
            identity_region[1] < base or high < identity_region[0]
            for base, high in occupied_bar2_regions
        ),
        "SDC1 identity segment cannot alias controls or framebuffer DDR",
    )


def generated_contract(project_dir: Path) -> None:
    size_kib = ROM_SIZE_KIB
    project_dir = project_dir.resolve()
    project_name = project_dir.name
    generated = project_dir / f"{project_name}.gen"
    sources = project_dir / f"{project_name}.srcs"

    parent_xci = single(
        [
            path
            for path in sources.rglob("*.xci")
            if "bd/PCIe/ip/" in path.as_posix()
            and component_reference(path) == "xilinx.com:ip:xdma:4.1"
        ],
        "outer XDMA XCI",
    )
    child_xci = single(
        [
            path
            for path in generated.rglob("*pcie2_ip.xci")
            if "/bd/PCIe/ip/" in path.as_posix()
        ],
        "nested pcie_7x XCI",
    )
    decoder = single(
        [
            path
            for path in generated.rglob("*tgt_req.sv")
            if "/bd/PCIe/ip/" in path.as_posix()
        ],
        "generated target decoder",
    )
    display_crossbar_xci = single(
        [
            path
            for path in sources.rglob("*.xci")
            if "bd/PCIe/ip/" in path.as_posix()
            and component_reference(path) == "xilinx.com:ip:axi_crossbar:2.1"
            and str(parameters(path).get("NUM_MI")) == "9"
        ],
        "nine-output display/control crossbar XCI",
    )
    rom_splitter_xci = single(
        [
            path
            for path in sources.rglob("*.xci")
            if "bd/PCIe/ip/" in path.as_posix()
            and component_reference(path) == "xilinx.com:ip:axi_crossbar:2.1"
            and str(parameters(path).get("NUM_MI")) == "3"
        ],
        "three-output Expansion ROM splitter XCI",
    )
    ila_xcis = [
        path
        for path in sources.rglob("*.xci")
        if "bd/PCIe/ip/" in path.as_posix()
        and component_reference(path) == "xilinx.com:ip:system_ila:1.1"
    ]

    parent = parameters(parent_xci)
    child = parameters(child_xci)
    require(
        str(parent.get("pf0_expansion_rom_enabled")).lower() == "true"
        and parent.get("pf0_expansion_rom_type") == "Bypass_AXI_Master",
        "outer XCI enables PF0 bypass-master Expansion ROM",
    )
    require(
        str(parent.get("pf0_expansion_rom_size")) == str(size_kib)
        and parent.get("pf0_expansion_rom_scale") == "Kilobytes",
        f"outer XCI aperture is {size_kib} KiB",
    )
    require(
        str(parent.get("pciebar2axibar_6")).lower()
        == "0x0000000001000000",
        "outer XCI BAR ID 6 translation is 0x01000000",
    )
    require(
        str(parent.get("PF0_EXPANSION_ROM_ENABLE")).upper() == "TRUE",
        "outer generated model enables PF0 Expansion ROM",
    )
    expected_encoding = "0x008"
    require(
        str(parent.get("PF0_EXPANSION_ROM_APERTURE_SIZE")).lower()
        == expected_encoding,
        f"outer generated aperture encoding is {expected_encoding}",
    )
    require(
        str(child.get("Expansion_Rom_Enabled")).lower() == "true"
        and str(child.get("Expansion_Rom_Size")) == str(size_kib)
        and child.get("Expansion_Rom_Scale") == "Kilobytes",
        f"nested pcie_7x aperture is {size_kib} KiB",
    )
    expected_xrom = "FFFF8001"
    require(
        str(child.get("xrom_bar")).upper() == expected_xrom,
        f"nested xrom_bar is {expected_xrom}",
    )
    require(
        decoder.read_text(encoding="utf-8").count(
            "(m_axis_rx_tuser[8]) ? 3'h6"
        )
        == 4,
        "all four generated decoders preserve BAR ID 6",
    )
    require(
        str(parameters(display_crossbar_xci).get("NUM_MI")) == "9",
        "generated display/control crossbar remains nine-output",
    )
    require(
        str(parameters(rom_splitter_xci).get("STRATEGY")) == "0",
        "generated same-clock Expansion ROM splitter uses area strategy",
    )
    require(
        len(ila_xcis) == 1
        and all(
            str(parameters(path).get("C_INPUT_PIPE_STAGES")) == "2"
            for path in ila_xcis
        ),
        "only the two-stage XDMA ILA remains in the production checkpoint",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-dir", type=Path)
    args = parser.parse_args()

    source_contract()
    if args.project_dir is not None:
        generated_contract(args.project_dir)
    print("SIMPLE_DISPLAY_XDMA_EXPANSION_ROM_CONTRACT_PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
