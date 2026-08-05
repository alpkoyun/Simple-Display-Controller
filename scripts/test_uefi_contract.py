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
GOP_HEADER = ROOT / "firmware/uefi/SimpleDisplayPkg/SimpleDisplayGopDxe/SimpleDisplay.h"
GOP_INF = ROOT / "firmware/uefi/SimpleDisplayPkg/SimpleDisplayGopDxe/SimpleDisplayGopDxe.inf"
GOP_TEST_SOURCE = ROOT / "firmware/uefi/SimpleDisplayPkg/Application/SimpleDisplayGopTest/SimpleDisplayGopTest.c"
PACKAGE_DSC = ROOT / "firmware/uefi/SimpleDisplayPkg/SimpleDisplayPkg.dsc"
LINUX_SOURCE = ROOT / "Linux_DRM_Driver/fpga_drm/fpga_drm_drv.c"


MODES = (
    ("1920x1080@60", 148500, 1920, 2008, 2052, 2200, 1080, 1084, 1089, 1125, 0x007D250A, 5),
    ("800x600@60", 40000, 800, 840, 968, 1056, 600, 601, 605, 628, 0x00000301, 15),
    ("640x480@60", 25175, 640, 656, 752, 800, 480, 490, 492, 525, 0x0000240B, 26),
    ("1024x768@60", 65000, 1024, 1048, 1184, 1344, 768, 771, 777, 806, 0x00000D04, 10),
    ("1280x1024@60", 108000, 1280, 1328, 1440, 1688, 1024, 1025, 1028, 1066, 0x00001B05, 10),
    ("1280x720@60", 74250, 1280, 1390, 1430, 1650, 720, 725, 730, 750, 0x007D250A, 10),
)


def require(text: str, pattern: str, label: str) -> None:
    if re.search(pattern, text, re.MULTILINE | re.DOTALL) is None:
        raise AssertionError(f"missing or stale {label}: /{pattern}/")


def forbid(text: str, pattern: str, label: str) -> None:
    if re.search(pattern, text, re.MULTILINE | re.DOTALL) is not None:
        raise AssertionError(f"forbidden {label}: /{pattern}/")


def source_between(text: str, start: str, end: str) -> str:
    start_index = text.index(start)
    end_index = text.index(end, start_index)
    return text[start_index:end_index]


def main() -> int:
    header = HW_HEADER.read_text(encoding="utf-8")
    hw = HW_SOURCE.read_text(encoding="utf-8")
    gop = GOP_SOURCE.read_text(encoding="utf-8")
    gop_header = GOP_HEADER.read_text(encoding="utf-8")
    gop_inf = GOP_INF.read_text(encoding="utf-8")
    gop_test = GOP_TEST_SOURCE.read_text(encoding="utf-8")
    package_dsc = PACKAGE_DSC.read_text(encoding="utf-8")
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

    firmware_mode_table = source_between(
        hw,
        "gSimpleDisplayModes[SIMPLE_DISPLAY_MODE_COUNT]",
        "STATIC\nEFI_STATUS",
    )
    require(
        firmware_mode_table,
        r"=\s*\{\s*\{\s*\"1920x1080@60\"",
        "1920x1080@60 firmware mode 0",
    )

    linux_mode_table = source_between(
        linux,
        "static const struct fpga_video_mode fpga_video_modes[]",
        "static const struct fpga_video_mode *fpga_drm_preferred_mode",
    )
    assert linux_mode_table.count("DRM_MODE_TYPE_PREFERRED") == 1, (
        "Linux mode table must contain exactly one preferred mode"
    )
    preferred_offset = linux_mode_table.index("DRM_MODE_TYPE_PREFERRED")
    preferred_mode_offset = linux_mode_table.rfind('FPGA_MODE("', 0, preferred_offset)
    assert preferred_mode_offset >= 0
    assert linux_mode_table[preferred_mode_offset:].startswith(
        'FPGA_MODE("1920x1080@60"'
    ), "Linux preferred mode must be 1920x1080@60"

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
    require(gop, r"PixelFormat\s*=\s*PixelBltOnly", "BLT-only GOP pixel format")
    forbid(
        gop,
        r"PixelFormat\s*=\s*PixelBlueGreenRedReserved8BitPerColor",
        "direct linear-framebuffer pixel format",
    )
    forbid(gop, r"FrameBufferBltConfigure\s*\(", "FrameBufferBltLib configuration")
    forbid(gop_header, r"FrameBufferBltLib|FRAME_BUFFER_CONFIGURE", "FrameBufferBltLib private state")
    forbid(gop_inf, r"FrameBufferBltLib", "FrameBufferBltLib INF dependency")
    forbid(package_dsc, r"FrameBufferBltLib", "FrameBufferBltLib DSC mapping")
    assert gop.count("GopMode.FrameBufferBase = 0;") == 2, (
        "Start and SetMode must both suppress the linear framebuffer base"
    )
    assert gop.count("GopMode.FrameBufferSize = 0;") == 2, (
        "Start and SetMode must both suppress the linear framebuffer size"
    )
    require(gop_test, r"PixelFormat\s*!=\s*PixelBltOnly", "GOP test BLT-only requirement")
    require(gop_test, r"FrameBufferBase\s*!=\s*0", "GOP test zero framebuffer base")
    require(gop_test, r"FrameBufferSize\s*!=\s*0", "GOP test zero framebuffer size")
    require(
        gop,
        r"ReadFrameBufferRow\s*\(.*?PciIo->Mem\.Read\s*\(",
        "PCI I/O framebuffer readback",
    )
    require(
        gop,
        r"WriteFrameBufferRow\s*\(.*?PciIo->Mem\.Write\s*\(",
        "PCI I/O overlapping framebuffer copy",
    )
    require(
        gop,
        r"BltOperation\s*==\s*EfiBltVideoFill.*?WriteFrameBufferRow\s*\(",
        "PCI I/O video fill",
    )
    require(
        gop,
        r"BltOperation\s*==\s*EfiBltBufferToVideo.*?WriteFrameBufferRow\s*\(",
        "PCI I/O buffer-to-video copy",
    )
    require(gop, r"Status\s*=\s*This->Blt\s*\(", "SetMode PCI I/O clear")
    require(gop, r"MAX_UINTN\s*-\s*Width", "BLT addition overflow guard")
    require(gop, r"EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER", "HDMI child ownership")
    require(gop, r"SimpleDisplayHwProgramMode", "SetMode hardware programming")

    set_mode = source_between(gop, "SimpleDisplaySetMode (", "ReadFrameBufferRow (")
    require(
        set_mode,
        r"SimpleDisplayHwProgramMode.*?GopMode\.FrameBufferBase\s*=\s*0.*?GopMode\.FrameBufferSize\s*=\s*0.*?This->Blt",
        "SetMode programs hardware, suppresses linear access, and clears through BLT",
    )
    blt = source_between(gop, "SimpleDisplayBlt (", "SimpleDisplayDriverSupported (")
    require(
        blt,
        r"GopMode\.Mode\s*==\s*MAX_UINT32.*?GopMode\.Mode\s*>=\s*Private->GopMode\.MaxMode.*?EFI_NOT_READY",
        "BLT readiness based on the selected GOP mode",
    )

    supported = source_between(
        gop, "SimpleDisplayDriverSupported (", "FreeChildResources ("
    )
    start = source_between(gop, "SimpleDisplayDriverStart (", "SimpleDisplayDriverStop (")
    stop = source_between(
        gop, "SimpleDisplayDriverStop (", "mSimpleDisplayDriverBinding"
    )
    forbid(
        supported,
        r"SimpleDisplayHwInitializeContext\s*\(",
        "BAR2 MMIO from Driver Binding Supported",
    )
    require(
        supported,
        r"SimpleDisplayHwMatchPci\s*\(",
        "PCI-only Driver Binding Supported match",
    )
    require(
        supported,
        r"EFI_ALREADY_STARTED.*?gEfiCallerIdGuid.*?Private->ChildHandle\s*==\s*NULL.*?return EFI_SUCCESS",
        "recoverable parent-only binding in Supported",
    )

    command_read = start.index("PciIo->Pci.Read")
    attributes_get = start.index("EfiPciIoAttributeOperationGet")
    memory_enable = start.index("EnablePciMemoryDecode")
    context_init = start.index("SimpleDisplayHwInitializeContext")
    assert command_read < attributes_get < memory_enable < context_init, (
        "Start must save the raw PCI Command and attributes before enabling BAR2 MMIO"
    )
    require(
        start,
        r"OriginalPciAttributes\s*&=\s*~\(EFI_PCI_IO_ATTRIBUTE_IO.*?EFI_PCI_IO_ATTRIBUTE_MEMORY.*?EFI_PCI_IO_ATTRIBUTE_BUS_MASTER\).*?EFI_PCI_COMMAND_MEMORY_SPACE.*?EFI_PCI_IO_ATTRIBUTE_MEMORY",
        "raw PCI Command normalization of cached decode attributes",
    )
    restore = source_between(gop, "RestorePciAttributes (", "SimpleDisplayDriverStart (")
    require(
        restore,
        r"EfiPciIoAttributeOperationSet.*?PciIo->Pci\.Write.*?PciIo->Pci\.Read.*?OriginalPciCommand",
        "exact raw PCI Command restore and readback",
    )
    enable = source_between(gop, "EnablePciMemoryDecode (", "SimpleDisplayDriverStart (")
    require(
        enable,
        r"EfiPciIoAttributeOperationEnable.*?PciIo->Pci\.Read.*?EFI_PCI_COMMAND_MEMORY_SPACE.*?PciIo->Pci\.Write.*?PciIo->Pci\.Read",
        "raw memory-decode enable fallback and readback",
    )
    require(
        start,
        r"InstallProtocolInterface\s*\(.*?gEfiCallerIdGuid.*?Private",
        "parent-private binding marker",
    )
    require(
        start,
        r"IsDevicePathEnd\s*\(RemainingDevicePath\).*?return EFI_SUCCESS",
        "parent-only end-node Start",
    )
    require(
        start,
        r"ChildStartFailed:.*?RestorePciAttributes\s*\(Private\).*?FreeChildResources\s*\(Private\)",
        "child-start attribute restoration",
    )
    require(
        stop,
        r"NumberOfChildren\s*==\s*0.*?Private->ChildHandle\s*!=\s*NULL.*?RestorePciAttributes\s*\(Private\).*?UninstallProtocolInterface\s*\(.*?gEfiCallerIdGuid",
        "parent Stop cleanup and marker removal",
    )
    require(
        stop,
        r"FreeChildResources\s*\(Private\).*?RestorePciAttributes\s*\(Private\)",
        "child Stop resource and PCI-state cleanup",
    )
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
    print(
        f"checked {len(MODES)} fixed 60 Hz timing candidates; "
        "GOP mode 0 and the Linux preferred mode are 1920x1080@60"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"UEFI_CONTRACT_FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
