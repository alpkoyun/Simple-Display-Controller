# Bring-Up Procedure

## Build

```sh
make -C Linux_DRM_Driver/fpga_drm
```

The output module is `Linux_DRM_Driver/fpga_drm/fpga_drm.ko`.

## Normal Load

Unload the standalone Xilinx XDMA module first. It matches the same PCI
function and conflicts with `fpga_drm.ko`.

```sh
/home/alpk/.codex/skills/ax7203-fpga-board-debug/scripts/load_probe_fpga_drm.sh --repo /home/alpk/codex_workspaces/Simple-Display-Controller --load --unload-conflicts --params "debug_logging=1"
dmesg | tail -80
ls /sys/class/drm
```

The default load exposes the connector as connected, desktop-capable, and
fbdev-disabled:

```text
connector_connected=1
connector_non_desktop=0
enable_fbdev=0
upload_enabled=1
upload_full_frame=1
configure_pipeline=1
```

On load, `dmesg` should show the XDMA MMIO BAR index/length, the fixed
AXI-Lite register ranges from `PCIe.hwh`, and the DDR frame ring used by VDMA.
The current hardware exposes the video AXI-Lite aperture through the XDMA
bypass BAR. Expected register ranges are:

```text
color_convert      0x00000000-0x0000ffff
pixel_unpack       0x00010000-0x0001ffff
axi_iic            0x00020000-0x0002ffff
axi_vdma_0         0x00040000-0x0004ffff
hdmi_out_v_tc_0    0x00050000-0x0005ffff
video_clk_wiz      0x00060000-0x0006ffff
video_lock_gpio    0x00070000-0x0007ffff
```

The driver then configures pixel unpack, color conversion, VDMA S2MM/MM2S at
`0x00040000`, HDMI I2C, VTC, and prints VDMA/VTC/video-lock readbacks before
registering the DRM device. The VDMA frame addresses remain DDR addresses,
currently starting at `0x81000000`; they are not bypass BAR offsets.

The FPGA should appear as a DRM card with one 1280x720 output. If the desktop
stack does not list it, verify that the FPGA PCIe function enumerates as a
display-class device and that `fpga_drm.ko` owns the PCI function.

## Optional Direct KMS Test

Use explicit driver selection. `drm_info` can open the card path directly, but
on this host the installed `modetest` build may ignore `-D /dev/dri/card0` and
fall back to its built-in driver list. Use `-M fpga_drm` for direct KMS tests.

```sh
drm_info /dev/dri/card0
modetest -M fpga_drm -c -p
modetest -M fpga_drm -s 31@34:1280x720-60@XR24
modetest -M fpga_drm -s 31@34:1280x720-60 -P 32@34:1280x720+0+0@XR24 -F smpte
```

`failed to set gamma: Function not implemented` is acceptable for this minimal
driver when the mode set proceeds.

If `drm_info /dev/dri/card0` fails with `Permission denied`, fix device-node
access before assuming a DRM-master problem:

```sh
getfacl /dev/dri/card0
sudo setfacl -m u:alpk:rw /dev/dri/card0
```

If the card opens but `modetest` fails with `Permission denied`, check DRM
master ownership:

```sh
sudo fuser -v /dev/dri/card*
```

Run the test from a text console or stop the display manager only for that
manual KMS test:

```sh
sudo systemctl stop display-manager
```

This is a diagnostic alternative, not the expected normal operating mode.

## Diagnostic Loads

DRM-only enumeration without sending frames:

```sh
sudo rmmod fpga_drm
sudo insmod Linux_DRM_Driver/fpga_drm/fpga_drm.ko upload_enabled=0 debug_logging=1
```

This still configures the FPGA video pipeline. To isolate DRM registration from
all video-IP register writes, add `configure_pipeline=0`.

Manual safe visibility mode:

```sh
sudo rmmod fpga_drm
sudo insmod Linux_DRM_Driver/fpga_drm/fpga_drm.ko <diagnostic connector/fbdev overrides> debug_logging=1
```

Use these only to isolate userspace enumeration, fbdev, or upload-path issues.

## ILA Video Validation

Capture `dmesg` around module load, first modeset, first upload, and unload.
For hardware validation, capture the FPGA video-output ILA while forcing a KMS
test pattern or desktop update.

The matching probes file is
`fpga_hardware/PCIe_wrapper/PCIe_wrapper.ltx`. The expected video ILA cell is
`PCIe_i/hdmi_out/video_stream_ila/inst/ila_lib`. Trigger on `tuser` to confirm
start-of-frame timing and on `tvalid` to confirm live stream data:

```sh
/home/alpk/xilinx/Vivado/2023.2/bin/vivado -mode batch -nojournal -nolog -notrace \
  -source /home/alpk/.codex/skills/ax7203-fpga-board-debug/scripts/capture_video_stream_ila.tcl \
  -tclargs fpga_hardware/PCIe_wrapper/PCIe_wrapper.ltx tmp_ila_capture tuser

/home/alpk/xilinx/Vivado/2023.2/bin/vivado -mode batch -nojournal -nolog -notrace \
  -source /home/alpk/.codex/skills/ax7203-fpga-board-debug/scripts/capture_video_stream_ila.tcl \
  -tclargs fpga_hardware/PCIe_wrapper/PCIe_wrapper.ltx tmp_ila_capture tvalid
```

Successful upload should show `tvalid && tready > 0`; after the SMPTE
`modetest` pattern, the `tvalid` capture should show nonzero 24-bit pixel data
and more than one unique `tdata` value.
