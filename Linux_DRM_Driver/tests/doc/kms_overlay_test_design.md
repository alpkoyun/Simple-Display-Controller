# kms_overlay_test Design

`kms_overlay_test` is a userspace validation tool for the experimental
`fpga_drm` overlay plane. It proves that standard DRM/KMS userspace can discover
the primary and overlay planes, submit an atomic commit, and cause the driver to
compose the overlay into the normal XDMA frame upload path.

The test intentionally uses only standard DRM UAPI ioctls. It does not use a
private `fpga_drm` ioctl, does not need Mesa, and does not require a DRM render
node. On this machine, the `libdrm` runtime package is installed but the
development headers are not, so the test includes kernel UAPI headers directly:

```c
#include <drm.h>
#include <drm_fourcc.h>
#include <drm_mode.h>
```

## What It Proves

The test validates the first CPU-overlay milestone:

1. The driver exposes a standard KMS overlay plane when loaded with
   `enable_overlay=1`.
2. The overlay plane advertises `DRM_FORMAT_XRGB8888` / `XR24`.
3. A normal atomic KMS client can commit a primary framebuffer and an overlay
   framebuffer to the same CRTC.
4. The kernel driver accepts the atomic state.
5. `fpga_drm` sees the overlay as active and sends a composed frame through the
   existing XDMA upload path.

It does not prove desktop-compositor use. GNOME, KDE, or Weston may still choose
not to assign any surface to the overlay plane. This test bypasses compositor
policy and checks the driver contract directly.

## Expected Driver State

Before running the test, load `fpga_drm` with overlay and debug logging enabled:

```bash
sudo -n modprobe fpga_drm \
  debug_logging=1 \
  enable_overlay=1 \
  composition_backend=cpu \
  connector_connected=1 \
  connector_non_desktop=0 \
  enable_fbdev=1
```

Expected module state:

```text
/sys/bus/pci/devices/0000:01:00.0/driver -> fpga_drm
/sys/module/fpga_drm/parameters/enable_overlay = Y
/sys/module/fpga_drm/parameters/composition_backend = cpu
/sys/module/fpga_drm/parameters/debug_logging = Y
```

The test needs permission to open the KMS card:

```bash
sudo setfacl -m u:alpk:rw /dev/dri/card0
```

## High-Level Flow

The tool performs this sequence:

1. Open the DRM card.
2. Enable universal-plane and atomic-client capabilities.
3. Enumerate DRM resources.
4. Find a connected connector and a compatible CRTC.
5. Enumerate planes and select one primary plane plus one overlay plane.
6. Create XRGB8888 dumb buffers for primary and overlay content.
7. Create a mode property blob.
8. Build an atomic commit with connector, CRTC, primary-plane, and overlay-plane
   properties.
9. Either run a test-only commit or a real commit.
10. For a real commit, hold the image on screen briefly, then clean up the KMS
    state and destroy buffers.

## DRM Capabilities

At startup, the tool enables:

```c
DRM_CLIENT_CAP_UNIVERSAL_PLANES
DRM_CLIENT_CAP_ATOMIC
```

`DRM_CLIENT_CAP_UNIVERSAL_PLANES` makes primary and overlay planes visible to
userspace. `DRM_CLIENT_CAP_ATOMIC` exposes atomic properties such as `CRTC_ID`,
`FB_ID`, `SRC_*`, `CRTC_*`, `MODE_ID`, and `ACTIVE`.

If either capability fails, the card is not usable for this test.

## Resource Discovery

The test calls `DRM_IOCTL_MODE_GETRESOURCES` twice:

1. First call: get counts and framebuffer size limits.
2. Second call: retrieve CRTC, connector, and encoder object IDs.

The framebuffer size limits matter for this driver. The validated run reported:

```text
framebuffer limits min=640x480 max=1920x1080
```

That means a small overlay framebuffer such as `320x180` cannot be created
directly with `ADDFB2`. The test allocates a `640x480` overlay backing
framebuffer and displays only the requested `320x180` source rectangle.

## Connector and CRTC Selection

For each connector, the test calls `DRM_IOCTL_MODE_GETCONNECTOR` twice:

1. First call: read mode, encoder, and property counts.
2. Second call: read the arrays.

The second call must provide arrays for modes, encoders, properties, and
property values. Passing only mode and encoder arrays caused this failure during
bring-up:

```text
GETCONNECTOR 39 arrays failed: Bad address
```

The tool selects the first connected connector with at least one mode. It uses
the preferred mode if one is marked with `DRM_MODE_TYPE_PREFERRED`; otherwise it
uses the first mode returned by the driver.

The CRTC is selected from the connector's current encoder or possible encoders.
For the validated driver instance, the tool found:

```text
connector=39 crtc=34 mode=1280x720 1280x720
```

Object IDs are not stable across reloads. Do not hardcode these values.

## Plane Selection

The test calls `DRM_IOCTL_MODE_GETPLANERESOURCES`, then reads each plane with
`DRM_IOCTL_MODE_GETPLANE`.

For each plane, it checks:

- the plane can be used with the selected CRTC index
- the plane supports `DRM_FORMAT_XRGB8888`
- the plane's atomic `type` property is `DRM_PLANE_TYPE_PRIMARY` or
  `DRM_PLANE_TYPE_OVERLAY`

Validated output:

```text
plane 31 type=1 possible_crtcs=0x1 XR24=1
plane 35 type=0 possible_crtcs=0x1 XR24=1
```

In DRM UAPI terms:

- `type=1` is primary
- `type=0` is overlay

The test fails if either plane is missing. If no overlay plane is found, reload
the driver with `enable_overlay=1`.

## Framebuffer Creation

Both framebuffers are dumb buffers:

1. `DRM_IOCTL_MODE_CREATE_DUMB`
2. `DRM_IOCTL_MODE_ADDFB2`
3. `DRM_IOCTL_MODE_MAP_DUMB`
4. `mmap`
5. CPU-fill the mapped memory

The primary framebuffer is full mode size and contains a gradient/checkerboard
pattern. The overlay framebuffer contains a bright border and striped fill so it
is visually obvious on the monitor.

The pixel format is always:

```c
DRM_FORMAT_XRGB8888
```

This matches the current `fpga_drm` primary and overlay plane contract.

## Atomic Commit Contents

The atomic commit updates four DRM objects:

- connector
- CRTC
- primary plane
- overlay plane

Connector properties:

```text
CRTC_ID = selected CRTC
```

CRTC properties:

```text
MODE_ID = mode blob
ACTIVE  = 1
```

Primary plane properties:

```text
FB_ID   = primary framebuffer
CRTC_ID = selected CRTC
SRC_X   = 0
SRC_Y   = 0
SRC_W   = mode width  << 16
SRC_H   = mode height << 16
CRTC_X  = 0
CRTC_Y  = 0
CRTC_W  = mode width
CRTC_H  = mode height
```

Overlay plane properties:

```text
FB_ID   = overlay framebuffer
CRTC_ID = selected CRTC
SRC_X   = 0
SRC_Y   = 0
SRC_W   = overlay displayed width  << 16
SRC_H   = overlay displayed height << 16
CRTC_X  = overlay x
CRTC_Y  = overlay y
CRTC_W  = overlay displayed width
CRTC_H  = overlay displayed height
```

The source and destination sizes match, so the commit requests no scaling.
This matches the current driver's overlay check rules.

## Test-Only Commit

`--commit-test-only` adds `DRM_MODE_ATOMIC_TEST_ONLY`:

```bash
Linux_DRM_Driver/tests/kms_overlay_test --device /dev/dri/card0 --commit-test-only
```

This asks the kernel to validate the full atomic state without programming the
display. A successful result means the KMS object relationships, formats,
dimensions, bounds, and properties are acceptable to the driver.

Validated result:

```text
atomic TEST_ONLY commit succeeded
```

## Real Commit

The real commit omits `DRM_MODE_ATOMIC_TEST_ONLY`:

```bash
Linux_DRM_Driver/tests/kms_overlay_test --device /dev/dri/card0 --overlay 100,80,320,180 --hold 10
```

The tool holds the image for the requested number of seconds, then commits a
cleanup state:

```text
overlay FB_ID=0, CRTC_ID=0
primary FB_ID=0, CRTC_ID=0
CRTC ACTIVE=0, MODE_ID=0
connector CRTC_ID=0
```

Validated result:

```text
atomic overlay commit succeeded; holding for 10 seconds
display state cleaned up
```

## How This Reaches fpga_drm

The test does not talk to XDMA directly. It only submits KMS state to the DRM
core. The expected kernel-side path is:

1. DRM atomic helpers call the driver's primary and overlay plane checks.
2. `fpga_drm_crtc_atomic_enable()` programs the selected mode if needed.
3. `fpga_drm_crtc_atomic_flush()` snapshots the primary and overlay plane
   states and queues an upload.
4. `fpga_drm_upload_work()` copies the primary framebuffer into host line
   buffers.
5. The CPU composition backend overlays the active overlay rectangle into those
   line buffers.
6. The driver submits the composed frame through the existing XDMA H2C path.

Successful kernel evidence:

```text
queue upload count=6366 full=1 enabled=1 overlay=1
upload composed frame mode=1280x720@60 overlay=1
```

`overlay=1` is the key proof that the driver saw an active overlay plane and
used the CPU composition path for that upload.

## What the Test Does Not Cover

This test does not validate:

- compositor plane assignment policy
- Weston, GNOME, or KDE behavior
- alpha blending
- pixel blend mode properties
- rotation
- scaling
- hardware FPGA overlay composition
- render-node operation
- Mesa/OpenGL/Vulkan integration

Those are later milestones. This tool only proves the standard KMS plane
contract and the current CPU composition backend.

## Common Failures

`open /dev/dri/card0 failed: Permission denied`

Fix:

```bash
sudo setfacl -m u:alpk:rw /dev/dri/card0
```

`no usable XR24 overlay plane found`

Reload the driver with:

```bash
sudo -n modprobe -r fpga_drm
sudo -n modprobe fpga_drm debug_logging=1 enable_overlay=1 composition_backend=cpu connector_connected=1 connector_non_desktop=0 enable_fbdev=1
```

`ADDFB2 320x180 failed: Invalid argument`

The driver may reject framebuffers smaller than its mode-config minimum. The
current tool handles this by allocating a larger backing framebuffer, but older
tool versions did not.

`ATOMIC commit ... failed: Permission denied`

Another process may hold DRM master. Stop the display manager before running
the direct test:

```bash
sudo systemctl stop display-manager
```

## Relationship to the Test Log

The design and mechanism live in this file. Each actual run, including commands
and results, should be recorded in:

```text
Linux_DRM_Driver/tests/TEST_LOG.md
```
