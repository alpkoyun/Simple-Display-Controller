# Test Log

Use this file to record each hardware or driver test briefly. Include the date,
setup state, command, result, and any follow-up change. Keep entries concise but
specific enough that the run can be repeated.

## 2026-06-12: Direct KMS Overlay Commit

Purpose: prove that the optional `fpga_drm` KMS overlay plane can be discovered,
atomically committed, and CPU-composited into the XDMA upload path without a
render node or private ioctl.

Setup:

```bash
sudo systemctl stop display-manager
sudo -n modprobe -r fpga_drm
sudo -n modprobe fpga_drm debug_logging=1 enable_overlay=1 composition_backend=cpu connector_connected=1 connector_non_desktop=0 enable_fbdev=1
sudo setfacl -m u:alpk:rw /dev/dri/card0
```

Verified state:

```text
/sys/bus/pci/devices/0000:01:00.0/driver -> fpga_drm
enable_overlay=Y
composition_backend=cpu
debug_logging=Y
display-manager=inactive
/dev/dri/card0 ACL includes user:alpk:rw-
```

Build command:

```bash
make -C Linux_DRM_Driver/tests
```

Result:

```text
cc -I/usr/include/drm -O2 -g -Wall -Wextra -Wshadow -Wformat=2 -Wno-missing-field-initializers -o kms_overlay_test kms_overlay_test.c
```

First test command:

```bash
./Linux_DRM_Driver/tests/kms_overlay_test --device /dev/dri/card0 --commit-test-only
```

Initial result:

```text
GETCONNECTOR 39 arrays failed: Bad address
```

Fix: `kms_overlay_test` now allocates connector property and property-value
arrays before the second `DRM_IOCTL_MODE_GETCONNECTOR` call.

Second test command:

```bash
./Linux_DRM_Driver/tests/kms_overlay_test --device /dev/dri/card0 --commit-test-only
```

Initial result:

```text
ADDFB2 320x180 failed: Invalid argument
plane 31 type=1 possible_crtcs=0x1 XR24=1
plane 35 type=0 possible_crtcs=0x1 XR24=1
connector=39 crtc=34 primary=31 overlay=35 mode=1280x720 1280x720
overlay rectangle=80,60 320x180
```

Fix: `kms_overlay_test` now respects the driver's advertised minimum framebuffer
size by allocating a `640x480` overlay backing framebuffer while displaying only
the requested `320x180` source rectangle.

Successful test-only command:

```bash
./Linux_DRM_Driver/tests/kms_overlay_test --device /dev/dri/card0 --commit-test-only
```

Result:

```text
plane 31 type=1 possible_crtcs=0x1 XR24=1
plane 35 type=0 possible_crtcs=0x1 XR24=1
connector=39 crtc=34 primary=31 overlay=35 mode=1280x720 1280x720
overlay rectangle=80,60 320x180
framebuffer limits min=640x480 max=1920x1080
overlay backing framebuffer=640x480, displayed source=320x180
atomic TEST_ONLY commit succeeded
```

Successful real commit command:

```bash
./Linux_DRM_Driver/tests/kms_overlay_test --device /dev/dri/card0 --overlay 100,80,320,180 --hold 10
```

Result:

```text
plane 31 type=1 possible_crtcs=0x1 XR24=1
plane 35 type=0 possible_crtcs=0x1 XR24=1
connector=39 crtc=34 primary=31 overlay=35 mode=1280x720 1280x720
overlay rectangle=100,80 320x180
framebuffer limits min=640x480 max=1920x1080
overlay backing framebuffer=640x480, displayed source=320x180
atomic overlay commit succeeded; holding for 10 seconds
display state cleaned up
```

Kernel log check:

```bash
sudo -n dmesg | grep -Ei 'overlay=1|cpu_compositions|stats:'
```

Result:

```text
[ 3887.693861] fpga_drm 0000:01:00.0: [drm] queue upload count=6366 full=1 enabled=1 overlay=1
[ 3887.694620] fpga_drm 0000:01:00.0: [drm] upload composed frame mode=1280x720@60 overlay=1
```

Conclusion: the direct KMS overlay milestone passed. The overlay plane was
discovered, accepted by atomic test-only validation, committed in a real modeset,
and used by the driver's CPU composition path. The unload-time
`cpu_compositions` stat was not checked in this run because the module remained
loaded.
