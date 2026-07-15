# Required FPGA Hardware Architecture

## Current path and missing contracts

The working Linux path is:

```text
host XRGB8888 -> XDMA H2C stream -> VDMA S2MM -> DDR frame ring
DDR -> VDMA MM2S -> pixel_unpack -> color_convert -> video out/VTC -> HDMI
```

The host can program video registers through the current 32 MiB XDMA bypass
BAR. The updated export maps an 8 MiB MIG segment into the BAR's non-aliased
lower half. The strict static contract, live scratch save/write/read/restore,
AXI ILA responses, and a `1280x720` bypass-frame scanout now pass. The current
PCI function still has no Expansion ROM. See the
[development record](../development_work/pcie_ddr_bypass_gop_prerequisite/README.md)
for the complete evidence and remaining maximum-resolution test.

## Target PCI resources

| Resource | Proposed purpose | Initial sizing |
|---|---|---:|
| Existing XDMA control BAR | Keep Linux XDMA engine access | Existing 64 KiB |
| Register BAR/window | VDMA, VTC, clock, I2C, pixel-IP, and status MMIO | Less than 1 MiB is currently used |
| Framebuffer BAR/window | Direct PCIe writes into one DDR scanout frame | 8 MiB |
| Expansion ROM BAR | Read-only UEFI Option ROM | Size after measuring the packaged ROM; likely 128-512 KiB |

A 1920x1080 32-bit framebuffer needs 8,294,400 bytes. An 8 MiB binary window
holds one such frame and leaves 94,208 bytes, including a reserved scratch
page. The validated shared BAR2 is currently 64-bit and prefetchable, and this
B85 host assigned it below 4 GiB. UEFI software must discover its actual base
and must not assume that placement. If a future design introduces a dedicated
framebuffer BAR, a 32-bit prefetchable BAR remains worth evaluating for older
firmware compatibility.

There are two Vivado realizations:

1. The implemented shared map keeps translation `0x3f000000`, control IPs at
   `0x3f000000-0x3f07ffff`, and maps MIG at
   `0x3f800000-0x3fffffff`. Only host offsets
   `0x00000000-0x00ffffff` are used. VDMA independently maps the same MIG
   offset zero at `0x40000000`.
2. A future separate framebuffer BAR remains the cleanest firmware contract
   if this 7-series XDMA configuration can expose it.

Do not place a required segment in host offsets `0x01000000-0x01ffffff` with
the current translation; ILA proved that those upper offsets alias. Equal AXI
addresses across bypass and VDMA masters are not necessary, but both mappings
must select the same physical MIG offsets.

Use the implemented shared map for the first Shell application and GOP driver;
it has passed HWH, kernel, and ILA validation. Revisit a separate framebuffer
BAR only if firmware compatibility, performance, or ownership testing exposes
a concrete limitation.

## Framebuffer data path

The framebuffer BAR must route PCI writes to the same DDR address scanned by
VDMA MM2S. GOP mode metadata should report:

```text
FrameBufferBase = host physical address of framebuffer BAR + framebuffer offset
FrameBufferSize = PixelsPerScanLine * VerticalResolution * 4
PixelFormat     = PixelBlueGreenRedReserved8BitPerColor
```

On little-endian x86, that GOP format is byte-compatible with DRM
`XRGB8888`: bytes are blue, green, red, reserved. The existing FPGA ignores
the reserved/X byte, so the Linux and firmware paths can share the pixel
contract.

Initially dedicate frame store 0 to GOP. VDMA MM2S should park on it. The Linux
driver may reinitialize all four frame stores after takeover.

## Option ROM storage in FPGA logic

AMD PG195 states that enabling the Expansion ROM makes that read-only space
accessible through the AXI4-Lite master path. The target implementation is:

```text
XDMA Expansion ROM requests -> AXI read-only interconnect -> BRAM ROM
                                                        -> SimpleDisplayGop.rom
```

Required changes include:

- enable PF0 Expansion ROM in the XDMA IP;
- enable/connect the AXI4-Lite master path required by that ROM feature;
- choose a power-of-two aperture large enough for the packaged image;
- initialize BRAM from the generated ROM data during bitstream build;
- return errors or zero data for writes; and
- verify the ROM signature, PCIR fields, vendor/device IDs, image architecture,
  and final-image indicator through Linux sysfs and UEFI Shell.

The current export explicitly shows `PF0_EXPANSION_ROM_ENABLE=FALSE`, so this
is a real bitstream change, not only a software packaging step.

## Pipeline initialization

The UEFI driver, not MicroBlaze firmware, should program:

- VDMA frame addresses, stride, horizontal size, and vertical size;
- VTC timing and polarities;
- dynamic video clock parameters;
- `pixel_unpack` and `color_convert` registers;
- HDMI transmitter configuration through AXI IIC; and
- lock/status checks and bounded timeouts.

The register behavior already exists in `fpga_drm_drv.c` and should be ported
as hardware algorithms, not copied with Linux types, sleeps, or APIs.

## Power-on configuration requirement

The final FPGA image, including the PCI endpoint, framebuffer aperture,
pipeline, and ROM BRAM contents, must reside in AX7203 SPI configuration flash.
JTAG programming is insufficient because the endpoint must exist before UEFI
PCI enumeration.

Use the existing SPI x4 generation and programming flow, then validate at least
20 true AC-power cold boots. Record whether `10ee:7024` and its ROM are present
on every boot. If cold boot fails while warm reset succeeds, reduce bitstream
load time, increase configuration clock within board/flash limits, enable
compression, or evaluate a supported 7-series Tandem PROM flow.

## Hardware acceptance gates

1. Cold boot enumerates `10ee:7024` consistently without a PCI rescan.
2. PCI config shows a nonzero Expansion ROM BAR and intended framebuffer BAR.
3. Host writes to the framebuffer BAR produce visible pixel changes without
   XDMA descriptors.
4. VDMA scans the BAR-backed DDR frame continuously.
5. Linux can read a valid Option ROM image from the PCI ROM resource.
6. Existing H2C streaming and `fpga_drm` still work after the new BARs are
   introduced.

## References

- [AMD PG195 PCIe BARs and Expansion ROM](https://docs.amd.com/r/en-US/pg195-pcie-dma/PCIe-BARs-Tab)
- [AMD PCIe address translation alignment checks](https://docs.amd.com/r/en-US/pg194-axi-bridge-pcie-gen3/Addressing-Checks)
- [AMD PG054 7-series PCIe configuration timing](https://docs.amd.com/r/en-US/pg054-7series-pcie/Configuration-Access-Specification-Requirements)
- [Current Vivado recreation source](../../vivado_project/PCIe.tcl)
- [Current hardware export](../../fpga_hardware/PCIe_wrapper/PCIe.hwh)
