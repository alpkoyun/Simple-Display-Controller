# Required FPGA Hardware Architecture

## Current path and missing contracts

The working Linux path is:

```text
host XRGB8888 -> XDMA H2C stream -> VDMA S2MM -> DDR frame ring
DDR -> VDMA MM2S -> pixel_unpack -> color_convert -> video out/VTC -> HDMI
```

The host can program video registers through the 2 MiB XDMA bypass BAR, but it
cannot issue ordinary PCI memory writes into the DDR frame store. The current
PCI function also has no Expansion ROM. Those are the two hardware contracts
GOP deployment needs.

## Target PCI resources

| Resource | Proposed purpose | Initial sizing |
|---|---|---:|
| Existing XDMA control BAR | Keep Linux XDMA engine access | Existing 64 KiB |
| Register BAR/window | VDMA, VTC, clock, I2C, pixel-IP, and status MMIO | Existing 2 MiB is sufficient |
| Framebuffer BAR/window | Direct PCIe writes into one DDR scanout frame | 16 MiB |
| Expansion ROM BAR | Read-only UEFI Option ROM | Size after measuring the packaged ROM; likely 128-512 KiB |

A 1920x1080 32-bit framebuffer needs 8,294,400 bytes, so a 16 MiB aperture
holds one maximum-size frame with alignment room. Start with a 32-bit BAR for
compatibility with this older B85 firmware. Marking a framebuffer-only BAR
prefetchable is desirable if the XDMA core and interconnect preserve the
required semantics, because firmware and early OS mappings can then use
write-combining.

There are two Vivado realizations to evaluate:

1. Configure a separate XDMA PCI-to-AXI bridge BAR translated to DDR address
   `0x81000000`. This is the cleanest PCI contract.
2. If this 7-series XDMA configuration exposes only the existing
   `M_AXI_BYPASS`, enlarge/repartition that aperture and add address translation
   so one subwindow reaches DDR while the low subwindow retains the register
   map.

Do not choose between these from documentation alone. Create a minimal Vivado
experiment and verify the generated `.hwh`, PCI BAR layout, AXI addresses, and
read/write behavior before integrating GOP.

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
- [AMD PG054 7-series PCIe configuration timing](https://docs.amd.com/r/en-US/pg054-7series-pcie/Configuration-Access-Specification-Requirements)
- [Current Vivado recreation source](../../vivado_project/PCIe.tcl)
- [Current hardware export](../../fpga_hardware/PCIe_wrapper/PCIe.hwh)

