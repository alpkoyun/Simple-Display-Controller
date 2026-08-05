# Simple Display UEFI GOP Reference

This package is the structural reference for the Simple Display Controller
UEFI Graphics Output Protocol implementation. It describes the current source
and the accepted `1920x1080@60` `PixelBltOnly` deployment. Dated experiments,
raw logs, bitstream reports, and artifact hashes remain in the development and
evidence trees linked below.

The intended reader is familiar with C, PCIe, EDK II, and FPGA display
pipelines. UEFI-specific ownership and protocol rules are explained where they
are essential to this implementation.

## Current Contract

| Item | Current implementation |
|---|---|
| PCI function | Xilinx `10ee:7024`, display class `0x038000` |
| Option ROM | One 32 KiB, uncompressed X64 EFI boot-service-driver image |
| Driver model | UEFI Driver Binding bus driver with one physical-output child |
| Public display API | `EFI_GRAPHICS_OUTPUT_PROTOCOL` plus empty EDID Discovered/Active protocols |
| GOP modes | One public mode: `1920x1080@60` |
| Pixel contract | `PixelBltOnly`; `FrameBufferBase=0`, `FrameBufferSize=0` |
| Frame storage | BAR2 `+0x02000000`, FPGA AXI address `0x3e000000` |
| Access rule | Ordered 32-bit `EFI_PCI_IO_PROTOCOL.Mem.Read/Write` operations |
| Persistent boot | AX7203 SPI configuration loads the FPGA image and firmware consumes the embedded GOP |
| OS handoff | Linux binds `fpga_drm` after boot and reprograms the display pipeline |

The shared mode table contains additional Linux-supported timings, but
`SIMPLE_DISPLAY_GOP_MODE_COUNT` exposes only entry 0. Those other entries are
not public GOP modes.

## Reading Order

1. [Architecture](architecture.md) for the system layers and high-level design.
2. [Firmware components](firmware_components.md) for source ownership.
3. [Driver lifecycle](driver_lifecycle.md) and [state and ownership](state_and_ownership.md)
   for binding, child creation, cleanup, and PCI restoration.
4. [GOP protocol](gop_protocol.md) and [data flow](data_flow.md) for consumer-visible
   behavior and framebuffer transactions.
5. [Hardware interface](hardware_interface.md) and
   [Option ROM and boot flow](option_rom_and_boot_flow.md) for the FPGA contract.
6. [Build and packaging](build_and_packaging.md) and
   [validation and bring-up](validation_and_bringup.md) for reproducibility.
7. [Call graphs](call_graph.md), [error paths](error_paths.md), and
   [problems encountered](problems_encountered.md) for maintenance and diagnosis.

## Evidence Boundaries

These are independent claims. Passing one does not imply the next:

1. The PCI function enumerates with the expected identity, class, BARs, and ROM
   aperture.
2. Linux reads the complete ROM three times and each copy exactly matches the
   packaged input.
3. UEFI recognizes and loads the embedded EFI image.
4. Driver Binding claims the parent controller and creates the HDMI child.
5. The child exposes GOP, empty EDID protocols, and the physical-output device
   path.
6. `QueryMode()`, `SetMode()`, and every required BLT operation pass.
7. The FPGA display visibly carries firmware and bootloader output.
8. The same configuration boots from SPI without JTAG assistance.
9. Linux boots, binds `fpga_drm`, and produces native DRM scanout.

The current project record marks all nine layers as passed for the accepted
1080p checkpoint. The documents keep the layers separate because their failure
modes and evidence are different.

## Sources of Truth

Use the following precedence when facts disagree:

1. Current firmware, RTL, Tcl, and contract-checker source.
2. Machine-readable evidence and exact tested artifacts.
3. The living [project status](../project_next_steps.md).
4. Dated development documents and narrative analysis.
5. Research and superseded design plans.

Important source entry points are the
[GOP driver](../../firmware/uefi/SimpleDisplayPkg/SimpleDisplayGopDxe/SimpleDisplayGopDxe.c),
[hardware library](../../firmware/uefi/SimpleDisplayPkg/Library/SimpleDisplayHwLib/SimpleDisplayHwLib.c),
and [shared hardware contract](../../firmware/uefi/SimpleDisplayPkg/Include/SimpleDisplayHardware.h).

The detailed development trail is retained in
[GOP firmware implementation](../development_work/gop_firmware_implementation/README.md)
and [XDMA Expansion ROM/GOP integration](../development_work/xdma_expansion_rom_gop_integration/README.md).
The operator-oriented Shell procedure remains in the
[UEFI Shell validation guide](../uefi_shell_expansion_rom_validation/README.md).

## Terminology

| Term | Meaning in this project |
|---|---|
| Controller | Parent PCI function exposing `EFI_PCI_IO_PROTOCOL` |
| Child | HDMI physical-output handle created by the GOP driver |
| SDC1 | Read-only BAR2 identity and feature ABI at offset `0x00080000` |
| ROM transport | XDMA BAR ID 6 routing and the read-only AXI ROM endpoint |
| EFI dispatch | Firmware recognized and loaded the embedded EFI image |
| Binding | Driver Binding `Start()` claimed the controller and created the child |
| GOP output | A consumer selected a mode and used GOP to produce visible pixels |
| Native handoff | Linux `fpga_drm` owns and drives the endpoint after firmware |

## Maintenance Rule

Update these structural documents when a source-level contract changes. Add a
new dated development or evidence record when a build or hardware run proves a
new checkpoint. Do not edit old raw logs or strengthen an old claim after the
fact.
