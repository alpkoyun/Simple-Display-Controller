# FPGA DRM Driver Architecture

This repository builds a whitelist-mode Linux DRM/KMS driver for the FPGA PCIe
HDMI design. The current driver looks like one normal KMS display to userspace
while using XDMA H2C AXI-stream to deliver each rendered frame to the FPGA.

## Current Contract

- Supported modes are `640x480@60`, `800x600@60`, `1024x768@60`,
  `1280x720@60`, `1280x1024@60`, and `1920x1080@60`.
- The maximum advertised pixel clock is `148.5 MHz`.
- The only supported framebuffer format is `DRM_FORMAT_XRGB8888`.
- The FPGA consumes 32-bit pixels. Bits 31:24 are ignored; RGB is carried in
  bits 23:0.
- One complete frame is `active_width * active_height * 4` bytes.
- The XDMA H2C stream is packetized per video line. The driver submits
  `active_height` line-sized packets for each frame, with EOP/TLAST at the end
  of each line.
- The FPGA stores incoming pixels in the VDMA DDR frame ring before video
  output.
- Linux configures the FPGA video pipeline through the XDMA AXI-Lite bypass
  BAR in this bitstream. VDMA programming targets `axi_vdma_0` at bypass
  offset `0x00040000`, using AXI VDMA register offsets rather than XDMA
  engine/config offsets.

## Linux Model

`fpga_drm.ko` is a PCI DRM driver. It binds the Xilinx PCIe XDMA function,
opens the vendored XDMA core with `xdma_device_open()`, and registers one DRM
device with one virtual connector and one `drm_simple_display_pipe`.
Before DRM registration it requires a mapped XDMA MMIO BAR and runs
`fpga_drm_configure_static_pipeline()` to validate the bypass BAR map and
program static video IP state. On each KMS enable/modeset,
`fpga_drm_program_mode()` programs the video clock wizard, VDMA S2MM/MM2S
frame buffers, VTC, and debug readbacks for the selected whitelist mode.

Userspace interacts with the driver through standard DRM/KMS APIs:

- `/dev/dri/cardN` from `drm_dev_register()`
- GEM SHMEM framebuffer allocation and mapping
- atomic modeset/page-flip paths
- generic fbdev setup when `enable_fbdev=1`

The driver has no private ioctl ABI and does not create the standalone
`/dev/xdma*` character devices.

The validated userspace path is `drm_info /dev/dri/card0` and
`modetest -M fpga_drm`. On this host, this `modetest` build does not reliably
honor `-D /dev/dri/card0`, so the module selector is the preferred direct KMS
test path.

Vivado ILA validation uses the matching
`fpga_hardware/PCIe_wrapper/PCIe_wrapper.ltx`. The current bitstream exposes a
video stream ILA at `PCIe_i/hdmi_out/video_stream_ila/inst/ila_lib`; after a
`modetest -F smpte` upload, the ILA should show `tvalid && tready` handshakes
and nonzero 24-bit `tdata` values.

## Frame Upload Model

The current upload path is asynchronous at frame granularity:

1. `fpga_drm_pipe_enable()` or `fpga_drm_pipe_update()` marks the current
   framebuffer dirty.
2. `fpga_drm_mark_dirty()` stores the framebuffer reference and shadow-plane
   map, then schedules `upload_work`.
3. `fpga_drm_upload_work()` serializes against any in-flight frame.
4. `fpga_drm_copy_frame()` copies the active-mode XRGB8888 framebuffer into
   DRM-managed max-width line buffers.
5. `xdma_xfer_submit_lines_nowait()` submits the active-mode SG-table view to
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
enable_fbdev=0
h2c_channel=0
stream_ep_addr=0
upload_full_frame=1
upload_enabled=1
debug_logging=0
configure_pipeline=1
```

The FPGA PCIe function should enumerate as a display-class device for normal
desktop stacks to treat it as a display. The driver still binds by Xilinx PCI
device IDs, so the class code is a platform integration requirement rather than
a private driver ABI.
