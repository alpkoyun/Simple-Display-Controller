#!/usr/bin/env python3
"""Check the source/generated boundary for the canonical Vivado project."""

from __future__ import annotations

import argparse
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
VIVADO_ROOT = REPO_ROOT / "vivado_project"
PROJECT_NAME = "PCIe_GOP_ROM_32K_1080P_DEFAULT"
CANONICAL_XPR = (
    VIVADO_ROOT / "linux_build" / PROJECT_NAME / f"{PROJECT_NAME}.xpr"
)

REQUIRED_INPUTS = (
    "vivado_project/scripts/run_linux_vivado_flow.sh",
    "vivado_project/scripts/run_expansion_rom_unit_sim.sh",
    "vivado_project/scripts/check_vivado_source_tree.py",
    "vivado_project/export/PCIe.tcl",
    "vivado_project/scripts/recreate_pcie_linux.tcl",
    "vivado_project/scripts/build_pcie_linux.tcl",
    "vivado_project/scripts/verify_pcie_linux_implementation.tcl",
    "vivado_project/scripts/configure_xdma_expansion_rom.tcl",
    "vivado_project/scripts/integrate_expansion_rom_bd.tcl",
    "vivado_project/scripts/patch_xdma_tgt_req.py",
    "vivado_project/scripts/check_bypass_64mb_baseline.py",
    "vivado_project/scripts/check_efi_option_rom.py",
    "vivado_project/scripts/check_expansion_rom_contract.py",
    "vivado_project/scripts/check_fix_pcie_lane_order.py",
    "scripts/check_fpga_hardware_contract.py",
    "scripts/check_uefi_cold_bind_validation.py",
    "vivado_project/IPs/IP_Packages/component.xml",
    "vivado_project/IPs/IP_Packages/src/frame_counter.v",
    "vivado_project/IPs/IP_Packages/xgui/frame_counter_v1_0.tcl",
    "vivado_project/hls/build_ip.sh",
    "vivado_project/hls/color_convert/script.tcl",
    "vivado_project/hls/color_convert/color_convert.cpp",
    "vivado_project/hls/color_convert/color_convert.hpp",
    "vivado_project/hls/pixel_pack/script.tcl",
    "vivado_project/hls/pixel_pack/pixel_pack.cpp",
    "vivado_project/hls/pixel_pack/pixel_pack.hpp",
    "vivado_project/hls/pixel_unpack/script.tcl",
    "vivado_project/hls/pixel_unpack/pixel_unpack.cpp",
    "vivado_project/hls/pixel_unpack/pixel_unpack.hpp",
    "vivado_project/hls/trace_cntrl_32/script.tcl",
    "vivado_project/hls/trace_cntrl_32/trace_cntrl_32.cpp",
    "vivado_project/hls/trace_cntrl_64/script.tcl",
    "vivado_project/hls/trace_cntrl_64/trace_cntrl_64.cpp",
    "vivado_project/rtl/simple_display_axi_rom.sv",
    "vivado_project/rtl/simple_display_identity_regs.sv",
    "vivado_project/sim/tb_simple_display_axi_rom.sv",
    "vivado_project/sim/tb_simple_display_identity_regs.sv",
    "vivado_project/rom/GOP_SHA256SUMS",
    "vivado_project/rom/gop_option_rom_metadata.json",
    "vivado_project/rom/simple_display_gop_option_rom.bin",
    "vivado_project/rom/simple_display_gop_option_rom_32.mem",
    "vivado_project/PCIe.srcs/constrs_1/new/PCIe.xdc",
    "vivado_project/PCIe.srcs/sources_1/bd/PCIe/ip/"
    "PCIe_mig_7series_0_0/mig_a.prj",
    "vivado_project/PCIe.srcs/sources_1/bd/PCIe/ip/"
    "PCIe_mig_7series_0_0/mig_b.prj",
    "firmware/uefi/test_logs/2026-08-05_1080p_cold_bind_pass/"
    "COLD_GOP_BIND_VALIDATION_PASS.json",
    "firmware/uefi/test_logs/2026-08-05_1080p_cold_bind_pass/"
    "tested_payload/SimpleDisplayGopDxe.efi",
    "firmware/uefi/test_logs/2026-08-05_1080p_cold_bind_pass/"
    "tested_payload/SimpleDisplayGopTest.efi",
)

FORBIDDEN_TRACKED_PARTS = {
    ".Xil",
    "linux_build",
    "solution1",
}
FORBIDDEN_TRACKED_SUFFIXES = {
    ".bit",
    ".dcp",
    ".jou",
    ".ltx",
    ".xpr",
}
FORBIDDEN_TRACKED_ENDINGS = (
    ".cache",
    ".gen",
    ".hw",
    ".ip_user_files",
    ".runs",
    ".sim",
)


def git_lines(*args: str) -> list[str]:
    result = subprocess.run(
        ["git", "-C", str(REPO_ROOT), *args],
        check=True,
        stdout=subprocess.PIPE,
        text=True,
    )
    return [line for line in result.stdout.splitlines() if line]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Check canonical Vivado inputs and generated-tree policy."
    )
    parser.add_argument(
        "--require-tracked",
        action="store_true",
        help="also fail when a required recreation input is not tracked by Git",
    )
    return parser.parse_args()


def tracked_path_is_generated(relative: str) -> bool:
    path = Path(relative)
    if not path.parts or path.parts[0] != "vivado_project":
        return False
    if any(part in FORBIDDEN_TRACKED_PARTS for part in path.parts):
        return True
    if any(part.endswith(FORBIDDEN_TRACKED_ENDINGS) for part in path.parts):
        return True
    if path.suffix in FORBIDDEN_TRACKED_SUFFIXES:
        return True
    return path.name.startswith("vivado.log") or path.name.startswith("vivado.jou")


def check_frame_counter_package(errors: list[str]) -> None:
    component = VIVADO_ROOT / "IPs/IP_Packages/component.xml"
    if not component.is_file():
        return

    try:
        root = ET.parse(component).getroot()
    except ET.ParseError as error:
        errors.append(f"invalid frame-counter component.xml: {error}")
        return

    namespace = {"spirit": "http://www.spiritconsortium.org/XMLSchema/SPIRIT/1685-2009"}
    source_names = [
        (element.text or "").strip()
        for element in root.findall(".//spirit:file/spirit:name", namespace)
        if (element.text or "").strip().endswith(".v")
    ]
    expected = ["src/frame_counter.v", "src/frame_counter.v"]
    if source_names != expected:
        errors.append(
            "frame-counter package must use its self-contained RTL once in each "
            f"synthesis/simulation file set; found: {source_names}"
        )

    stale_names = [
        name
        for name in source_names
        if name.startswith("../") or Path(name).is_absolute()
    ]
    if stale_names:
        errors.append(
            "frame-counter package contains external or stale source paths: "
            + ", ".join(stale_names)
        )


def main() -> int:
    args = parse_args()
    errors: list[str] = []

    for relative in REQUIRED_INPUTS:
        if not (REPO_ROOT / relative).is_file():
            errors.append(f"missing canonical input: {relative}")

    check_frame_counter_package(errors)

    try:
        tracked = git_lines("ls-files")
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"ERROR: could not inspect Git source policy: {error}", file=sys.stderr)
        return 1

    if args.require_tracked:
        tracked_set = set(tracked)
        for relative in REQUIRED_INPUTS:
            if relative not in tracked_set:
                errors.append(f"canonical input is not tracked by Git: {relative}")

    for relative in tracked:
        if tracked_path_is_generated(relative) and (REPO_ROOT / relative).exists():
            errors.append(f"generated Vivado product is tracked: {relative}")

    local_projects = sorted(VIVADO_ROOT.rglob("*.xpr"))
    unexpected_projects = [
        path for path in local_projects if path.resolve() != CANONICAL_XPR.resolve()
    ]
    for path in unexpected_projects:
        errors.append(
            "unexpected local Vivado project: "
            f"{path.relative_to(REPO_ROOT)}"
        )
    if len(local_projects) > 1:
        errors.append(
            f"expected at most one local Vivado project, found {len(local_projects)}"
        )

    stale_roots = (
        VIVADO_ROOT / "PCIe.xpr",
        VIVADO_ROOT / "PCIe.cache",
        VIVADO_ROOT / "PCIe.gen",
        VIVADO_ROOT / "PCIe.hw",
        VIVADO_ROOT / "PCIe.ip_user_files",
        VIVADO_ROOT / "PCIe.sim",
        VIVADO_ROOT / "bypass_64mb",
        VIVADO_ROOT / "IPs/frame_counter",
        VIVADO_ROOT / "IPs/pcie_counter",
    )
    for path in stale_roots:
        if path.exists():
            errors.append(
                f"stale generated Vivado state remains: {path.relative_to(REPO_ROOT)}"
            )

    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1

    project_state = "present" if CANONICAL_XPR.is_file() else "not generated"
    print(f"CANONICAL_PROJECT={CANONICAL_XPR.relative_to(REPO_ROOT)}")
    print(f"CANONICAL_PROJECT_STATE={project_state}")
    print("SIMPLE_DISPLAY_VIVADO_SOURCE_TREE_PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
