# FPGA DRM Driver Architecture

This repository builds a fixed-purpose Linux DRM/KMS driver for the FPGA PCIe
HDMI design. The current driver is meant to look like one normal 1280x720
display to userspace while using XDMA H2C AXI-stream to deliver each rendered
frame to the FPGA.

## Current Contract

- The only supported mode is 1280x720 at 60 Hz.
- The only supported framebuffer format is `DRM_FORMAT_XRGB8888`.
- The FPGA consumes 32-bit pixels. Bits 31:24 are ignored; RGB is carried in
  bits 23:0.
- One complete frame is 1280 * 720 * 4 = 3,686,400 bytes.
- The XDMA H2C stream is packetized per video line. The driver submits 720
  line-sized packets for each frame, with EOP/TLAST at the end of each line.
- The FPGA stores or stages one frame as 720 line buffers before video output.
- MicroBlaze configures the FPGA video pipeline. This Linux driver does not
  program clocks, resets, HDMI, VDMA, or custom video IP registers.

## Linux Model

`fpga_drm.ko` is a PCI DRM driver. It binds the Xilinx PCIe XDMA function,
opens the vendored XDMA core with `xdma_device_open()`, and registers one DRM
device with one virtual connector and one `drm_simple_display_pipe`.

Userspace interacts with the driver through standard DRM/KMS APIs:

- `/dev/dri/cardN` from `drm_dev_register()`
- GEM SHMEM framebuffer allocation and mapping
- atomic modeset/page-flip paths
- generic fbdev setup when `enable_fbdev=1`

The driver has no private ioctl ABI and does not create the standalone
`/dev/xdma*` character devices.

## Frame Upload Model

The current upload path is asynchronous at frame granularity:

1. `fpga_drm_pipe_enable()` or `fpga_drm_pipe_update()` marks the current
   framebuffer dirty.
2. `fpga_drm_mark_dirty()` stores the framebuffer reference and shadow-plane
   map, then schedules `upload_work`.
3. `fpga_drm_upload_work()` serializes against any in-flight frame.
4. `fpga_drm_copy_frame()` copies the 1280x720 XRGB8888 framebuffer into 720
   DRM-managed line buffers.
5. `xdma_xfer_submit_lines_nowait()` submits the 720-entry frame SG table to
   the streaming H2C engine.
6. `fpga_drm_xdma_done()`, `fpga_drm_dma_complete_work()`, and
   `fpga_drm_dma_timeout_work()` finish or fail the in-flight frame.

Only one frame upload is in flight. If userspace updates while DMA is busy, the
driver records `upload_pending` and queues the latest framebuffer again after
completion.

## Current Module Defaults

```text
connector_connected=1
connector_non_desktop=0
enable_fbdev=1
h2c_channel=0
stream_ep_addr=0
upload_full_frame=1
upload_enabled=1
debug_logging=0
```

The FPGA PCIe function should enumerate as a display-class device for normal
desktop stacks to treat it as a display. The driver still binds by Xilinx PCI
device IDs, so the class code is a platform integration requirement rather than
a private driver ABI.
