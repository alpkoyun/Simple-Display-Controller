#!/usr/bin/env python3
"""Validate the fpga_drm address contract against a Vivado HWH export."""

from __future__ import annotations

import argparse
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


EXPECTED_BYPASS_RANGES = {
    "hdmi_out_pixel_unpack": ("REGISTER", 0x3C000000, 0x3C00FFFF),
    "hdmi_out_v_tc_0": ("REGISTER", 0x3C010000, 0x3C01FFFF),
    "axi_iic_0": ("REGISTER", 0x3C020000, 0x3C02FFFF),
    "axi_uartlite_0": ("REGISTER", 0x3C030000, 0x3C03FFFF),
    "axi_vdma_0": ("REGISTER", 0x3C040000, 0x3C04FFFF),
    "hdmi_out_color_convert": ("REGISTER", 0x3C050000, 0x3C05FFFF),
    "hdmi_out_video_clk_wiz": ("REGISTER", 0x3C060000, 0x3C06FFFF),
    "hdmi_out_video_lock_monitor": ("REGISTER", 0x3C070000, 0x3C07FFFF),
    "mig_7series_0": ("MEMORY", 0x3E000000, 0x3FFFFFFF),
}

EXPECTED_VDMA_DDR_RANGE = (0x3E000000, 0x3FFFFFFF)

ROM_CONTRACT = {
    "size": 0x8000,
    "mem": "simple_display_gop_option_rom_32.mem",
    "aperture_encoding": "0x008",
}

EXPECTED_IDENTITY_PARAMETERS = {
    "ABI_MAGIC": 0x31434453,
    "ABI_VERSION": 0x00010000,
    "ABI_FEATURES": 0x00000003,
}

EXPECTED_XDMA_PARAMETERS = {
    "AXIST_BYPASS_APERTURE_SIZE": "0x13",
    "AXIST_BYPASS_CONTROL": "0x5",
    "PF0_BAR0_APERTURE_SIZE": "0x0A",
    "PF0_DEVICE_ID": "0x7024",
    "PF0_CLASS_CODE": "0x038000",
    "axist_bypass_scale": "Megabytes",
    "axist_bypass_size": "64",
    "pciebar2axibar_axist_bypass": "0x3c000000",
}

EXPECTED_DRIVER_MACROS = {
    "FPGA_HW_FRAME_COUNT": 4,
    "FPGA_HW_DDR_AXI_BASE": 0x3E000000,
    "FPGA_HW_DDR_AXI_SIZE": 0x02000000,
    "FPGA_HW_BYPASS_AXI_TRANSLATION": 0x3C000000,
    "FPGA_HW_BYPASS_RESOURCE_SIZE": 0x04000000,
    "FPGA_HW_BYPASS_HOST_USABLE_SIZE": 0x04000000,
    "FPGA_HW_DDR_BYPASS_AXI_BASE": 0x3E000000,
    "FPGA_HW_DDR_BYPASS_SIZE": 0x02000000,
    "FPGA_HW_DDR_SCRATCH_AXI_BASE": 0x3FFFF000,
    "FPGA_HW_GOP_BYPASS_FRAME_BASE": 0x3E000000,
    "FPGA_HW_GOP_VDMA_FRAME_BASE": 0x3E000000,
    "FPGA_HW_FRAME_BASE": 0x3E000000,
    "FPGA_HW_PIXEL_UNPACK_BASE": 0x3C000000,
    "FPGA_HW_VTC_BASE": 0x3C010000,
    "FPGA_HW_AXI_IIC_BASE": 0x3C020000,
    "FPGA_HW_VDMA_BASE": 0x3C040000,
    "FPGA_HW_COLOR_CONVERT_BASE": 0x3C050000,
    "FPGA_HW_VIDEO_CLK_WIZ_BASE": 0x3C060000,
    "FPGA_HW_VIDEO_LOCK_GPIO_BASE": 0x3C070000,
}


def parse_args() -> argparse.Namespace:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--hwh",
        type=Path,
        default=repo / "fpga_hardware/PCIe_wrapper/PCIe.hwh",
        help="Vivado HWH export to validate",
    )
    parser.add_argument(
        "--driver",
        type=Path,
        default=repo / "Linux_DRM_Driver/fpga_drm/fpga_drm_drv.c",
        help="fpga_drm source containing the matching constants",
    )
    parser.add_argument(
        "--require-ddr-bypass",
        action="store_true",
        help=(
            "fail unless the direct DDR range is representable through "
            "the XDMA bypass translation"
        ),
    )
    parser.add_argument(
        "--clean-baseline",
        action="store_true",
        help="validate the pre-integration 64 MiB display HWH",
    )
    return parser.parse_args()


def integer(attribute: str) -> int:
    return int(attribute, 0)


def parameter_integer(value: str) -> int:
    """Parse decimal/hex or Vivado's unprefixed binary HWH parameters."""
    normalized = value.strip('"')
    if len(normalized) >= 8 and set(normalized) <= {"0", "1"}:
        return int(normalized, 2)
    return integer(normalized)


def canonical_instance(name: str) -> str:
    """Normalize the MIG name emitted by flat and hierarchical HWH exports."""
    if name == "mig_7series_0" or name.endswith("_mig_7series_0"):
        return "mig_7series_0"
    return name


def check_hwh(path: Path, clean_baseline: bool) -> list[str]:
    root = ET.parse(path).getroot()
    errors: list[str] = []
    ranges = list(root.iter("MEMRANGE"))

    bypass = {
        canonical_instance(item.attrib["INSTANCE"]): item
        for item in ranges
        if item.attrib.get("MASTERBUSINTERFACE") == "M_AXI_BYPASS"
    }
    for instance, (memtype, base, high) in EXPECTED_BYPASS_RANGES.items():
        item = bypass.get(instance)
        if item is None:
            errors.append(f"missing M_AXI_BYPASS range for {instance}")
            continue
        actual = (
            item.attrib.get("MEMTYPE"),
            integer(item.attrib["BASEVALUE"]),
            integer(item.attrib["HIGHVALUE"]),
        )
        expected = (memtype, base, high)
        if actual != expected:
            errors.append(
                f"{instance}: expected {expected}, got {actual}"
            )

    if not clean_baseline:
        rom_size = int(ROM_CONTRACT["size"])
        extra_ranges = {
            "expansion_rom": (
                "REGISTER",
                0x01000000,
                0x01000000 + rom_size - 1,
            ),
        }
        extra_ranges["identity_regs"] = (
            "REGISTER",
            0x3C080000,
            0x3C08FFFF,
        )
        for instance, (memtype, base, high) in extra_ranges.items():
            item = bypass.get(instance)
            if item is None:
                errors.append(f"missing M_AXI_BYPASS range for {instance}")
                continue
            actual = (
                item.attrib.get("MEMTYPE"),
                integer(item.attrib["BASEVALUE"]),
                integer(item.attrib["HIGHVALUE"]),
            )
            expected = (memtype, base, high)
            if actual != expected:
                errors.append(f"{instance}: expected {expected}, got {actual}")

        module_parameters = {
            module.attrib.get("INSTANCE", ""): {
                parameter.attrib["NAME"]: parameter.attrib.get("VALUE", "")
                for parameter in module.iter("PARAMETER")
            }
            for module in root.iter("MODULE")
        }
        rom_parameters = module_parameters.get("expansion_rom", {})
        expected_mem = str(ROM_CONTRACT["mem"])
        if rom_parameters.get("ROM_BYTES") != str(rom_size):
            errors.append(
                "expansion_rom ROM_BYTES: "
                f"expected {rom_size}, got {rom_parameters.get('ROM_BYTES')}"
            )
        if rom_parameters.get("ROM_INIT_FILE") != expected_mem:
            errors.append(
                "expansion_rom ROM_INIT_FILE: "
                f"expected {expected_mem}, got "
                f"{rom_parameters.get('ROM_INIT_FILE')}"
            )

        identity_parameters = module_parameters.get("identity_regs") or {}
        expected_identity = {
            **EXPECTED_IDENTITY_PARAMETERS,
            "ROM_APERTURE_BYTES": rom_size,
        }
        for name, expected in expected_identity.items():
            value = identity_parameters.get(name, "")
            try:
                actual = parameter_integer(value)
            except ValueError:
                actual = -1
            if actual != expected:
                errors.append(
                    f"identity_regs {name}: expected 0x{expected:08x}, "
                    f"got {value}"
                )

    for master in ("M_AXI_MM2S", "M_AXI_S2MM"):
        matches = [
            item
            for item in ranges
            if item.attrib.get("MASTERBUSINTERFACE") == master
            and canonical_instance(item.attrib.get("INSTANCE", ""))
            == "mig_7series_0"
        ]
        if len(matches) != 1:
            errors.append(f"expected one {master} DDR range, found {len(matches)}")
            continue
        actual = (
            integer(matches[0].attrib["BASEVALUE"]),
            integer(matches[0].attrib["HIGHVALUE"]),
        )
        if actual != EXPECTED_VDMA_DDR_RANGE:
            errors.append(
                f"{master}: expected {EXPECTED_VDMA_DDR_RANGE}, got {actual}"
            )

    xdma_modules = [
        module
        for module in root.iter("MODULE")
        if module.attrib.get("INSTANCE") == "xdma_0"
    ]
    if len(xdma_modules) != 1:
        errors.append(f"expected one xdma_0 module, found {len(xdma_modules)}")
    else:
        parameters = {
            parameter.attrib["NAME"]: parameter.attrib.get("VALUE", "")
            for parameter in xdma_modules[0].iter("PARAMETER")
        }
        for name, expected in EXPECTED_XDMA_PARAMETERS.items():
            actual = parameters.get(name)
            if actual != expected:
                errors.append(f"xdma_0 {name}: expected {expected}, got {actual}")
        rom_enabled = "FALSE" if clean_baseline else "TRUE"
        if parameters.get("PF0_EXPANSION_ROM_ENABLE") != rom_enabled:
            errors.append(
                "xdma_0 PF0_EXPANSION_ROM_ENABLE: "
                f"expected {rom_enabled}, got "
                f"{parameters.get('PF0_EXPANSION_ROM_ENABLE')}"
            )
        if not clean_baseline:
            expected_aperture = str(ROM_CONTRACT["aperture_encoding"])
            if parameters.get("PF0_EXPANSION_ROM_APERTURE_SIZE") != expected_aperture:
                errors.append(
                    "xdma_0 PF0_EXPANSION_ROM_APERTURE_SIZE: "
                    f"expected {expected_aperture}, got "
                    f"{parameters.get('PF0_EXPANSION_ROM_APERTURE_SIZE')}"
                )

    return errors


def check_driver(path: Path) -> list[str]:
    source = path.read_text(encoding="utf-8")
    errors: list[str] = []
    for name, expected in EXPECTED_DRIVER_MACROS.items():
        match = re.search(
            rf"^#define\s+{re.escape(name)}\s+((?:0x[0-9a-fA-F]+)|(?:[0-9]+))(?:ULL|UL|U|LL|L)?\s*$",
            source,
            re.MULTILINE,
        )
        if not match:
            errors.append(f"driver macro {name} is missing or not a literal")
            continue
        actual = int(match.group(1), 0)
        if actual != expected:
            errors.append(
                f"driver macro {name}: expected 0x{expected:x}, got 0x{actual:x}"
            )
    return errors


def representability_error(name: str, base: int, high: int) -> str | None:
    translation = EXPECTED_DRIVER_MACROS["FPGA_HW_BYPASS_AXI_TRANSLATION"]
    aperture = EXPECTED_DRIVER_MACROS["FPGA_HW_BYPASS_RESOURCE_SIZE"]
    usable = EXPECTED_DRIVER_MACROS["FPGA_HW_BYPASS_HOST_USABLE_SIZE"]
    if base < translation:
        return f"{name}: AXI base 0x{base:08x} is below translation 0x{translation:08x}"

    start_offset = base - translation
    end_offset = high - translation
    if end_offset >= aperture:
        return (
            f"{name}: host offsets 0x{start_offset:08x}-0x{end_offset:08x} "
            f"exceed BAR aperture 0x{aperture:08x}"
        )
    if end_offset >= usable:
        return (
            f"{name}: host offsets 0x{start_offset:08x}-0x{end_offset:08x} "
            f"leave the non-aliased range 0x00000000-0x{usable - 1:08x}"
        )
    observed_base = translation | start_offset
    observed_high = translation | end_offset
    if observed_base != base or observed_high != high:
        return (
            f"{name}: XDMA translation aliases requested AXI "
            f"0x{base:08x}-0x{high:08x} to "
            f"0x{observed_base:08x}-0x{observed_high:08x}"
        )
    return None


def main() -> int:
    args = parse_args()
    errors: list[str] = []
    warnings: list[str] = []

    translation = EXPECTED_DRIVER_MACROS["FPGA_HW_BYPASS_AXI_TRANSLATION"]
    aperture = EXPECTED_DRIVER_MACROS["FPGA_HW_BYPASS_RESOURCE_SIZE"]
    if aperture == 0 or aperture & (aperture - 1):
        errors.append(f"bypass aperture 0x{aperture:x} is not a power of two")
    else:
        for instance, (_, base, high) in EXPECTED_BYPASS_RANGES.items():
            issue = representability_error(instance, base, high)
            if not issue:
                continue
            if instance == "mig_7series_0" and not args.require_ddr_bypass:
                warnings.append(issue)
            else:
                errors.append(issue)

        usable = EXPECTED_DRIVER_MACROS["FPGA_HW_BYPASS_HOST_USABLE_SIZE"]
        if usable > aperture:
            errors.append(
                f"usable bypass host size 0x{usable:x} exceeds aperture 0x{aperture:x}"
            )
        elif usable < aperture:
            warnings.append(
                f"BAR host offsets 0x{usable:08x}-0x{aperture - 1:08x} alias "
                "because of translation bits and are intentionally unused"
            )

    inputs_exist = True
    for path in (args.hwh, args.driver):
        if not path.is_file():
            errors.append(f"missing input: {path}")
            inputs_exist = False

    if inputs_exist:
        try:
            errors.extend(check_hwh(args.hwh, args.clean_baseline))
            errors.extend(check_driver(args.driver))
        except (ET.ParseError, OSError, ValueError) as error:
            errors.append(str(error))

    if errors:
        print("hardware contract: FAIL", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1

    print(f"hardware contract: PASS for normal H2C display ({args.hwh})")
    configuration = "clean pre-integration baseline" if args.clean_baseline else "fixed 32 KiB EFI/GOP"
    print(f"  Hardware configuration          {configuration}")
    for instance, (_, base, high) in EXPECTED_BYPASS_RANGES.items():
        print(f"  {instance:30s} 0x{base:08x}-0x{high:08x}")
    print("  VDMA DDR masters               0x3e000000-0x3fffffff")
    print("  PCI BAR host offsets            0x00000000-0x03ffffff")
    print("  non-aliased host offsets        0x00000000-0x03ffffff")
    print("  PCI-to-AXI translation          0x3c000000")
    print("  bypass DDR host offsets         0x02000000-0x03ffffff")
    print("  GOP bypass frame                0x3e000000-0x3e7e8fff")
    print("  GOP VDMA scanout                0x3e000000-0x3e7e8fff")
    print("  DDR scratch page                0x3ffff000 (host+0x03fff000)")
    print("  fpga_drm four-frame ring        0x3e000000-0x3ffa6fff")
    if not args.clean_baseline:
        rom_size = int(ROM_CONTRACT["size"])
        print(
            "  Expansion ROM AXI destination   "
            f"0x01000000-0x{0x01000000 + rom_size - 1:08x}"
        )
        print("  SDC1 identity registers         0x3c080000-0x3c08ffff")
    for warning in warnings:
        print(f"  WARNING: {warning}")
    if args.require_ddr_bypass:
        print("  direct DDR/GOP bypass           PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
