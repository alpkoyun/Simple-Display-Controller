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

## 2026-06-18: Atomic Reject Diagnostics Source Build

Purpose: implement the next project step after direct overlay success: focused
atomic reject diagnostics plus negative `kms_overlay_test` modes for validating
scaling and bounds rejects.

Source changes:

- `fpga_drm` now logs focused atomic reject reasons when `debug_logging=1`.
- Reject logs include stage, plane id/type, reason, return code, framebuffer
  format/modifier/size/pitch, CRTC id, mode name, source size, and destination
  rectangle.
- `fpga_drm_crtc_atomic_flush()` now logs changed plane state and enabled
  primary/overlay snapshots when `debug_logging=1`.
- CPU overlay composition failures now log source and destination rectangles.
- `kms_overlay_test` now supports:
  - `--scale-overlay`
  - `--overlay-out-of-bounds`

Build commands:

```bash
make -C Linux_DRM_Driver/tests
make -C Linux_DRM_Driver/fpga_drm
git diff --check
```

Results:

```text
kms_overlay_test build: pass
fpga_drm module build: pass
git diff --check: pass
```

Help command:

```bash
./Linux_DRM_Driver/tests/kms_overlay_test --help
```

Result excerpt:

```text
--scale-overlay           Request overlay scaling; expected to fail
--overlay-out-of-bounds   Place overlay partly outside CRTC; expected to fail
```

Module version check:

```bash
modinfo Linux_DRM_Driver/fpga_drm/fpga_drm.ko | sed -n '1,25p'
modinfo fpga_drm | sed -n '1,25p'
```

Result:

```text
repo-built srcversion: 9205FC3654816841CF7942C
installed srcversion:  E1CC69967649F5D3185976E
```

Live validation status: not run in this step. `systemctl stop display-manager`
and `sudo -n systemctl stop display-manager` were blocked because sudo required
a password, and the installed module was still the older srcversion. Before
live-testing the new diagnostics, install/reload the repo-built module.

Required live validation commands after install/reload:

```bash
Linux_DRM_Driver/tests/kms_overlay_test --device /dev/dri/card0 --commit-test-only --scale-overlay
Linux_DRM_Driver/tests/kms_overlay_test --device /dev/dri/card0 --commit-test-only --overlay-out-of-bounds
sudo -n dmesg | grep -Ei 'atomic reject|helper-check|scaling|out-of-bounds'
```

Expected result: the two negative tests fail, and `dmesg` reports focused reject
reasons from `fpga_drm`.

## 2026-06-18: Live Diagnostics Retry Blocked by Active Display Manager

Purpose: retry live validation of the atomic reject diagnostics after the
repo-built module had been installed.

Version check commands:

```bash
modinfo Linux_DRM_Driver/fpga_drm/fpga_drm.ko | sed -n '1,10p'
modinfo fpga_drm | sed -n '1,10p'
```

Result:

```text
repo-built srcversion: 9205FC3654816841CF7942C
installed srcversion:  9205FC3654816841CF7942C
```

State checks:

```bash
systemctl is-active display-manager
lsmod | grep -Ei '^fpga_drm|^xdma|drm' || true
readlink /sys/bus/pci/devices/0000:01:00.0/driver 2>/dev/null || true
cat /sys/class/drm/card0-Virtual-1/status
cat /sys/class/drm/card0-Virtual-1/modes
for p in /sys/module/fpga_drm/parameters/*; do printf '%s=' "$(basename "$p")"; cat "$p"; done
```

Result:

```text
display-manager: active
loaded module: fpga_drm
PCI driver: ../../../../bus/pci/drivers/fpga_drm
connector status: connected
advertised modes: 1280x720, 1920x1080, 1280x1024, 1024x768, 800x600, 640x480
debug_logging=N
enable_overlay=N
enable_fbdev=N
composition_backend=cpu
```

Kernel log check:

```bash
sudo -n dmesg | grep -Ei 'fpga_drm|xdma|drm|overlay|cpu_compositions|atomic|reject' | tail -120
```

Result excerpt:

```text
registered 12-mode XRGB8888 stream display ... fbdev=0 upload=1 debug=0 overlay=0 composition=cpu
mode 1280x720@60 readback: VDMA S2MM_SR=0x00010000 MM2S_SR=0x00010000 VTC_ISR=0x00012200 VTC_ERR=0x00000000 CLK_WIZ_STATUS=0x00000001
```

Reload attempt:

```bash
sudo -n systemctl stop display-manager
sudo -n modprobe -r fpga_drm
```

Result:

```text
sudo -n systemctl stop display-manager: sudo: a password is required
sudo -n modprobe -r fpga_drm: modprobe: FATAL: Module fpga_drm is in use.
```

DRM access checks:

```bash
ls -la /dev/dri /dev/dri/by-path
id
getfacl /dev/dri/card0
drm_info /dev/dri/card0
modetest -M fpga_drm -c -p
sudo -n setfacl -m u:alpk:rw /dev/dri/card0
```

Result:

```text
/dev/dri/card0 exists as root:video crw-rw----+
current user groups: alpk,nogroup
ACL includes root, video, and gdm, but not alpk
drm_info: /dev/dri/card0: Permission denied
modetest: failed to open device 'fpga_drm': No such file or directory
sudo -n setfacl: sudo: a password is required
```

Conclusion: the installed module matches the repo-built diagnostics build, but
live validation was not run because the currently loaded instance was started
with `enable_overlay=0 debug_logging=0`, `display-manager` was active, and this
session could not stop it or grant `/dev/dri/card0` access without a password.

Required unblock sequence:

```bash
sudo systemctl stop display-manager
sudo modprobe -r fpga_drm
sudo modprobe fpga_drm debug_logging=1 enable_overlay=1 composition_backend=cpu connector_connected=1 connector_non_desktop=0 enable_fbdev=1
sudo setfacl -m u:alpk:rw /dev/dri/card0
```

After that, rerun:

```bash
Linux_DRM_Driver/tests/kms_overlay_test --device /dev/dri/card0 --commit-test-only
Linux_DRM_Driver/tests/kms_overlay_test --device /dev/dri/card0 --commit-test-only --scale-overlay
Linux_DRM_Driver/tests/kms_overlay_test --device /dev/dri/card0 --commit-test-only --overlay-out-of-bounds
sudo -n dmesg | grep -Ei 'atomic reject|helper-check|scaling|out-of-bounds|overlay=1|cpu_compositions'
```

## 2026-06-18: Live Atomic Reject Diagnostics Validated

Purpose: validate the installed diagnostics build on the live FPGA DRM device,
including a valid overlay commit, expected negative atomic rejects, CPU
composition accounting, and focused reject log messages.

Initial setup:

```bash
sudo -n systemctl stop display-manager
sudo -n modprobe -r fpga_drm
sudo -n modprobe fpga_drm debug_logging=1 enable_overlay=1 composition_backend=cpu connector_connected=1 connector_non_desktop=0 enable_fbdev=1
sudo -n setfacl -m u:alpk:rw /dev/dri/card0
```

Result:

```text
display-manager: inactive
PCI driver: ../../../../bus/pci/drivers/fpga_drm
debug_logging=Y
enable_overlay=Y
composition_backend=cpu
enable_fbdev=Y
/dev/dri/card0 ACL includes user:alpk:rw-
```

KMS enumeration:

```bash
modetest -M fpga_drm -c -p
```

Result excerpt:

```text
connector=39 Virtual-1 connected, preferred mode 1280x720@60
primary plane=31 formats: XR24 LINEAR zpos=0
overlay plane=35 formats: XR24 LINEAR zpos=1
```

Build check:

```bash
make -C Linux_DRM_Driver/tests
```

Result:

```text
Nothing to be done for 'all'.
```

Valid atomic test-only command:

```bash
./Linux_DRM_Driver/tests/kms_overlay_test --device /dev/dri/card0 --commit-test-only
```

Result:

```text
overlay src=320x180 dst=80,60 320x180 scale=0 out_of_bounds=0
atomic TEST_ONLY commit succeeded
```

Expected-failure scaling command:

```bash
./Linux_DRM_Driver/tests/kms_overlay_test --device /dev/dri/card0 --commit-test-only --scale-overlay
```

Result:

```text
overlay src=320x180 dst=80,60 640x360 scale=1 out_of_bounds=0
ATOMIC commit flags=0x500 failed: Numerical result out of range
```

Expected-failure out-of-bounds command:

```bash
./Linux_DRM_Driver/tests/kms_overlay_test --device /dev/dri/card0 --commit-test-only --overlay-out-of-bounds
```

Result:

```text
overlay src=320x180 dst=1120,630 320x180 scale=0 out_of_bounds=1
ATOMIC commit flags=0x500 failed: Invalid argument
```

Real overlay commit command:

```bash
./Linux_DRM_Driver/tests/kms_overlay_test --device /dev/dri/card0 --overlay 100,80,320,180 --hold 3
```

Result:

```text
overlay src=320x180 dst=100,80 320x180 scale=0 out_of_bounds=0
atomic overlay commit succeeded; holding for 3 seconds
display state cleaned up
```

Unload stats command:

```bash
sudo -n modprobe -r fpga_drm
sudo -n dmesg | grep -Ei 'fpga_drm.*stats|atomic reject|cpu_compositions|overlay=1|helper-check|scaling|out-of-bounds' | tail -120
```

Result:

```text
stats: atomic_commits=8321 atomic_rejects=2 frames_queued=8320 frames_uploaded=8311 upload_failures=0 cpu_compositions=1
```

The first log read was flooded by fbdev/debug upload messages, so the focused
reject log check was repeated with fbdev disabled.

Focused reject diagnostic setup:

```bash
sudo -n systemctl stop display-manager
sudo -n modprobe -r fpga_drm
sudo -n modprobe fpga_drm debug_logging=1 enable_overlay=1 composition_backend=cpu connector_connected=1 connector_non_desktop=0 enable_fbdev=0
sudo -n setfacl -m u:alpk:rw /dev/dri/card0
./Linux_DRM_Driver/tests/kms_overlay_test --device /dev/dri/card0 --commit-test-only --scale-overlay
./Linux_DRM_Driver/tests/kms_overlay_test --device /dev/dri/card0 --commit-test-only --overlay-out-of-bounds
sudo -n dmesg | grep -Ei 'registered 12-mode|atomic reject|helper-check|out-of-bounds|scaling|atomic_rejects|stats:' | tail -120
```

Result:

```text
registered 12-mode XRGB8888 stream display ... fbdev=0 upload=1 debug=1 overlay=1 composition=cpu
atomic reject stage=overlay plane=35 type=0 reason=helper-check ret=-34 ... src=320x180 dst=80x60+640x360
atomic reject stage=overlay plane=35 type=0 reason=out-of-bounds ret=-22 ... src=320x180 dst=1120x630+320x180
```

Diagnostic-only unload stats:

```bash
sudo -n modprobe -r fpga_drm
sudo -n dmesg | grep -Ei 'stats: atomic_commits|atomic reject' | tail -40
```

Result:

```text
stats: atomic_commits=1 atomic_rejects=2 frames_queued=0 frames_uploaded=0 upload_failures=0 cpu_compositions=0
```

Restore command:

```bash
sudo -n modprobe fpga_drm debug_logging=1 enable_overlay=1 composition_backend=cpu connector_connected=1 connector_non_desktop=0 enable_fbdev=1
sudo -n systemctl start display-manager
```

Final state:

```text
display-manager: active
PCI driver: ../../../../bus/pci/drivers/fpga_drm
debug_logging=Y
enable_overlay=Y
composition_backend=cpu
enable_fbdev=Y
```

Conclusion: live validation passed. Valid atomic overlay commits work, scaling
and out-of-bounds requests are rejected, unload stats report
`atomic_rejects=2`, and a real overlay commit increments `cpu_compositions`.
Focused reject logs are visible when fbdev/debug upload noise is avoided.
