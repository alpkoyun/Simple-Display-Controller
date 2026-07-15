# Hardware Interface

## Summary

The Linux driver owns the PCIe/XDMA transport, DRM display surface, and FPGA
video-IP setup. During probe, `fpga_drm.ko` opens XDMA, obtains the mapped
XDMA bypass BAR, validates the AXI-Lite slaves listed in
`fpga_hardware/PCIe_wrapper/PCIe.hwh`, and configures static video IP state.
The selected video timing is programmed on KMS enable/modeset.

The expected hardware contract is:

- the FPGA enumerates as a PCI display-class device for normal desktop
  integration;
- the PCI function contains Xilinx XDMA with an H2C AXI-stream engine;
- the stream sink accepts one active XRGB8888 line per packet;
- each line packet is `active_width * 4` bytes;
- EOP/TLAST is asserted once per line;
- the FPGA stages one complete active-mode frame into the VDMA DDR ring.

## Linux-Visible Resources

| Resource | Acquired by | Notes |
|---|---|---|
| PCI function | `fpga_drm_pci_driver` | Binds Xilinx XDMA PCI IDs. Only one PCI driver can own the function. |
| BAR mappings | `libxdma.c:map_bars()` | The embedded XDMA layer maps at most the low 1 MiB prefix of each BAR. `fpga_drm_drv.c` uses the bypass prefix for video registers and maps bounded DDR ranges only for the opt-in diagnostic. |
| DMA mask | `libxdma.c:set_dma_mask()` | Tries 64-bit DMA and falls back as needed. |
| H2C engines | `libxdma.c:probe_engines()` | `fpga_drm` uses the configured `h2c_channel`, default 0. |
| IRQ vectors | `libxdma.c:irq_setup()` | MSI-X, MSI, or legacy IRQ depending on platform and `interrupt_mode`. |
| DRM connector/pipe | `fpga_drm_modeset_init()` | Software DRM/KMS objects; not FPGA MMIO resources. |

## Stream Format

| Item | Value |
|---|---|
| Resolution | Whitelist up to 1920x1080 |
| Line bytes | `active_width * 4`, maximum `1920 * 4 = 7680` |
| Frame bytes | `line_bytes * active_height`, maximum `8,294,400` |
| Pixel storage | 32 bits per pixel |
| Pixel meaning | XRGB8888; X byte ignored by FPGA, RGB in low 24 bits |
| Packet boundary | One AXI-stream packet per line |
| End marker | `XDMA_DESC_EOP` on the descriptor that ends each line |

`xdma_xfer_submit_lines_nowait()` validates that the submitted SG table covers
exactly `line_count` lines of `line_size` bytes. It sets EOP on every line
boundary and STOP/COMPLETED on the final descriptor.

## Frame Buffering Contract

The driver allocates 1080 host line buffers, each sized for a 1920-pixel
XRGB8888 line, and keeps a persistent SG table. On each upload, it copies only
the active mode's width and height into those line buffers, sizes the active SG
view to the active line byte count, and submits the complete active-mode frame
to XDMA.

The hardware side accepts active-mode line packets into VDMA S2MM. The driver
programs the VDMA frame ring in DDR, starts S2MM/MM2S, configures pixel unpack
and color conversion, programs the HDMI I2C LUT, and enables the VTC generator
for the selected mode. The DDR frame spacing is based on the largest supported
frame so mode switches cannot make frame stores overlap.

## Register Access

XDMA config/control registers stay inside `XDMA_driver/xdma/libxdma.c`.
Video-pipeline registers are accessed by `fpga_drm_drv.c` through the XDMA
MMIO BAR. This bitstream exposes that AXI-Lite aperture as the bypass BAR,
returned by `xdma_device_bypass_bar()`. The driver falls back to the user BAR
only for other designs that expose the AXI-Lite aperture there.

| Register group | Used for |
|---|---|
| XDMA config BAR | Engine discovery, interrupt setup, and SGDMA descriptor start registers. |
| H2C engine registers | Start/stop, status, descriptor completion, alignment, and interrupt control. |
| SGDMA registers | First descriptor address and adjacent descriptor count. |
| Interrupt registers | MSI-X/MSI/legacy channel and user interrupt control. |
| XDMA bypass BAR AXI-Lite window | Host writes to the FPGA video IP address map. |

The current XDMA bypass configuration uses `0x3f000000` as its AXI translation
base. Low BAR offsets reach the control IPs as intended:

| Resource | AXI address | Host BAR offset |
|---|---:|---:|
| Pixel unpack | `0x3f000000-0x3f00ffff` | `0x00000000-0x0000ffff` |
| VTC | `0x3f010000-0x3f01ffff` | `0x00010000-0x0001ffff` |
| AXI IIC | `0x3f020000-0x3f02ffff` | `0x00020000-0x0002ffff` |
| AXI UART Lite | `0x3f030000-0x3f03ffff` | `0x00030000-0x0003ffff` |
| AXI VDMA | `0x3f040000-0x3f04ffff` | `0x00040000-0x0004ffff` |
| Color convert | `0x3f050000-0x3f05ffff` | `0x00050000-0x0005ffff` |
| Video clock wizard | `0x3f060000-0x3f06ffff` | `0x00060000-0x0006ffff` |
| Video lock GPIO | `0x3f070000-0x3f07ffff` | `0x00070000-0x0007ffff` |
| DDR bypass slice | `0x3f800000-0x3fffffff` | `0x00800000-0x00ffffff` |

The updated DDR row uses the non-aliased lower half of the BAR. ILA evidence
from the previous export showed that XDMA combines its translation bits with
the host offset rather than performing unrestricted arithmetic addition. With
translation `0x3f000000`, offsets through `0x00ffffff` reproduce their intended
AXI addresses. The BAR's upper half, `0x01000000-0x01ffffff`, aliases and is
intentionally unused.

The VDMA masters do not pass through this PCIe translation. Their independent
1 GiB DDR address range remains `0x40000000-0x7fffffff`. Normal operation still
programs four maximum-sized frame stores at `0x41000000-0x42fa6fff`; shrinking
the bypass-visible slice does not reduce that ring.

With `ddr_bypass_test=1 upload_enabled=0`, the driver temporarily programs
VDMA with one frame, performs a bounded write/read/restore check, and writes a
color-bar frame. The bypass master writes MIG offset zero at AXI `0x3f800000`;
VDMA reaches the same offset at its own master address `0x40000000`. This
diagnostic is off by default; normal Linux updates retain the four-frame ring
and use XDMA H2C streaming.

The generated export and live readback remain the authority. A future
dedicated framebuffer BAR may provide a cleaner firmware contract, but equal
numeric AXI addresses across the bypass and VDMA masters are not required.

## Not in Scope

| Hardware function | Linux driver status |
|---|---|
| MicroBlaze firmware control | Replaced by host-side AXI-Lite setup for this pipeline. |
| C2H video capture | Not used by this display driver. |
| Arbitrary modelines | Not supported; the driver accepts only the whitelist at or below `148.5 MHz`. |
