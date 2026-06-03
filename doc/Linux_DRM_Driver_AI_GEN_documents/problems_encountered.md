# Problems Encountered

This document keeps historical bring-up issues out of the current architecture
and operating docs. Each item is factual: symptom, cause, fix, and current
status.

## Blocking XDMA Frame Upload Performance

**Symptom:** Early driver revisions uploaded a frame as 720 separate blocking
line transfers. That path was too slow and could stall the display update path.

**Cause:** Each line used its own synchronous XDMA submission and completion
wait, so a full frame paid the setup and wake cost 720 times.

**Fix:** The current driver copies the framebuffer into max-width line buffers,
keeps a max-height frame SG table, prepares an active-mode SG view, and submits
the whole frame with
`xdma_xfer_submit_lines_nowait()`. Completion is handled by callback,
completion work, and timeout work.

**Current status:** Resolved in the current architecture. Main docs describe
only the async frame path.

## fbdev 24/24 Mismatch

**Symptom:** Generic fbdev setup did not match the 32-bit XRGB8888 framebuffer
storage expected by the FPGA stream path.

**Cause:** The display depth and storage bits were treated inconsistently.
The FPGA ignores bits 31:24, but the Linux-side buffer still uses 32 bits per
pixel.

**Fix:** The current driver sets preferred DRM depth to 24 but calls
`drm_fbdev_generic_setup(drm, 32)` so fbdev uses 32-bit storage.

**Current status:** Resolved. The current pixel contract is XRGB8888 storage
with RGB in the lower 24 bits.

## Display Manager Did Not Expose the FPGA Card

**Symptom:** The DRM card could exist under `/sys/class/drm`, but Xorg or the
display manager did not expose it as a normal desktop display.

**Cause:** The FPGA PCIe function was not presented as a display-class device,
so the desktop stack did not treat it like a normal display adapter.

**Fix:** The FPGA PCIe function should enumerate as display class. The current
driver defaults expose the connector as connected and desktop-capable. Generic
fbdev is available through `enable_fbdev=1`, but the current default is
disabled.

**Current status:** Resolved as a platform/bitstream integration requirement.
Stopping the display manager is now only a manual KMS diagnostic step.

## `modetest` Permission Denied

**Symptom:** `modetest` reached modeset or page flip but failed with
`Permission denied`.

**Cause:** Another process, usually the compositor or display manager, already
owned DRM master for the target card.

**Fix:** First make sure the user can open `/dev/dri/card0`; a missing ACL also
appears as `Permission denied`. Use `getfacl /dev/dri/card0` and, when needed,
`sudo setfacl -m u:alpk:rw /dev/dri/card0`. For direct `modetest` runs, target
the FPGA driver with `modetest -M fpga_drm`. Run from a text console or stop
the display manager temporarily only when the card opens but DRM master is
owned by another process. Check owners with `sudo fuser -v /dev/dri/card*`.

**Current status:** Expected DRM ownership behavior. It is not treated as a
driver architecture problem.
