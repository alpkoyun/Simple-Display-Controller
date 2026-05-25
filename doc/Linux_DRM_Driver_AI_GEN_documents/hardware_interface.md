# Hardware Interface

## Summary

The Linux driver owns the PCIe/XDMA transport, DRM display surface, and FPGA
video-IP setup. During probe, `fpga_drm.ko` opens XDMA, obtains the mapped
XDMA bypass BAR, and programs the AXI-Lite slaves listed in
`fpga_hardware/PCIe_wrapper/PCIe.hwh`.

The expected hardware contract is:

- the FPGA enumerates as a PCI display-class device for normal desktop
  integration;
- the PCI function contains Xilinx XDMA with an H2C AXI-stream engine;
- the stream sink accepts one 1280-pixel line per packet;
- each line packet is 5120 bytes of XRGB8888 storage;
- EOP/TLAST is asserted once per line;
- the FPGA stages one complete 1280x720 frame as 720 line buffers.

## Linux-Visible Resources

| Resource | Acquired by | Notes |
|---|---|---|
| PCI function | `fpga_drm_pci_driver` | Binds Xilinx XDMA PCI IDs. Only one PCI driver can own the function. |
| BAR mappings | `libxdma.c:map_bars()` | XDMA maps the config/user/bypass BARs; `fpga_drm_drv.c` uses the bypass BAR for AXI-Lite video-IP registers in this hardware. |
| DMA mask | `libxdma.c:set_dma_mask()` | Tries 64-bit DMA and falls back as needed. |
| H2C engines | `libxdma.c:probe_engines()` | `fpga_drm` uses the configured `h2c_channel`, default 0. |
| IRQ vectors | `libxdma.c:irq_setup()` | MSI-X, MSI, or legacy IRQ depending on platform and `interrupt_mode`. |
| DRM connector/pipe | `fpga_drm_modeset_init()` | Software DRM/KMS objects; not FPGA MMIO resources. |

## Stream Format

| Item | Value |
|---|---|
| Resolution | 1280x720 |
| Line bytes | 1280 * 4 = 5120 |
| Frame bytes | 5120 * 720 = 3,686,400 |
| Pixel storage | 32 bits per pixel |
| Pixel meaning | XRGB8888; X byte ignored by FPGA, RGB in low 24 bits |
| Packet boundary | One AXI-stream packet per line |
| End marker | `XDMA_DESC_EOP` on the descriptor that ends each line |

`xdma_xfer_submit_lines_nowait()` validates that the submitted SG table covers
exactly `line_count` lines of `line_size` bytes. It sets EOP on every line
boundary and STOP/COMPLETED on the final descriptor.

## Frame Buffering Contract

The driver allocates 720 host line buffers and keeps a persistent SG table with
one entry per line. On each upload, it copies the current DRM framebuffer into
those line buffers and submits the complete frame to XDMA.

The hardware side accepts 720 line packets into VDMA S2MM. The driver programs
the VDMA frame ring in DDR, starts S2MM/MM2S, configures pixel unpack and color
conversion, programs the HDMI I2C LUT, and enables the VTC 1280x720@60
generator.

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

The fixed bypass BAR AXI address map is:

| IP | AXI address |
|---|---:|
| Color convert | `0x00000000` |
| Pixel unpack | `0x00010000` |
| AXI IIC | `0x00020000` |
| AXI VDMA | `0x00040000` |
| VTC | `0x00050000` |
| Video clock wizard | `0x00060000` |
| Video lock GPIO | `0x00070000` |
| DDR aperture in bypass map | `0x00080000` |

VDMA configuration uses AXI VDMA offsets relative to `0x00040000`. It does
not use XDMA engine/config offsets for VDMA control. The frame addresses
programmed into VDMA are still DDR addresses, currently starting at
`0x81000000`; those are not host MMIO offsets.

## Not in Scope

| Hardware function | Linux driver status |
|---|---|
| MicroBlaze firmware control | Replaced by host-side AXI-Lite setup for this pipeline. |
| C2H video capture | Not used by this display driver. |
| Dynamic modes | Not implemented; v1 remains fixed at 1280x720@60. |
