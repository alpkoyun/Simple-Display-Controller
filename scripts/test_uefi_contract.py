#!/usr/bin/env python3
"""Host-side contract checks for the Simple Display UEFI implementation."""

from __future__ import annotations

import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
HW_HEADER = ROOT / "firmware/uefi/SimpleDisplayPkg/Include/SimpleDisplayHardware.h"
HW_SOURCE = ROOT / "firmware/uefi/SimpleDisplayPkg/Library/SimpleDisplayHwLib/SimpleDisplayHwLib.c"
GOP_SOURCE = ROOT / "firmware/uefi/SimpleDisplayPkg/SimpleDisplayGopDxe/SimpleDisplayGopDxe.c"
LINUX_SOURCE = ROOT / "Linux_DRM_Driver/fpga_drm/fpga_drm_drv.c"


MODES = (
    ("1280x720@60", 74250, 1280, 1390, 1430, 1650, 720, 725, 730, 750, 0x007D250A, 10),
    ("800x600@60", 40000, 800, 840, 968, 1056, 600, 601, 605, 628, 0x00000301, 15),
    ("640x480@60", 25175, 640, 656, 752, 800, 480, 490, 492, 525, 0x0000240B, 26),
    ("1024x768@60", 65000, 1024, 1048, 1184, 1344, 768, 771, 777, 806, 0x00000D04, 10),
    ("1280x1024@60", 108000, 1280, 1328, 1440, 1688, 1024, 1025, 1028, 1066, 0x00001B05, 10),
    ("1920x1080@60", 148500, 1920, 2008, 2052, 2200, 1080, 1084, 1089, 1125, 0x007D250A, 5),
)


def require(text: str, pattern: str, label: str) -> None:
    if re.search(pattern, text, re.MULTILINE | re.DOTALL) is None:
        raise AssertionError(f"missing or stale {label}: /{pattern}/")


def main() -> int:
    header = HW_HEADER.read_text(encoding="utf-8")
    hw = HW_SOURCE.read_text(encoding="utf-8")
    gop = GOP_SOURCE.read_text(encoding="utf-8")
    linux = LINUX_SOURCE.read_text(encoding="utf-8")

    constants = {
        "SIMPLE_DISPLAY_VENDOR_ID": 0x10EE,
        "SIMPLE_DISPLAY_DEVICE_ID": 0x7024,
        "SIMPLE_DISPLAY_BAR_INDEX": 2,
        "SIMPLE_DISPLAY_BAR_MIN_SIZE": 0x04000000,
        "SIMPLE_DISPLAY_IDENTITY_OFFSET": 0x00080000,
        "SIMPLE_DISPLAY_FRAMEBUFFER_OFFSET": 0x02000000,
        "SIMPLE_DISPLAY_SCRATCH_OFFSET": 0x03FFF000,
        "SIMPLE_DISPLAY_DDR_FRAME_ADDRESS": 0x3E000000,
        "SIMPLE_DISPLAY_MAX_FRAME_BYTES": 0x007E9000,
        "SIMPLE_DISPLAY_ABI_MAGIC": 0x31434453,
        "SIMPLE_DISPLAY_ABI_VERSION": 0x00010000,
    }
    for name, value in constants.items():
        require(
            header,
            rf"#define\s+{name}\s+(?:0x0*{value:X}|{value})(?:U|ULL)?\b",
            name,
        )

    require(header, r"#define\s+SIMPLE_DISPLAY_MODE_COUNT\s+6U", "six fixed 60 Hz candidates")
    require(header, r"#define\s+SIMPLE_DISPLAY_GOP_MODE_COUNT\s+1U", "Stage 2 exposure gate")

    for mode in MODES:
        name, clock, width, hs, he, ht, height, vs, ve, vt, cfg0, cfg2 = mode
        frame_bytes = width * height * 4
        assert frame_bytes <= constants["SIMPLE_DISPLAY_MAX_FRAME_BYTES"]
        assert constants["SIMPLE_DISPLAY_FRAMEBUFFER_OFFSET"] + frame_bytes <= constants["SIMPLE_DISPLAY_BAR_MIN_SIZE"]
        require(hw, re.escape(f'"{name}", {clock}'), f"firmware mode {name}")
        require(
            hw,
            rf"{width},\s*{hs},\s*{he},\s*{ht},\s*{height},\s*{vs},\s*{ve},\s*{vt}",
            f"firmware timing {name}",
        )
        require(
            linux,
            rf'FPGA_MODE\("{re.escape(name)}",\s*{clock},\s*'
            rf'{width},\s*{hs},\s*{he},\s*{ht},\s*'
            rf'{height},\s*{vs},\s*{ve},\s*{vt},',
            f"Linux timing {name}",
        )
        # Verify the evaluated clock-wizard values remain represented in source.
        if cfg0 == 0x007D250A:
            require(hw, r"10U\s*\|\s*\(37U\s*<<\s*8\)\s*\|\s*\(125U\s*<<\s*16\)", f"CFG0 {name}")
        require(hw, rf",\s*{cfg2}U\s*\n\s*\}}", f"CFG2 {name}")

    assert constants["SIMPLE_DISPLAY_SCRATCH_OFFSET"] + 16 <= constants["SIMPLE_DISPLAY_BAR_MIN_SIZE"]
    assert constants["SIMPLE_DISPLAY_FRAMEBUFFER_OFFSET"] + constants["SIMPLE_DISPLAY_MAX_FRAME_BYTES"] <= constants["SIMPLE_DISPLAY_SCRATCH_OFFSET"]

    require(gop, r"SizeOfEdid\s*=\s*0", "empty EDID sizes")
    require(gop, r"Edid\s*=\s*NULL", "empty EDID pointers")
    require(gop, r"FrameBufferBlt\s*\(", "FrameBufferBltLib use")
    require(gop, r"MAX_UINTN\s*-\s*Width", "BLT addition overflow guard")
    require(gop, r"EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER", "HDMI child ownership")
    require(gop, r"SimpleDisplayHwProgramMode", "SetMode hardware programming")
    require(
        hw,
        r"Magic\s*!=\s*SIMPLE_DISPLAY_ABI_MAGIC",
        "automatic-deployment ABI magic rejection",
    )
    require(
        hw,
        r"Context->AbiVersion\s*!=\s*SIMPLE_DISPLAY_ABI_VERSION",
        "automatic-deployment ABI version rejection",
    )
    require(
        hw,
        r"Context->AbiFeatures\s*&\s*SIMPLE_DISPLAY_ABI_REQUIRED_FEATURES",
        "automatic-deployment feature-bit rejection",
    )

    print("UEFI_CONTRACT_PASS")
    print(f"checked {len(MODES)} fixed 60 Hz timing candidates; GOP exposes mode 0 until Shell validation")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"UEFI_CONTRACT_FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
