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

## 2026-07-15: Translated BAR Normal Display Pass, Direct DDR Fail

Purpose: validate the July 15 hardware export after reducing the PCIe bypass
BAR while retaining the VDMA masters' full 1 GiB DDR range.

Export and live PCI state:

```text
PCI endpoint:            0000:01:00.0 10ee:7024, driver fpga_drm
BAR0:                    64 KiB
BAR2 bypass:             32 MiB, 64-bit prefetchable
PCIe-to-AXI translation: 0x3f000000
VDMA DDR:                0x40000000-0x7fffffff (1 GiB)
normal frame ring:       0x41000000-0x42fa6fff (four frames)
```

The installed and repository modules matched:

```text
srcversion: 52CE5F8E40912C028CAAFBE
```

Normal-path evidence after reboot:

```text
VDMA S2MM ... frames=4 first=0x41000000
VDMA MM2S ... frames=4 first=0x41000000
mode 1920x1080@60 readback: VDMA S2MM_SR=0x00010000 MM2S_SR=0x00010000 VTC_ERR=0x00000000 CLK_WIZ_STATUS=0x00000001
async frame upload complete mode=1920x1080@60 count=1344
```

The user confirmed visible display output. The connector was `connected`, the
display manager was active, and no new normal-path DMA timeout was present.

Direct DDR diagnostic command:

```bash
sudo -n systemctl stop display-manager
sudo -n modprobe -r fpga_drm
sudo -n modprobe fpga_drm debug_logging=1 upload_enabled=0 configure_pipeline=1 ddr_bypass_test=1 connector_connected=1 connector_non_desktop=0 enable_fbdev=1 enable_overlay=0
```

Result:

```text
hardware contract: BAR2 resource=0x2000000 mapped=0x100000 translates host+0x0 to AXI=0x3f000000
DDR bypass scratch mismatch word=0 expected=0x55aa00ff got=0x00000044
probe of 0000:01:00.0 failed with error -5
```

Root cause: `0x3f000000` is not aligned to the 32 MiB aperture;
`0x3f000000 & 0x01ffffff = 0x01000000`. The intended BAR upper-half address
bit therefore overlaps the translation base instead of selecting AXI DDR at
`0x40000000`. Low-offset control MMIO still works, explaining why the normal
display path passes.

### Bypass AXI ILA confirmation

The July 15 follow-up export restored the XDMA System ILA AXI monitor on
`SLOT_1_AXI`, connected to `xdma_0_M_AXI_BYPASS`. Vivado matched the exported
`PCIe_wrapper.ltx` to the programmed device and enumerated two cores:

```text
PCIe_i/hdmi_out/video_stream_ila/inst/ila_lib
PCIe_i/xdma_ila/inst/ila_lib
```

The bypass capture was armed on an accepted write whose address matched
`0x00000000XXfff000`. Loading the installed diagnostic driver then repeated
the 16-byte scratch test at host BAR2 offset `0x01fff000`. The decoded capture
showed:

```text
ARADDR 0x000000003ffff004/008/00c -> RRESP DECERR
AWADDR 0x000000003ffff000/004/008/00c
WDATA  0x55aa00ff, 0xa55ac33c, 0x01234567, 0x89abcdef
BRESP  DECERR for every captured write
ARADDR 0x000000003ffff000          -> RRESP DECERR
```

No transaction used the intended AXI address `0x40fff000`. The observed
address is exactly:

```text
0x3f000000 OR 0x01fff000 = 0x3ffff000
```

It is not the additive result:

```text
0x3f000000 + 0x01fff000 = 0x40fff000
```

This turns the alignment diagnosis into direct hardware evidence: XDMA emits
the aliased `0x3ffffxxx` addresses, and the AXI interconnect returns `DECERR`
because that range is not assigned to MIG. The failure occurs before any MIG
DDR transaction, so it is not evidence of failed DDR calibration or VDMA DDR
access.

Reproduction and decoding files:

```text
scripts/capture_bypass_axi_ila.tcl
scripts/analyze_bypass_axi_ila.py
tmp_bypass_ila_capture/bypass_axi_aw.csv
tmp_bypass_ila_capture/bypass_axi_aw.wdb
```

The 32 MiB bypass window must be laid out inside one 32 MiB-aligned AXI
region. Merely changing the translation base while leaving control registers
at `0x3f000000` and bypass DDR at `0x40000000` cannot work because those ranges
straddle the alignment boundary. The next hardware export must remap the
bypass master's control and MIG segments together; the VDMA masters can keep
their independent 1 GiB DDR mapping.

The normal module and desktop were restored:

```text
display-manager=active
ddr_bypass_test=N
upload_enabled=Y
enable_fbdev=Y
enable_overlay=Y
connector=connected
VDMA S2MM/MM2S frames=4 first=0x41000000
```

Conclusion: the full four-frame normal display path passes. The current export
is not GOP-ready because direct PCIe-to-DDR access fails. The next export must
use an aperture-aligned translation (or a separate framebuffer BAR), then pass
`scripts/check_fpga_hardware_contract.py --require-ddr-bypass` and the live
scratch/pattern diagnostic before GOP software work begins.

## 2026-07-15: Remapped 8 MiB Bypass DDR Contract

The next export keeps the 32 MiB BAR and translation `0x3f000000`, but moves
the bypass master's MIG segment into the non-aliased lower half:

```text
M_AXI_BYPASS controls: 0x3f000000-0x3f07ffff
M_AXI_BYPASS MIG:      0x3f800000-0x3fffffff (8 MiB)
M_AXI_MM2S/S2MM MIG:   0x40000000-0x7fffffff (1 GiB)
DDR host offsets:      0x00800000-0x00ffffff
scratch:               AXI 0x3ffff000, host+0x00fff000
GOP frame CPU write:   bypass AXI 0x3f800000-0x3ffe8fff
GOP frame VDMA scan:   VDMA AXI   0x40000000-0x407e8fff
normal frame ring:     VDMA AXI   0x41000000-0x42fa6fff, four frames
```

One maximum `1920x1080` XRGB8888 frame consumes `0x7e9000` bytes and fits in
the 8 MiB slice, leaving `0x17000` bytes. The last 4 KiB is reserved for the
non-destructive scratch test.

The checker now validates XDMA representability instead of requiring the
translation to align to the full BAR. Both static checks pass:

```sh
python3 scripts/check_fpga_hardware_contract.py
python3 scripts/check_fpga_hardware_contract.py --require-ddr-bypass
```

The rebuilt module compiled successfully with repo `srcversion`
`E191F18FCE249601FF6F162`. After installation and reboot, both the installed
and repo modules reported that same `srcversion`; `01:00.0` was bound to
`fpga_drm`, the connector was connected, and the user confirmed visible display
output in normal mode.

The display manager was stopped, the bypass ILA was armed for an accepted
write to `0x3ffff000`, and the driver was loaded once with
`ddr_bypass_test=1 upload_enabled=0`. The kernel reported:

```text
DDR bypass scratch test passed at AXI=0x3ffff000 BAR2+0x00fff000 (4 words restored)
VDMA S2MM configured ... frames=1 first=0x40000000
VDMA MM2S configured ... frames=1 first=0x40000000
DDR bypass test pattern wrote GOP frame bypass_AXI=0x3f800000 VDMA_AXI=0x40000000
    for 1280x720@60 (3686400 bytes)
```

The captured AXI transactions independently confirmed the translation and MIG
decode. The ILA observed accepted accesses at `0x3ffff000`, `0x3ffff004`,
`0x3ffff008`, and `0x3ffff00c`; all captured `BRESP` and `RRESP` values were
`OKAY`. The analyzer result was:

```text
base OR offset        0x3ffff000
base + offset         0x3ffff000
observed addresses    0x3ffff000, 0x3ffff004, 0x3ffff008, 0x3ffff00c
write responses       OKAY,OKAY,OKAY,OKAY
read responses        OKAY,OKAY,OKAY,OKAY,OKAY
result=PASS: intended AXI address completed with OKAY responses
```

Capture artifacts are under `tmp_bypass_ila_capture_8m/`. Normal operation was
then restored with `ddr_bypass_test=N`, `upload_enabled=Y`, `enable_overlay=Y`,
and four VDMA frames beginning at `0x41000000`. The display manager returned to
`active`, the connector remained `connected`, and asynchronous frame uploads
completed without errors.

Conclusion: the remapped 8 MiB bypass slice is live-validated for CPU writes,
the separate `0x40000000` VDMA view scans the written frame, and normal
four-frame display operation remains intact. This hardware/driver baseline is
ready for the GOP-specific software phase.
