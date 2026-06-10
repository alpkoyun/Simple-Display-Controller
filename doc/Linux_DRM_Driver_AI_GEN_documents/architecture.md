# FPGA DRM Driver Architecture

This repository builds a whitelist-mode Linux DRM/KMS driver for the FPGA PCIe
HDMI design. The current driver looks like one normal KMS display to userspace
while using XDMA H2C AXI-stream to deliver each rendered frame to the FPGA.

## Current Contract

- Supported modes are `640x480`, `800x600`, `1024x768`, `1280x720`,
  `1280x1024`, and `1920x1080`, each at 60 Hz and nominal 30 Hz.
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
device with explicit KMS objects: one CRTC, one primary plane, an optional
overlay plane, one virtual encoder, and one virtual connector.
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
`fpga_hardware/PCIe_wrapper/PCIe_wrapper.ltx`. The current bitstream no longer
contains the HDMI video-output ILA, so live frame-traffic validation uses the
XDMA ILA at `PCIe_i/xdma_ila/inst/ila_lib`; after a `modetest -F smpte` upload,
the ILA should show `tvalid && tready` handshakes, nonzero `tdata`, and more
than one unique `tdata` value.

## Frame Upload Model

The current upload path is asynchronous at frame granularity:

1. Plane atomic checks validate the primary and optional overlay framebuffer
   state.
2. `fpga_drm_crtc_atomic_enable()` programs the selected whitelist mode on
   enable/modeset.
3. `fpga_drm_crtc_atomic_flush()` snapshots the primary and optional overlay
   plane state, then queues `upload_work`.
4. `fpga_drm_upload_work()` serializes against any in-flight frame.
5. `fpga_drm_copy_frame()` copies the active-mode XRGB8888 primary framebuffer
   into DRM-managed max-width line buffers and CPU-composites the optional
   overlay rectangle when enabled and active.
6. `xdma_xfer_submit_lines_nowait()` submits the active-mode SG-table view to
   the streaming H2C engine.
7. `fpga_drm_xdma_done()`, `fpga_drm_dma_complete_work()`, and
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
