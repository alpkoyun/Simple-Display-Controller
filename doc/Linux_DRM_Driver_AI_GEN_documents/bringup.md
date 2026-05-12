# Bring-Up Procedure

## Build

```sh
make -C sources/fpga_drm
```

The output module is `sources/fpga_drm/fpga_drm.ko`.

## Normal Load

Unload the standalone Xilinx XDMA module first. It matches the same PCI
function and conflicts with `fpga_drm.ko`.

```sh
sudo insmod sources/fpga_drm/fpga_drm.ko
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
```


The FPGA should appear as a DRM card with one 1280x720 output. If the desktop
stack does not list it, verify that the FPGA PCIe function enumerates as a
display-class device and that `fpga_drm.ko` owns the PCI function.

## Optional Direct KMS Test

Use explicit driver or card selection.

```sh
modetest -M fpga_drm -c -p
modetest -M fpga_drm -s <connector_id>@<crtc_id>:1280x720-60 -F tiles
modetest -D /dev/dri/card0 -M fpga_drm -s 31@34:1280x720-60 -F tiles
```

If `modetest` fails with `Permission denied`, check DRM master ownership:

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
sudo insmod sources/fpga_drm/fpga_drm.ko upload_enabled=0 debug_logging=1
```

Manual safe visibility mode:

```sh
sudo rmmod fpga_drm
sudo insmod sources/fpga_drm/fpga_drm.ko <diagnostic connector/fbdev overrides> debug_logging=1
```

Use these only to isolate userspace enumeration, fbdev, or upload-path issues.

## Debug Snapshot

```sh
sh sources/fpga_drm/debug_collect.sh
```

Capture `dmesg` around module load, first modeset, first upload, and unload.
For hardware validation, capture the FPGA video-output ILA while forcing a KMS
test pattern or desktop update. Successful upload should show changing 24-bit
pixel data and active sync/video timing on the FPGA side.
