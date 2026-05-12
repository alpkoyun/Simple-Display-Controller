# Hardware Interface

## Summary

The Linux driver controls only the PCIe/XDMA transport and DRM display surface.
It does not control the FPGA video IP. MicroBlaze is responsible for setting up
the internal video pipeline that consumes the H2C stream and drives HDMI.

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
| BAR mappings | `libxdma.c:map_bars()` | Used internally by the XDMA core. `fpga_drm_drv.c` does not directly access BAR registers. |
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

The hardware side is expected to accept 720 line packets and present them as
one video frame. There is no Linux-side frame-swap register, VDMA programming,
or HDMI timing control in this driver.

## Register Access

`fpga_drm_drv.c` does not call `ioread32()` or `iowrite32()` directly. All XDMA
register access is inside `XDMA_driver/xdma/libxdma.c`.

| Register group | Used for |
|---|---|
| XDMA config BAR | Engine discovery, interrupt setup, and SGDMA descriptor start registers. |
| H2C engine registers | Start/stop, status, descriptor completion, alignment, and interrupt control. |
| SGDMA registers | First descriptor address and adjacent descriptor count. |
| Interrupt registers | MSI-X/MSI/legacy channel and user interrupt control. |

Standalone `xdma.ko` can expose BAR access through `/dev/xdma*_control`, but
that interface is not created by `fpga_drm.ko`.

## Not in Scope

| Hardware function | Linux driver status |
|---|---|
| MicroBlaze firmware control | Not implemented here. |
| VDMA/video IP register setup | Not implemented here. |
| HDMI PHY/display timing control | Not implemented here. |
| I2C/SPI/GPIO/clock/reset control | No APIs are used by `fpga_drm_drv.c`. |
| C2H video capture | Not used by this display driver. |
