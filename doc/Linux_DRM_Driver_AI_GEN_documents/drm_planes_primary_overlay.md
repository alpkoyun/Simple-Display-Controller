# DRM Planes in `fpga_drm`

This note explains what a DRM/KMS plane is, how this driver advertises a
primary plane and an optional overlay plane, and what userspace can actually
call. The short version: an application does not call private functions in
`fpga_drm`. It opens `/dev/dri/cardN` and uses standard DRM/KMS ioctls, either
directly or through libdrm helpers.

## Mental Model

In the Linux DRM/KMS display model, framebuffers feed planes, and one or more
planes feed a CRTC. The CRTC produces the timed scanout stream for a mode, and
the connector is the visible output endpoint. For this driver the chain is:

```text
userspace framebuffer(s)
        |
        v
DRM plane objects: primary, optional overlay
        |
        v
one DRM CRTC: crtc-0
        |
        v
virtual encoder and virtual connector
        |
        v
driver copies/composes XRGB8888 into line buffers
        |
        v
XDMA H2C line upload into the FPGA video path
```

A plane is therefore not an application window and not a PCIe DMA channel. It
is a KMS object that describes one image source: which framebuffer is attached,
which source rectangle inside that framebuffer is used, where it lands on the
CRTC, what pixel formats it accepts, and which CRTCs it can be attached to.

The primary plane is the base image for a CRTC. In this driver it must cover
the whole active mode. An overlay plane is an additional image layer. In many
display controllers overlays are blended by hardware during scanout. In this
driver the advertised overlay is currently CPU-composited: the kernel copies
the primary framebuffer into staging line buffers, overwrites the overlay
rectangle, and uploads the composed full frame through XDMA.

## What This Driver Advertises

The relevant implementation is in
`Linux_DRM_Driver/fpga_drm/fpga_drm_drv.c`.

The device private structure contains one CRTC, one primary plane, one overlay
plane slot, one encoder, and one connector:

```c
struct drm_crtc crtc;
struct drm_plane primary_plane;
struct drm_plane overlay_plane;
struct drm_encoder encoder;
struct drm_connector connector;
```

The driver advertises modesetting, GEM buffers, and atomic KMS through:

```c
.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC
```

It also uses GEM SHMEM helpers, so ordinary dumb buffers and mmap-backed
software rendering work through standard DRM ioctls.

The mode config sets the global framebuffer limits and mode-config callbacks:

```c
drm->mode_config.min_width = 640;
drm->mode_config.max_width = FPGA_DRM_MAX_WIDTH;   /* 1920 */
drm->mode_config.min_height = 480;
drm->mode_config.max_height = FPGA_DRM_MAX_HEIGHT; /* 1080 */
drm->mode_config.preferred_depth = 24;
drm->mode_config.funcs = &fpga_drm_mode_config_funcs;
drm->mode_config.normalize_zpos = enable_overlay;
```

Only one DRM pixel format and one modifier are advertised for both planes:

```c
static const u32 fpga_drm_formats[] = {
        DRM_FORMAT_XRGB8888,
};

static const u64 fpga_drm_modifiers[] = {
        DRM_FORMAT_MOD_LINEAR,
        DRM_FORMAT_MOD_INVALID,
};
```

### Primary Plane

The primary plane is always created:

```c
drm_universal_plane_init(drm, &fpga->primary_plane, 0,
                         &fpga_drm_plane_funcs,
                         fpga_drm_formats,
                         ARRAY_SIZE(fpga_drm_formats),
                         fpga_drm_modifiers,
                         DRM_PLANE_TYPE_PRIMARY,
                         "primary");
```

After the CRTC is initialized, the driver computes the CRTC mask and assigns it
to the primary plane:

```c
crtc_mask = drm_crtc_mask(&fpga->crtc);
fpga->primary_plane.possible_crtcs = crtc_mask;
```

The primary plane also gets immutable `zpos = 0`, meaning it is below the
overlay in the normalized z-order.

The primary atomic check enforces the current hardware/software contract:

- framebuffer must be `DRM_FORMAT_XRGB8888`
- pitch must be at least `width * 4`
- no scaling
- destination starts at `(0, 0)`
- destination size equals the active mode
- source size equals the active mode
- mode must be one of the driver's whitelist modes

So userspace should treat the primary plane as a full-screen scanout plane.

### Overlay Plane

The overlay plane is created only when the module is loaded with
`enable_overlay=1`:

```c
if (enable_overlay) {
        drm_universal_plane_init(drm, &fpga->overlay_plane,
                                 crtc_mask, &fpga_drm_plane_funcs,
                                 fpga_drm_formats,
                                 ARRAY_SIZE(fpga_drm_formats),
                                 fpga_drm_modifiers,
                                 DRM_PLANE_TYPE_OVERLAY,
                                 "cpu-overlay");
}
```

The overlay gets immutable `zpos = 1`, so it is above the primary plane. It
uses the same format contract: linear `DRM_FORMAT_XRGB8888`.

The overlay atomic check accepts only a simple rectangle:

- `enable_overlay` must be true
- framebuffer must be `DRM_FORMAT_XRGB8888`
- pitch must be at least `width * 4`
- no scaling
- destination rectangle must be fully inside the active CRTC mode
- source width/height must equal destination width/height
- no alpha, rotation, color keying, or blend-mode property is implemented

This means a desktop compositor can discover a normal KMS overlay plane, but
the plane is intentionally limited. A compositor may still choose not to use it.
The repository's direct test bypasses compositor policy and commits the overlay
itself.

Live validation on 2026-06-18 proved this contract against the installed
module. A valid atomic `TEST_ONLY` primary-plus-overlay commit succeeded, a real
overlay commit incremented unload-time `cpu_compositions` to `1`, and two
negative test-only commits incremented `atomic_rejects` to `2`. The focused
reject logs showed `reason=helper-check ret=-34` for the scaling request and
`reason=out-of-bounds ret=-22` for the rectangle outside the active mode.

## What Happens on an Atomic Commit

The driver uses the DRM atomic helpers for the public API. Its plane callbacks
validate state; actual upload scheduling happens from the CRTC flush path.

Important driver callbacks:

| Callback | Role |
|---|---|
| `fpga_drm_primary_atomic_check()` | Validates full-screen primary plane state. |
| `fpga_drm_overlay_atomic_check()` | Validates the optional overlay rectangle. |
| `fpga_drm_crtc_atomic_enable()` | Programs the selected video mode and marks the pipe enabled. |
| `fpga_drm_crtc_atomic_flush()` | Captures current plane state and queues frame upload. |
| `fpga_drm_upload_work()` | Copies primary, applies overlay if present, and submits XDMA. |

The flush path snapshots both planes:

```c
primary_state = drm_atomic_get_new_plane_state(state,
                                               &fpga->primary_plane);
if (enable_overlay)
        overlay_state = drm_atomic_get_new_plane_state(state,
                                                       &fpga->overlay_plane);

fpga_drm_replace_snapshot(&fpga->primary, primary_state);
fpga_drm_replace_snapshot(&fpga->overlay, overlay_state);
fpga_drm_queue_commit_upload(fpga);
```

The upload worker then:

1. Takes a safe reference to the latest primary and overlay snapshots.
2. Copies the primary framebuffer into per-line staging buffers.
3. If an overlay is enabled, overwrites the destination rectangle in those line
   buffers with overlay pixels.
4. Submits the staged frame with `xdma_xfer_submit_lines_nowait()`.

The current overlay blend is really a copy operation, not alpha blending:

```c
((u32 *)dst_line)[x] = ((u32 *)src_line)[x];
```

So `XRGB8888`'s unused X byte is not treated as alpha.

## What Userspace Can Call

Applications call standard DRM/KMS ioctls on `/dev/dri/cardN`. They can do this
raw, as `Linux_DRM_Driver/tests/kms_overlay_test.c` does, or through libdrm
helpers such as `drmModeGetResources()`, `drmModeGetPlaneResources()`,
`drmModeAddFB2()`, and `drmModeAtomicCommit()`.

There are no private `fpga_drm` ioctls and no custom per-plane driver API.

If using libdrm, the app-callable names you will usually see are:

| Task | Common libdrm call |
|---|---|
| Enable client capability | `drmSetClientCap()` |
| Enumerate CRTC/connector/encoder IDs | `drmModeGetResources()` |
| Read connector modes | `drmModeGetConnector()` |
| Read encoder routing | `drmModeGetEncoder()` |
| Enumerate planes | `drmModeGetPlaneResources()` |
| Read one plane | `drmModeGetPlane()` |
| Read object properties | `drmModeObjectGetProperties()` and `drmModeGetProperty()` |
| Create framebuffer object | `drmModeAddFB2()` |
| Remove framebuffer object | `drmModeRmFB()` |
| Build atomic request | `drmModeAtomicAlloc()` and `drmModeAtomicAddProperty()` |
| Commit atomic request | `drmModeAtomicCommit()` |
| Create/destroy mode blob | `drmModeCreatePropertyBlob()` and `drmModeDestroyPropertyBlob()` |

For dumb-buffer allocation and mapping, many small KMS programs still call
`DRM_IOCTL_MODE_CREATE_DUMB`, `DRM_IOCTL_MODE_MAP_DUMB`, and
`DRM_IOCTL_MODE_DESTROY_DUMB` through `drmIoctl()` or plain `ioctl()`.

Typical discovery calls:

| Operation | Raw ioctl | Purpose |
|---|---|---|
| Enable modern KMS visibility | `DRM_IOCTL_SET_CLIENT_CAP` | Enable `DRM_CLIENT_CAP_UNIVERSAL_PLANES` and `DRM_CLIENT_CAP_ATOMIC`. |
| Get global KMS objects | `DRM_IOCTL_MODE_GETRESOURCES` | Enumerate CRTC, connector, and encoder IDs. |
| Read connector state/modes | `DRM_IOCTL_MODE_GETCONNECTOR` | Find connected connector and advertised modes. |
| Read encoder routing | `DRM_IOCTL_MODE_GETENCODER` | Find which CRTC index can drive the connector. |
| Enumerate planes | `DRM_IOCTL_MODE_GETPLANERESOURCES` | Get plane object IDs. |
| Read plane formats/routing | `DRM_IOCTL_MODE_GETPLANE` | Check `possible_crtcs` and supported formats. |
| Read object properties | `DRM_IOCTL_MODE_OBJ_GETPROPERTIES` plus `DRM_IOCTL_MODE_GETPROPERTY` | Find property IDs such as `type`, `FB_ID`, `CRTC_ID`, `SRC_W`, and `CRTC_W`. |

Typical framebuffer calls:

| Operation | Raw ioctl | Purpose |
|---|---|---|
| Allocate scanout memory | `DRM_IOCTL_MODE_CREATE_DUMB` | Create linear software-rendered GEM buffer. |
| Turn buffer into framebuffer | `DRM_IOCTL_MODE_ADDFB2` | Create a framebuffer ID with `DRM_FORMAT_XRGB8888`. |
| Map dumb buffer | `DRM_IOCTL_MODE_MAP_DUMB` plus `mmap()` | CPU-fill the pixels. |
| Remove framebuffer | `DRM_IOCTL_MODE_RMFB` | Drop the framebuffer object. |
| Destroy dumb buffer | `DRM_IOCTL_MODE_DESTROY_DUMB` | Free the backing GEM buffer. |

Typical atomic mode/plane calls:

| Operation | Raw ioctl | Purpose |
|---|---|---|
| Create mode blob | `DRM_IOCTL_MODE_CREATEPROPBLOB` | Turn `struct drm_mode_modeinfo` into a `MODE_ID` property blob. |
| Commit display state | `DRM_IOCTL_MODE_ATOMIC` | Set connector, CRTC, primary plane, and overlay plane properties atomically. |
| Validate without applying | `DRM_IOCTL_MODE_ATOMIC` with `DRM_MODE_ATOMIC_TEST_ONLY` | Ask the driver whether the state would be accepted. |
| Allow modeset changes | `DRM_MODE_ATOMIC_ALLOW_MODESET` flag | Required when enabling/disabling a CRTC or changing mode. |
| Destroy blob | `DRM_IOCTL_MODE_DESTROYPROPBLOB` | Release the user-created mode blob. |

The atomic plane properties userspace normally sets are:

```text
FB_ID    framebuffer object ID
CRTC_ID  target CRTC object ID
SRC_X    source x in 16.16 fixed point
SRC_Y    source y in 16.16 fixed point
SRC_W    source width in 16.16 fixed point
SRC_H    source height in 16.16 fixed point
CRTC_X   destination x on the CRTC
CRTC_Y   destination y on the CRTC
CRTC_W   destination width on the CRTC
CRTC_H   destination height on the CRTC
```

For a full commit to this driver, userspace also sets:

```text
connector.CRTC_ID = crtc_id
crtc.MODE_ID      = mode_blob_id
crtc.ACTIVE       = 1
```

The included `kms_overlay_test` follows exactly this pattern:

1. Open `/dev/dri/card0`.
2. Enable universal planes and atomic KMS client caps.
3. Enumerate resources, connector, CRTC, and planes.
4. Find one `DRM_PLANE_TYPE_PRIMARY` plane and one
   `DRM_PLANE_TYPE_OVERLAY` plane that support `DRM_FORMAT_XRGB8888`.
5. Allocate and fill dumb buffers for primary and overlay content.
6. Create a mode blob.
7. Submit an atomic request that binds both planes to the same CRTC.

## Minimal Atomic State Shape

For this driver, a successful visible overlay commit looks like this at the
property level:

```text
connector:
  CRTC_ID = crtc

crtc:
  MODE_ID = selected mode blob
  ACTIVE  = 1

primary plane:
  FB_ID   = full-screen framebuffer
  CRTC_ID = crtc
  SRC_X   = 0
  SRC_Y   = 0
  SRC_W   = mode_width << 16
  SRC_H   = mode_height << 16
  CRTC_X  = 0
  CRTC_Y  = 0
  CRTC_W  = mode_width
  CRTC_H  = mode_height

overlay plane:
  FB_ID   = overlay framebuffer
  CRTC_ID = crtc
  SRC_X   = 0
  SRC_Y   = 0
  SRC_W   = overlay_width << 16
  SRC_H   = overlay_height << 16
  CRTC_X  = overlay_x
  CRTC_Y  = overlay_y
  CRTC_W  = overlay_width
  CRTC_H  = overlay_height
```

If userspace wants to disable the overlay, it commits `FB_ID = 0` and
`CRTC_ID = 0` for the overlay plane.

## Practical Notes

Load with overlay enabled if you want two planes:

```sh
sudo modprobe fpga_drm enable_overlay=1 composition_backend=cpu
```

Without `enable_overlay=1`, userspace should see only the primary plane.

Object IDs are runtime IDs. They can change after driver reloads. Always
enumerate them instead of hardcoding values.

`DRM_CLIENT_CAP_UNIVERSAL_PLANES` matters. Without it, legacy userspace may not
see primary and cursor-like plane objects in the same way as overlay planes.

The overlay is advertised as a normal KMS overlay, but the current
implementation is not a zero-copy hardware overlay. It is a software
composition step inside the kernel followed by the existing XDMA frame upload.

The app-callable ABI is intentionally generic DRM/KMS. That is good: tools such
as `modetest`, `drm_info`, a Wayland compositor, or a custom KMS app all speak
the same object/property language.

## Sources

- Linux kernel KMS documentation: <https://docs.kernel.org/gpu/drm-kms.html>
- Linux kernel DRM userspace API documentation:
  <https://docs.kernel.org/gpu/drm-uapi.html>
- Local driver: `Linux_DRM_Driver/fpga_drm/fpga_drm_drv.c`
- Local raw-ioctl test: `Linux_DRM_Driver/tests/kms_overlay_test.c`
- Local test design note:
  `Linux_DRM_Driver/tests/doc/kms_overlay_test_design.md`
