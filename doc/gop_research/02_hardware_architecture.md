# Required FPGA Hardware Architecture

## Current path and missing contracts

The intended native Linux path is:

```text
host XRGB8888 -> XDMA H2C stream -> VDMA S2MM -> DDR frame ring
DDR -> VDMA MM2S -> pixel_unpack -> color_convert -> video out/VTC -> HDMI
```

The host can program video registers through the current 64 MiB XDMA bypass
BAR. The updated export maps a 32 MiB MIG segment into the upper half and gives
the bypass and VDMA masters the same `0x3e000000-0x3fffffff` DDR addresses.
The strict static contract, live scratch test, and `1280x720@60` direct-BAR
color-bar scanout pass on this export. The independent XDMA H2C Linux upload
path currently stalls before asserting stream `TVALID`; four-frame H2C
operation must be repaired separately. The current PCI function still has no
Expansion ROM. See the
[current development record](../development_work/shared_32m_ddr_gop_prerequisite/README.md)
for the complete evidence and remaining maximum-resolution test.

## Target PCI resources

| Resource | Proposed purpose | Initial sizing |
|---|---|---:|
| Existing XDMA control BAR | Keep Linux XDMA engine access | Existing 64 KiB |
| Register BAR/window | VDMA, VTC, clock, I2C, pixel-IP, and status MMIO | Less than 1 MiB is currently used |
| Framebuffer BAR/window | Direct PCIe writes into the shared DDR frame ring | 32 MiB |
| Expansion ROM BAR | Read-only UEFI Option ROM | Size after measuring the packaged ROM; likely 128-512 KiB |

A 1920x1080 32-bit framebuffer needs 8,294,400 bytes. Four maximum-sized
frames plus three 4 KiB spacing gaps occupy `0x01fa7000` bytes, fitting in the
32 MiB DDR window and leaving `0x00059000` bytes. The final 4 KiB is reserved
for the scratch test. The shared BAR2 is 64-bit and prefetchable, and this B85
host assigned it below 4 GiB. UEFI software must discover its actual base and
must not assume that placement. If a future design introduces a dedicated
framebuffer BAR, a 32-bit prefetchable BAR remains worth evaluating for older
firmware compatibility.

There are two possible Vivado realizations:

1. The implemented shared map uses translation `0x3c000000`, control IPs at
   `0x3c000000-0x3c07ffff`, and maps MIG at
   `0x3e000000-0x3fffffff`. Host offsets
   `0x00000000-0x03ffffff` are non-aliased, and both VDMA masters use the same
   numeric MIG range.
2. A future separate framebuffer BAR remains the cleanest firmware contract
   if this 7-series XDMA configuration can expose it.

The earlier `0x3f000000` translation aliased half of a 32 MiB BAR. The current
translation is aligned to the full 64 MiB aperture, so all offsets are uniquely
representable. Both mappings must still select the same physical MIG bytes;
the shared numeric address makes that contract explicit.

Use the implemented shared map for the first Shell application and GOP driver;
it has passed the static HWH check, kernel scratch/frame diagnostics, and a
monitor-visible direct-BAR scanout test. Revisit a separate framebuffer BAR
only if firmware compatibility, performance, or ownership testing exposes a
concrete limitation. Do not make the first GOP milestone depend on the
currently stalled XDMA H2C requester.

The current `fpga_hardware/PCIe_wrapper/` export is maintained by hand while
the design is changing. It is the working authority for these values. The
tracked Vivado recreate/export source is synchronized after the hardware is
finished and is required before a reproducible release.

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
6. H2C streaming and native `fpga_drm` uploads work after the new BARs are
   introduced. This gate currently fails on the July 16 export and is not a
   prerequisite for the Shell/direct-GOP stages.

## References

- [AMD PG195 PCIe BARs and Expansion ROM](https://docs.amd.com/r/en-US/pg195-pcie-dma/PCIe-BARs-Tab)
- [AMD PCIe address translation alignment checks](https://docs.amd.com/r/en-US/pg194-axi-bridge-pcie-gen3/Addressing-Checks)
- [AMD PG054 7-series PCIe configuration timing](https://docs.amd.com/r/en-US/pg054-7series-pcie/Configuration-Access-Specification-Requirements)
- [Current hand-maintained hardware export](../../fpga_hardware/PCIe_wrapper/PCIe.hwh)
- [Tracked Vivado recreate flow](../../vivado_project/README.md), to be synchronized after hardware freeze
