# 2026-08-03 `load -nc` Shell evidence analysis

## Evidence preservation

The six Shell files were copied byte-for-byte from
`/media/alpk/ALP/EFI/SimpleDisplay/evidence/` into `raw/`.  The SHA-256 values
in `usb_raw_sha256.txt` and `workspace_raw_sha256.txt` match for every file.
All six raw inputs are US-ASCII; no decoding or newline conversion was applied
to the preserved files.

The Shell-tested `SimpleDisplayGopDxe.efi` on the USB and the current workspace
artifact both have SHA-256:

`bed2193f46cd7042274297b2bafd4178d192fbf3d13eb771d058cf7a33a954fd`

## What this run proves

- `uefi-shell.log` identifies the endpoint as `10ee:7024`, revision `00`, at
  `0000:01:00.0` and reports a 64 MiB BAR2 at `0xF0000000`.
- The standalone application reports every required marker through
  `BRINGUP_PASS`: `BAR2_OK`, `SCRATCH_RESTORE_OK`, `HDMI_I2C_OK`,
  `CLOCK_LOCK_OK`, `VDMA_MM2S_OK`, `VTC_OK`, and `FRAME_FILL_OK`.
- The application reports `1280x720@60`, a `0x384000`-byte frame, and that
  scanout and PCI memory decoding were intentionally left active.
- `devices.txt` and `devices_after.txt` are byte-identical.  No new child
  device or bound GOP controller appears after the standalone bring-up.
- The current Linux check after boot still finds `0000:01:00.0` as
  `10ee:7024`, and `display-manager` is active.

The generated bring-up pattern is eight broad horizontal colour bands.  A
stable horizontal-band pattern is therefore the intended orientation; it is
not supposed to be vertical bars.

## What `load -nc` proves, and does not prove

`drivers.txt` already contains an unconnected SimpleDisplay image at driver
handle `119`.  `drivers_after.txt` retains `119` and adds a second unconnected
copy at `11B`.  Both rows have driver type `?` and no device/controller counts.
Together with the byte-identical device tables, this shows that the non-connect
load path loaded images without binding the driver or changing the handle
topology.

The added `11B` row means that `SimpleDisplayGopDxe.efi` was loaded a second
time between these driver captures.  Driver and controller handle values are
boot-local and must be rediscovered in any later Shell session.

This capture does **not** prove any of the following:

- a SimpleDisplay driver binding to the FPGA controller;
- installation of a GOP protocol or creation of a GOP child handle;
- preservation of either visible display during driver `Start()`;
- `SimpleDisplayGopTest.efi` or `GOP_TEST_PASS`.

Accordingly, no `SHELL_GOP_VALIDATION_PASS` record is emitted.

## Timestamp boundary

`pci-shell.log` is timestamped `16:00:32`, while the new device, driver, and
bring-up captures are timestamped `16:33:02` through `16:34:40`.  It confirms
UEFI enumeration from the earlier session but is not treated as the detailed
PCI capture for this later `load -nc` sequence.  The later `uefi-shell.log`
provides the fresh endpoint identity used above.

## Next controlled gate

On the next boot, load the driver exactly once with `load -nc`, recapture the
current `devices` and `drivers` handles, and connect only the SimpleDisplay
driver to the FPGA controller.  For this capture the likely pair was controller
`E9` and driver `119`, but those values must not be hard-coded across boots.
Do not use `connect -r` for that diagnostic.  Visible iGPU and FPGA-HDMI output
before, during, and after the targeted connection still requires direct human
confirmation.
