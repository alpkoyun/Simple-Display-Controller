# Userspace API

## `fpga_drm.ko`

`fpga_drm.ko` exposes a standard DRM/KMS display. Userspace should treat it as
a fixed 1280x720 display with one virtual connector. There are no private DRM
ioctls, no private character devices, no local debugfs files, and no custom
sysfs ABI beyond module parameters.

| Interface | Status |
|---|---|
| `/dev/dri/cardN` | Created by `drm_dev_register()`. |
| DRM mode setting | Standard connector/CRTC/plane APIs. |
| GEM buffers and mmap | Provided by GEM SHMEM helpers. |
| fbdev | Created by `drm_fbdev_generic_setup(drm, 32)` when `enable_fbdev=1`. |
| `/dev/xdma*` nodes | Not created by `fpga_drm.ko`; those belong to standalone `xdma.ko`. |

## DRM Behavior

| Item | Current behavior |
|---|---|
| Connector type | `DRM_MODE_CONNECTOR_VIRTUAL`. |
| Connection status | Controlled by `connector_connected`; default connected. |
| Mode list | One fixed 1280x720@60 mode. |
| Mode validation | Rejects anything except 1280x720 at 60 Hz. |
| Format | `DRM_FORMAT_XRGB8888` only. |
| Modifier | Linear only. |
| Non-desktop property | Controlled by `connector_non_desktop`; default false. |
| Updates | Full-frame upload by default through `upload_full_frame=1`. |

With the FPGA enumerated as a display-class PCI device and the defaults below,
normal desktop stacks should be able to see the output through `xrandr` or the
usual KMS enumeration tools. `modetest` remains useful for direct KMS testing,
but it is not the primary desktop integration path.

## Module Parameters

| Parameter | Type | Default | Effect |
|---|---|---:|---|
| `connector_connected` | bool | `true` | Reports the virtual connector as connected and advertises the fixed mode. |
| `connector_non_desktop` | bool | `false` | Sets the connector non-desktop property. |
| `enable_fbdev` | bool | `true` | Creates generic fbdev after DRM registration. |
| `h2c_channel` | uint | `0` | XDMA H2C channel used for frame uploads. |
| `stream_ep_addr` | ullong | `0` | Endpoint address passed into XDMA descriptors. |
| `upload_full_frame` | bool | `true` | Uploads the full 1280x720 frame on every update. |
| `upload_enabled` | bool | `true` | Enables XDMA frame upload; set to false for DRM-only diagnostics. |
| `debug_logging` | bool | `false` | Enables extra connector, modeset, upload, and DMA logs. |

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
connector exposes one 1280x720 mode when `connector_connected=1`.

## Direct KMS Test

Use the FPGA card explicitly. Card numbers can vary.

```sh
modetest -M fpga_drm -c -p
modetest -M fpga_drm -s <connector_id>@<crtc_id>:1280x720-60 -F tiles
```

If `modetest` reports `Permission denied` during modeset/page-flip, another
process owns DRM master for that card. That behavior is normal DRM ownership
semantics, not a private driver error.

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
