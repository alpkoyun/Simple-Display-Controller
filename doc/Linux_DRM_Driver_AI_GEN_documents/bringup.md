# Bring-Up Procedure

## Build

```sh
scripts/check_fpga_hardware_contract.py
make -C Linux_DRM_Driver/fpga_drm
```

The default checker validates the static H2C/VDMA address and connection
contract; it does not prove that live H2C requester traffic completes. Before
a GOP or direct-DDR test, the stronger gate must also pass:

```sh
scripts/check_fpga_hardware_contract.py --require-ddr-bypass
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

On load, `dmesg` should show the XDMA MMIO BAR index/length, the
AXI-Lite register ranges from `PCIe.hwh`, and the DDR frame ring used by VDMA.
The current hardware exposes the video AXI-Lite aperture through the XDMA
bypass BAR. The bridge translates host BAR offset zero to AXI `0x3c000000`.
Expected ranges are:

```text
Resource            AXI range                   Host BAR offset
pixel_unpack        0x3c000000-0x3c00ffff       0x00000000-0x0000ffff
hdmi_out_v_tc_0     0x3c010000-0x3c01ffff       0x00010000-0x0001ffff
axi_iic             0x3c020000-0x3c02ffff       0x00020000-0x0002ffff
axi_uartlite        0x3c030000-0x3c03ffff       0x00030000-0x0003ffff
axi_vdma_0          0x3c040000-0x3c04ffff       0x00040000-0x0004ffff
color_convert       0x3c050000-0x3c05ffff       0x00050000-0x0005ffff
video_clk_wiz       0x3c060000-0x3c06ffff       0x00060000-0x0006ffff
video_lock_gpio     0x3c070000-0x3c07ffff       0x00070000-0x0007ffff
DDR frame store     0x3e000000-0x3fffffff       0x02000000-0x03ffffff
```

During probe the driver validates the BAR ranges, configures static video IP
state such as pixel unpack, color conversion, HDMI I2C, and video-lock GPIO,
then registers the DRM device. VDMA, VTC, and the video clock wizard are
programmed during KMS enable/modeset for the selected mode. The VDMA MM2S and
S2MM masters use the same `0x3e000000-0x3fffffff` DDR range as the bypass
master. Normal operation keeps four frames at
`0x3e000000-0x3ffa6fff`.

On the current July 16 export, programming this normal configuration succeeds
but live XDMA H2C uploads time out with zero completed descriptors. The stream
ILA shows `TREADY=1`, `TVALID=0`. Treat normal load as an H2C recovery test until
completed descriptors and stream handshakes return.

The July 16 export uses a 64 MiB BAR aligned to translation `0x3c000000`.
Every offset from `0x00000000` through `0x03ffffff` is non-aliased. Control IPs
occupy low offsets and the 32 MiB MIG segment occupies
`0x02000000-0x03ffffff`.

## Direct DDR Bypass Diagnostic

Run this only with an export that passes
`scripts/check_fpga_hardware_contract.py --require-ddr-bypass`. The endpoint
must enumerate with the intended BAR, and every used address must be
representable by XDMA's translation/base composition.

```sh
sudo -n modprobe -r fpga_drm
sudo -n modprobe -r xdma
sudo -n modprobe fpga_drm debug_logging=1 upload_enabled=0 configure_pipeline=1 ddr_bypass_test=1
sudo -n dmesg | grep -Ei 'fpga_drm|BAR|hardware contract|DDR bypass|VDMA'
```

Success requires a `DDR bypass scratch test passed` line for bypass AXI
`0x3ffff000` at BAR offset `0x03fff000`, a test-pattern line after the first
modeset, clean VDMA/VTC/clock readbacks, and the color-bar pattern on the
monitor. In this diagnostic, the CPU writes the frame through bypass AXI
`0x3e000000`, and VDMA scans the same address `0x3e000000`.
Reloading without `ddr_bypass_test=1` restores the normal four-frame ring. The
driver deliberately rejects
`ddr_bypass_test=1` unless H2C uploads are disabled, preventing the stream path
from overwriting the diagnostic frame.

An earlier 2026-07-15 export failed safely before modeset:

```text
DDR bypass scratch mismatch word=0 expected=0x55aa00ff got=0x00000044
probe of 0000:01:00.0 failed with error -5
```

ILA subsequently proved that the old host offset `0x01fff000` aliased to
unmapped AXI `0x3ffff000`. The current aligned 64 MiB export removes that
aliasing condition, passes the stronger static checker, and has passed scratch
readback plus correct visible `1280x720@60` direct-frame scanout.

The FPGA should appear as a DRM card with these connector modes:

| Mode | Pixel clock |
|---|---:|
| `640x480@60` | `25.175 MHz` |
| `640x480@30` | `12.587 MHz` |
| `800x600@60` | `40.000 MHz` |
| `800x600@30` | `20.000 MHz` |
| `1024x768@60` | `65.000 MHz` |
| `1024x768@30` | `32.500 MHz` |
| `1280x720@60` | `74.250 MHz` |
| `1280x720@30` | `37.125 MHz` |
| `1280x1024@60` | `108.000 MHz` |
| `1280x1024@30` | `54.000 MHz` |
| `1920x1080@60` | `148.500 MHz` |
| `1920x1080@30` | `74.250 MHz` |

If the desktop stack does not list it, verify that the FPGA PCIe function
enumerates as a display-class device and that `fpga_drm.ko` owns the PCI
function.

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

For mode smoke testing, repeat the `-s`/`-P` pair with each advertised mode and
the matching plane size, for example `1920x1080-60@XR24` and
`1920x1080+0+0@XR24`. To test the lower-bandwidth 1080p path, use
`1920x1080-30@XR24` with the same `1920x1080+0+0@XR24` plane size.

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

This still configures static video IP state and will program timing on a KMS
modeset. To isolate DRM registration from all video-IP register writes, add
`configure_pipeline=0`.

Manual safe visibility mode:

```sh
sudo rmmod fpga_drm
sudo insmod Linux_DRM_Driver/fpga_drm/fpga_drm.ko <diagnostic connector/fbdev overrides> debug_logging=1
```

Use these only to isolate userspace enumeration, fbdev, or upload-path issues.

## XDMA ILA Video Validation

Capture `dmesg` around module load, first modeset, first upload, and unload.
For hardware validation on the current bitstream, capture the XDMA AXI-stream
ILA while forcing a KMS test pattern or desktop update. The older HDMI
video-output ILA is not present in the current hardware.

The matching probes file is `fpga_hardware/PCIe_wrapper/PCIe_wrapper.ltx`. The
current ILA cell is `PCIe_i/xdma_ila/inst/ila_lib`. Use the repo-local capture
script and trigger on `tvalid` to confirm live stream data:

```sh
/home/alpk/xilinx/Vivado/2023.2/bin/vivado -mode batch -nojournal -nolog -notrace \
  -source scripts/capture_video_stream_ila.tcl \
  -tclargs fpga_hardware/PCIe_wrapper/PCIe_wrapper.ltx tmp_ila_capture tvalid xdma
```

While the ILA is armed, generate a direct KMS upload:

```sh
modetest -M fpga_drm -s <connector>@<crtc>:1280x720-60 \
  -P <primary>@<crtc>:1280x720+0+0@XR24 -F smpte
```

Successful upload should show `tvalid && tready > 0`, nonzero `tdata`, and more
than one unique `tdata` value. The 2026-06-10 validation capture triggered on
`net_slot_0_axis_tvalid` and showed 797 valid/ready handshakes in 1024 samples,
with nonzero `net_slot_0_axis_tdata`.
