# Userspace API

## `fpga_drm.ko`

`fpga_drm.ko` exposes a standard DRM/KMS display. Userspace should treat it as
a normal modeset-capable display with one virtual connector and a whitelist of
supported timings. There are no private DRM ioctls, no private character
devices, no local debugfs files, and no custom sysfs ABI beyond module
parameters. The driver does not expose a render node.

| Interface | Status |
|---|---|
| `/dev/dri/cardN` | Created by `drm_dev_register()`. |
| DRM mode setting | Standard connector/CRTC/plane APIs. |
| Overlay plane | Optional standard KMS overlay plane when `enable_overlay=1`. |
| GEM buffers and mmap | Provided by GEM SHMEM helpers. |
| fbdev | Created by `drm_fbdev_generic_setup(drm, 32)` when `enable_fbdev=1`. |
| `/dev/xdma*` nodes | Not created by `fpga_drm.ko`; those belong to standalone `xdma.ko`. |

## DRM Behavior

| Item | Current behavior |
|---|---|
| Connector type | `DRM_MODE_CONNECTOR_VIRTUAL`. |
| Connection status | Controlled by `connector_connected`; default connected. |
| KMS objects | One explicit CRTC, primary plane, virtual encoder, and virtual connector. |
| Primary plane | Full-screen linear `DRM_FORMAT_XRGB8888`. |
| Overlay plane | Optional linear `DRM_FORMAT_XRGB8888`; no scaling, rotation, alpha, or out-of-bounds placement. |
| Mode list | Whitelist of common 30 Hz and 60 Hz modes up to `148.5 MHz`. |
| Mode validation | Rejects modes outside the whitelist or above `148.5 MHz`. |
| Format | `DRM_FORMAT_XRGB8888` only. |
| Modifier | Linear only. |
| Non-desktop property | Controlled by `connector_non_desktop`; default false. |
| Updates | Full-frame upload by default through `upload_full_frame=1`. |

Supported connector modes:

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

Userspace switches resolution through normal KMS modesets. On enable/modeset,
the driver stops outstanding uploads, programs the video clock wizard, VTC, and
VDMA for the selected mode, and resumes active-size frame uploads.
The 30 Hz modes use the same active resolution and timing totals as their
matching 60 Hz modes with the pixel clock halved. They reduce display-stream
bandwidth, but a full-frame PCIe upload still transfers
`active_width * active_height * 4` bytes.

With the FPGA enumerated as a display-class PCI device and the defaults below,
normal desktop stacks should be able to see the output through `xrandr` or the
usual KMS enumeration tools. `modetest` remains useful for direct KMS testing,
but it is not the primary desktop integration path.

Validated desktop load parameters for the current explicit-KMS driver are:

```sh
sudo modprobe fpga_drm \
  debug_logging=1 \
  enable_overlay=1 \
  composition_backend=cpu \
  connector_connected=1 \
  connector_non_desktop=0 \
  enable_fbdev=1
```

With those parameters, GDM/Xorg has been observed to pick up `/dev/dri/card0`,
set `1280x720@60`, and display the desktop through the FPGA output. In that
desktop state the primary plane is active, but the overlay plane may remain
unused by the compositor. That means the driver is doing KMS scanout and XDMA
frame upload, but not necessarily exercising the CPU overlay-composition path.

## Module Parameters

| Parameter | Type | Default | Effect |
|---|---|---:|---|
| `connector_connected` | bool | `true` | Reports the virtual connector as connected and advertises the whitelist modes. |
| `connector_non_desktop` | bool | `false` | Sets the connector non-desktop property. |
| `enable_fbdev` | bool | `false` | Creates generic fbdev after DRM registration. |
| `h2c_channel` | uint | `0` | XDMA H2C channel used for frame uploads. |
| `stream_ep_addr` | ullong | `0` | Endpoint address passed into XDMA descriptors. |
| `upload_full_frame` | bool | `true` | Uploads the full active-mode frame on every update. |
| `upload_enabled` | bool | `true` | Enables XDMA frame upload; set to false for DRM-only diagnostics. |
| `debug_logging` | bool | `false` | Enables extra connector, modeset, upload, and DMA logs. |
| `configure_pipeline` | bool | `true` | Programs FPGA video IPs through the XDMA bypass BAR during probe and modeset. |
| `enable_overlay` | bool | `false` | Exposes one experimental CPU-composited KMS overlay plane. |
| `composition_backend` | charp | `cpu` | Selects the composition backend. Only `cpu` is implemented; `fpga` is reserved for later hardware composition. |

With `enable_overlay=1`, atomic checks reject unsupported overlay states:
non-`XRGB8888` buffers, non-linear modifiers, scaling, out-of-bounds
rectangles, and disabled-CRTC updates. The CPU backend copies the primary
framebuffer into the existing line staging buffers, overwrites the overlay
rectangle, and submits the composed frame through the same XDMA H2C upload path.

Because `libxdma.c` is linked into `fpga_drm.ko`, its module parameters are
also present. The most relevant are `poll_mode`, `interrupt_mode`,
`enable_st_c2h_credit`, and `desc_blen_max`. `h2c_timeout` and `c2h_timeout`
exist as globals for the XDMA core but are not local `module_param()` entries
in this DRM driver.

## Normal Desktop Checks

```sh
ls /sys/class/drm
xrandr --listproviders
xrandr
```

Expected result: the FPGA card appears as a DRM device, and the virtual
connector exposes the supported-mode whitelist when `connector_connected=1`.

## Direct KMS Test

Use the FPGA driver explicitly. Card numbers can vary, and on this host the
installed `modetest` build does not reliably honor `-D /dev/dri/card0`.
`drm_info` can open the card path directly; `modetest` should use
`-M fpga_drm`.

```sh
drm_info /dev/dri/card0
modetest -M fpga_drm -c -p
modetest -M fpga_drm -s <connector>@<crtc>:1280x720-60@XR24
modetest -M fpga_drm -s <connector>@<crtc>:1280x720-60 -P <primary>@<crtc>:1280x720+0+0@XR24 -F smpte
```

For other advertised modes, replace both the mode and plane size in the
`modetest` command, for example:

```sh
modetest -M fpga_drm -s <connector>@<crtc>:1920x1080-60 -P <primary>@<crtc>:1920x1080+0+0@XR24 -F smpte
modetest -M fpga_drm -s <connector>@<crtc>:1920x1080-30 -P <primary>@<crtc>:1920x1080+0+0@XR24 -F smpte
```

Object IDs are not stable across driver reloads. In the current validated
session they were connector `39`, CRTC `34`, primary plane `31`, and overlay
plane `35`. Reconfirm them with `modetest -M fpga_drm -c -p` after reloads or
driver changes.

If `drm_info /dev/dri/card0` reports `Permission denied`, fix the device-node
ACL first:

```sh
getfacl /dev/dri/card0
sudo setfacl -m u:alpk:rw /dev/dri/card0
```

If userspace can open the card but `modetest` reports `Permission denied`
during modeset/page-flip, another process owns DRM master for that card. That
behavior is normal DRM ownership semantics, not a private driver error.

## Standalone XDMA Interfaces

The repository still contains Xilinx XDMA character-device code. Those
interfaces exist only when standalone `xdma.ko` is built and loaded.

| Node pattern | Purpose |
|---|---|
| `/dev/xdma%d_h2c_%d` | Host-to-card DMA write path. |
| `/dev/xdma%d_c2h_%d` | Card-to-host DMA read path. |
| `/dev/xdma%d_control` | Control/user BAR read, write, mmap, and ioctls. |
| `/dev/xdma%d_events_%d` | User interrupt event read/poll. |
| `/dev/xdma%d_xvc` | Xilinx Virtual Cable ioctl. |
| `/dev/xdma%d_bypass*` | Descriptor bypass interface. |

Do not load standalone `xdma.ko` at the same time as `fpga_drm.ko` for the
same PCI function.

## XDMA ILA Validation

The current hardware no longer contains the video-output ILA at the HDMI output
IP. The remaining validation probe for live frame traffic is the XDMA ILA:

```text
PCIe_i/xdma_ila/inst/ila_lib
```

The repo-local capture script targets this ILA by default:

```sh
mkdir -p tmp_ila_capture
/home/alpk/xilinx/Vivado/2023.2/bin/vivado -mode batch -nojournal -nolog -notrace \
  -source scripts/capture_video_stream_ila.tcl \
  -tclargs fpga_hardware/PCIe_wrapper/PCIe_wrapper.ltx tmp_ila_capture tvalid xdma
```

Current XDMA stream probe mapping:

| Signal | ILA probe |
|---|---|
| `TDATA` | `PCIe_i/xdma_ila/inst/net_slot_0_axis_tdata` |
| `TKEEP` | `PCIe_i/xdma_ila/inst/net_slot_0_axis_tkeep` |
| `TVALID` | `PCIe_i/xdma_ila/inst/net_slot_0_axis_tvalid` |
| `TREADY` | `PCIe_i/xdma_ila/inst/net_slot_0_axis_tready` |
| `TLAST` | `PCIe_i/xdma_ila/inst/net_slot_0_axis_tlast` |

After arming the ILA on `TVALID`, generate traffic with a direct KMS upload:

```sh
modetest -M fpga_drm -s <connector>@<crtc>:1280x720-60 \
  -P <primary>@<crtc>:1280x720+0+0@XR24 -F smpte
```

A validated capture on 2026-06-10 wrote
`tmp_ila_capture/xdma_ila_tvalid.csv` and showed:

```text
samples=1024
tvalid=797
tready=1024
handshake=797
tlast=2
unique_tdata=12
nonzero_tdata=1024
```
