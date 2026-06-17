# Project Next Steps

This is the living next-step tracker for the Simple-Display-Controller
`fpga_drm` work. Update it after each completed test, driver change, hardware
change, or workflow decision.

## Current Status

Last reviewed: 2026-06-17

Canonical update location: use this file for project-level next steps and
`Linux_DRM_Driver/tests/TEST_LOG.md` for exact test commands/results.

The project has moved from scanout-only validation to a proven CPU-backed KMS
overlay prototype:

- `fpga_drm` exposes explicit KMS objects: one CRTC, one primary plane, one
  virtual encoder, one virtual connector, and an optional overlay plane.
- `enable_overlay=1 composition_backend=cpu` exposes one `XRGB8888` overlay
  plane.
- A direct KMS atomic test has committed primary plus overlay successfully.
- Kernel logs confirmed `overlay=1`, proving the CPU overlay path was used for
  at least one XDMA upload.
- No render node exists and none is needed for the current KMS-plane milestone.
- Desktop-manager pickup has been validated for primary-plane scanout, but
  desktop compositors have not yet been shown to assign surfaces to the overlay
  plane automatically.

Evidence to keep aligned:

- Test log: `Linux_DRM_Driver/tests/TEST_LOG.md`
- Overlay test design: `Linux_DRM_Driver/tests/doc/kms_overlay_test_design.md`
- Rendering roadmap background:
  `doc/Linux_DRM_Driver_AI_GEN_documents/rendering_acceleration_research.md`

## Update Rule

After each step:

1. Add or update a dated entry in the relevant test log.
2. Update the checklist status in this file.
3. If the step changes the validated driver behavior, update `README.md` and
   the relevant document under `doc/Linux_DRM_Driver_AI_GEN_documents/`.
4. Include the exact commands, important output, and interpretation.
5. Keep object IDs such as connector, CRTC, and plane IDs as run-specific
   examples only; do not turn them into assumptions.

## Milestone Checklist

| Status | Milestone | Evidence / Notes |
|---|---|---|
| Done | Explicit KMS object refactor | Driver exposes explicit CRTC, primary plane, optional overlay plane, encoder, and connector. |
| Done | CPU overlay plane prototype | `enable_overlay=1 composition_backend=cpu` exposes the overlay plane. |
| Done | Direct KMS overlay atomic test tool | `Linux_DRM_Driver/tests/kms_overlay_test.c`. |
| Done | Direct KMS overlay test-only commit | `atomic TEST_ONLY commit succeeded` in `TEST_LOG.md`. |
| Done | Direct KMS real overlay commit | `atomic overlay commit succeeded` in `TEST_LOG.md`. |
| Done | Confirm CPU overlay upload path | Kernel log showed `upload composed frame ... overlay=1`. |
| Pending | Confirm unload-time `cpu_compositions` counter | Requires unloading `fpga_drm` after an overlay run and recording the stats line. |
| Pending | Add focused atomic reject diagnostics | Needed before compositor experiments become confusing. |
| Pending | Add compositor-friendly plane properties | Start with immutable `rotation=0`; consider alpha/blend only when semantics are implemented. |
| Pending | Run Weston DRM-backend plane assignment test | Prefer Weston before GNOME/KDE for easier KMS plane reasoning. |
| Pending | Run GNOME/KDE compositor behavior tests | Check whether mainstream compositors ever assign content to the overlay plane. |
| Pending | Pick first FPGA-backed display operation | Candidate: hardware cursor or fixed-format overlay blend. |
| Pending | Build first FPGA composition backend | Keep the same KMS plane contract; replace backend implementation only. |
| Deferred | Render node / Mesa research track | Do this only after KMS-plane acceleration is useful and stable. |

## Immediate Next Steps

### 1. Capture unload-time composition stats

Why: the direct overlay run already proved `overlay=1`, but the unload stats
line should also record a nonzero `cpu_compositions` counter.

Suggested flow:

```bash
sudo systemctl stop display-manager
sudo -n modprobe -r fpga_drm
sudo -n modprobe fpga_drm debug_logging=1 enable_overlay=1 composition_backend=cpu connector_connected=1 connector_non_desktop=0 enable_fbdev=1
sudo setfacl -m u:alpk:rw /dev/dri/card0
Linux_DRM_Driver/tests/kms_overlay_test --device /dev/dri/card0 --overlay 100,80,320,180 --hold 3
sudo -n modprobe -r fpga_drm
sudo -n dmesg | grep -Ei 'overlay=1|cpu_compositions|stats:'
```

Acceptance:

```text
overlay=1
cpu_compositions > 0
```

Record the exact output in `Linux_DRM_Driver/tests/TEST_LOG.md`.

### 2. Add focused overlay atomic-check logging

Why: Weston/GNOME/KDE tests will be hard to interpret if the driver only
returns a generic atomic failure.

Add logs around these reject points:

- missing framebuffer
- unsupported format
- non-linear modifier
- scaling request
- out-of-bounds rectangle
- disabled or mismatched CRTC
- unsupported composition backend

Target functions:

- `fpga_drm_primary_atomic_check`
- `fpga_drm_overlay_atomic_check`
- `fpga_drm_crtc_atomic_flush`
- CPU composition path

Acceptance:

- known-bad `kms_overlay_test` variants produce clear reject reasons
- valid primary-plus-overlay commit still succeeds
- `git diff --check` and module build pass

### 3. Add immutable rotation property

Why: compositors often inspect standard plane properties before deciding whether
a plane is useful. The current overlay does not support rotation, so the honest
standard property is immutable `rotation=0`.

Acceptance:

- `drm_info /dev/dri/card0` shows rotation support fixed to normal orientation
- direct overlay test still passes

### 4. Decide alpha and pixel blend mode semantics

Why: alpha and blend properties can make the overlay more compositor-friendly,
but only if the CPU backend implements the same semantics.

Decision needed:

- keep overlay opaque only for now
- or implement global alpha / premultiplied coverage in the CPU path

Acceptance if implemented:

- direct test can exercise opaque and alpha cases
- driver rejects unsupported blend states clearly

### 5. Weston DRM-backend experiment

Why: Weston is easier to reason about than GNOME/KDE for KMS plane assignment.

Goal:

- run Weston directly on `fpga_drm`
- inspect DRM logs and plane state
- determine whether Weston assigns any surface to the overlay plane

Acceptance:

- document exact Weston command
- record whether overlay plane `FB_ID` becomes nonzero
- record whether kernel logs show `overlay=1`

### 6. First FPGA-backed display operation

Why: once the KMS contract is stable, the next improvement is replacing one CPU
operation with hardware while keeping the same userspace API.

Recommended candidates:

- hardware cursor
- fixed-format overlay blend

Selection criteria:

- smallest hardware block
- easiest test vector
- minimal new memory-sharing requirements
- preserves the current KMS plane contract

## Longer-Term Tracks

### KMS Composition Track

Continue using standard KMS objects and atomic properties. This is the path most
likely to be discovered by desktop compositors automatically.

Possible features:

- cursor plane
- overlay blend
- global alpha
- z-position constraints
- limited scaling
- additional formats

### FPGA Backend Track

When a CPU feature is proven:

1. keep the KMS API unchanged
2. add an FPGA backend implementation
3. run the same test vectors against CPU and FPGA backends
4. compare output and logs

### Render Node Track

Keep this deferred. A render node alone will not make the desktop render through
the FPGA. It needs render-safe ioctls, validation, synchronization, userspace
tests, and likely Mesa or compositor integration.

## How to Close a Step

When a step is complete, update this document with:

- `Done` status in the milestone checklist
- dated summary under the relevant section
- command snippets
- result snippets
- remaining risk or follow-up

Then update `Linux_DRM_Driver/tests/TEST_LOG.md` if the step involved a test.
