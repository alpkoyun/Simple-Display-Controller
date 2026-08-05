# Simple Display UEFI firmware

This directory contains the Shell-first implementation of the Simple Display
Controller firmware roadmap. Generated EDK II workspaces and `Build/` output
remain outside source control under `build/gop/`.

## Pinned toolchain

The build is pinned to the official EDK II stable release below:

```text
tag:    edk2-stable202605
commit: b03a21a63e3bd001f52c527e5a57feddb53a690b
arch:   X64
target: DEBUG (default) or RELEASE
tools:  GCC
```

EDK II removed the legacy `GCC5` toolchain definition before this release. The
current equivalent is the `GCC` toolchain family, so the implementation uses
`-t GCC` even where the original roadmap said `GCC5`.

## Build and local validation

Run the host contract checks first, then build all three EFI images:

```bash
scripts/test_uefi_contract.py
python3 scripts/test_uefi_shell_log.py
python3 scripts/test_uefi_cold_bind_validation.py
python3 scripts/test_uefi_auto_gop_validation.py
scripts/build_uefi.sh
```

The build script clones the exact pinned commit into `build/gop/edk2` unless
`EDK2_DIR` points at an existing checkout with the same commit. It writes the
deliverables and a SHA-256 manifest to `build/gop/artifacts/`:

```text
SimpleDisplayBringup.efi
SimpleDisplayGopDxe.efi
SimpleDisplayGopTest.efi
SHA256SUMS
build-metadata.txt
```

`scripts/build_uefi.sh --check` reports missing prerequisites without modifying
the workspace. The present host has NASM 2.15.05 installed at `/usr/bin/nasm`.

## 2026-08-04 automatic-binding repair build

The cold-state Driver Binding failure has been repaired in source. `Supported()`
now performs PCI identity/class matching without BAR2 MMIO. `Start()` saves the
entry PCI attributes and raw Command word, enables
`EFI_PCI_IO_ATTRIBUTE_MEMORY`, verifies the raw Command memory bit, and only
then validates BAR2 and SDC1. If firmware reports abstract memory decode as
enabled while the raw bit is clear, `Start()` writes and reads back that bit
directly before any MMIO.
Parent-only end-node starts retain private state so a later child request can
create the GOP child. Stop restores the abstract attributes and then writes
and reads back the exact entry Command word before releasing ownership. The
saved abstract I/O, memory, and bus-master bits are normalized to that raw
Command value so a later reconnect does not inherit stale firmware cache state.

The pinned DEBUG build and all host evidence-tool tests pass with:

```text
50c68de5bae9c57dbb44ce3279dece62d032f1b9e723ba6cd8c8c9f62a9f0aa1  SimpleDisplayBringup.efi
76183ab80b31a3a9f519b97f5a24ef26bdb917856595e34bfd97b7c8c2a74117  SimpleDisplayGopDxe.efi
4c21b7fc8c6c0baa1bf0c09236e645087386a52d6ece2f3639579f602722a61a  SimpleDisplayGopTest.efi
```

The prior
`818dde18...` build bound, installed two GOP handles, passed the GOP test, and
reconnected, but failed cleanup because PCI Command changed from `0000` to
`0002` after disconnect. That raw evidence is preserved under
`test_logs/2026-08-04_cold_bind_disconnect_restore_failure/`. The follow-up
`4acd6244...` build bound, installed two GOP handles, passed the GOP test, and
restored Command exactly to `0000`; however, both targeted reconnect attempts
returned `Not Found`, including after disconnecting the whole controller. That
raw evidence is preserved under
`test_logs/2026-08-04_cold_bind_reconnect_failure/`. The current `76183ab8...`
build adds raw memory-decode verification and fallback for every fresh
`Start()`. Its cold USB test passed without running
`SimpleDisplayBringup.efi`: targeted bind created the FPGA GOP,
`GOP_TEST_PASS` completed with visible FPGA output while the iGPU remained
undisturbed, disconnect restored Command exactly to `0000`, and targeted
reconnect recreated the FPGA GOP. The raw logs and exact tested payload are
sealed under `test_logs/2026-08-04_cold_bind_pass/` with
`COLD_GOP_BIND_VALIDATION_PASS`.

That pass authorizes packaging but is not automatic Option-ROM evidence,
because it used USB `load -nc` and targeted `connect`. The exact driver is now
packaged in the 32 KiB uncompressed ROM
`vivado_project/rom/simple_display_gop_option_rom.bin`, SHA-256
`eeacaa66703abb7fa10c2677cd9738fcb70b0dfe531c07a65bb8a52c0c11df7e`.
The isolated `PCIe_GOP_ROM_32K_AUTOBIND1` implementation and independent audit
pass with WNS `-0.780 ns` against the accepted `-2.500 ns` minimum, WHS
`+0.017 ns`, zero blocking DRC errors, zero unrouted/partially routed nets, and
AX7203 lane order `5,4,6,7`. Candidate hashes are `e08fd656...` for the `.bit`,
`17c2fbf6...` for the SPI `.bin`, and `fc18aae4...` for the `.ltx`.
The exact `.bin` has now passed erase, program, and verification on
`mt25ql128-spi-x1_x2_x4`, followed by restoration of the matching application
bitstream and probes to SRAM. Automatic binding remains open until a complete
shutdown/power-on proves configuration from this persistent image and a cold
Shell run produces a second FPGA GOP and `GOP_TEST_PASS` before any manual
`load` or `connect`.

## 2026-08-04 PixelBltOnly Linux-handoff repair

The persistent repaired Option ROM subsequently produced visible BIOS and
GRUB output, proving automatic driver selection and GOP consumption. Ubuntu
failed before network access even with `fpga_drm` blocked. A final controlled
boot with only `simpledrm_platform_driver_init` suppressed restored Ubuntu and
the iGPU display while leaving the FPGA output unused. Linux had created a
`simple-framebuffer.0` child below `0000:01:00.0`; preventing `simpledrm` from
binding that child isolated direct access to the GOP-advertised framebuffer as
the boot failure.

The host implementation now advertises `PixelBltOnly` with
`FrameBufferBase=0` and `FrameBufferSize=0`. Firmware rendering continues
through the custom `Gop.Blt()` path and its explicit 32-bit PCI I/O accesses.
`FrameBufferBltLib` and its unused private configuration state have been
removed. The updated GOP test requires the BLT-only contract and still covers
mode changes, visible fill, buffer transfers, readback, overlapping copies,
and invalid requests.

The new source and pinned DEBUG build pass all host tests with:

```text
50c68de5bae9c57dbb44ce3279dece62d032f1b9e723ba6cd8c8c9f62a9f0aa1  SimpleDisplayBringup.efi
cb9cf66ddbc3762d1261c7153e96a5f1a5f151b74b485f9ddcfe705177bb8fbe  SimpleDisplayGopDxe.efi
656851b2d143029842ec09df1f4c05359b36696f03386399836ff2e39de8df91  SimpleDisplayGopTest.efi
```

These hashes are not yet hardware-tested or authorized for Option-ROM
packaging. Resume from the manual cold-bind gate in the
[PixelBltOnly handoff record](../../doc/development_work/xdma_expansion_rom_gop_integration/2026-08-04_pixel_blt_only_linux_handoff_fix.md).

## 2026-07-25 host build evidence

The host contract test passed, EDK II built all three modules with GCC 11.4.0,
and a second clean build produced the same hashes:

```text
50c68de5bae9c57dbb44ce3279dece62d032f1b9e723ba6cd8c8c9f62a9f0aa1  SimpleDisplayBringup.efi
bed2193f46cd7042274297b2bafd4178d192fbf3d13eb771d058cf7a33a954fd  SimpleDisplayGopDxe.efi
a28a654dbd5443ef59465a5fb1ae344845eda51d2fb55514e18ac6d520def608  SimpleDisplayGopTest.efi
```

Those hashes identify the payload used for the first 2026-08-03 Shell test.
`SimpleDisplayBringup.efi` ran on the FPGA machine and reported every required
marker through `BRINGUP_PASS` at `1280x720@60`. The preserved raw capture is
under `test_logs/2026-08-03_shell_load_nc/`. This does not yet prove driver
binding or `GOP_TEST_PASS`.

## 2026-08-03 BLT-readback diagnostic build

The first connected GOP test visibly drew its purple guard rectangle and
yellow/orange inner pixels, then failed its first `VideoToBltBuffer` comparison.
The untouched log and analysis are under
`test_logs/2026-08-03_gop_test_failure/`. The diagnostic driver now performs
framebuffer reads, and read-dependent overlapping video copies, through
`EFI_PCI_IO_PROTOCOL.Mem.Read/Write` instead of ordinary CPU reads from the PCI
BAR. The test also reports actual and expected pixel values if the retry fails.

The pinned DEBUG build and host contract checks passed with these first-retry
hashes:

```text
50c68de5bae9c57dbb44ce3279dece62d032f1b9e723ba6cd8c8c9f62a9f0aa1  SimpleDisplayBringup.efi
418be0d90072866f5bdc778a80eb56d2d212a0d053946ae22a9d926125bff3ef  SimpleDisplayGopDxe.efi
4c21b7fc8c6c0baa1bf0c09236e645087386a52d6ece2f3639579f602722a61a  SimpleDisplayGopTest.efi
```

These are diagnostic retry artifacts, not `GOP_TEST_PASS` evidence.

The first retry then read pixel 0 correctly but found zero at adjacent pixel 1.
That result, preserved under `test_logs/2026-08-03_gop_test_retry/`, matched the
observed partial-width purple pattern and isolated the remaining ordinary CPU
write paths. The current driver now uses explicit 32-bit PCI I/O transactions
for every GOP framebuffer operation, including the `SetMode()` clear,
`VideoFill`, `BufferToVideo`, `VideoToBltBuffer`, and overlapping
`VideoToVideo` copies. Its verified retry payload is:

```text
50c68de5bae9c57dbb44ce3279dece62d032f1b9e723ba6cd8c8c9f62a9f0aa1  SimpleDisplayBringup.efi
dfc349b67d8bbbd4cad7fa0ae09c70a056c99c31855039c3ce5e7b66c6830eb9  SimpleDisplayGopDxe.efi
4c21b7fc8c6c0baa1bf0c09236e645087386a52d6ece2f3639579f602722a61a  SimpleDisplayGopTest.efi
```

The fresh manual Shell run using that exact driver now reports
`BLT_BUFFER_TRANSFER_PASS`, `BLT_OVERLAP_PASS`,
`BLT_INVALID_REQUEST_PASS`, and `GOP_TEST_PASS`. The untouched log, tested
payload, and analysis are under `test_logs/2026-08-03_gop_test_pass/`. This is
manual GOP validation, not automatic Option-ROM execution. Sealing the final
split-log evidence record succeeded after explicit visible-output and
targeted-bind confirmations. `scripts/check_uefi_shell_validation.py` reports
`SHELL_GOP_VALIDATION_PASS` for the exact current driver.

## Hardware and validation status

The hardware implements the validated direct-BAR contract: Xilinx `10ee:7024`,
64 MiB BAR2, framebuffer storage at BAR2 `+0x02000000`, scratch at
`+0x03fff000`, MM2S-only VDMA, and the fixed 60 Hz timing profiles already in
`fpga_drm`. The GOP deliberately does not expose that PCI storage as a linear
CPU framebuffer; it reports `PixelBltOnly` and accesses the storage only
through its custom PCI-I/O `Blt()` implementation.

The GOP driver now also requires the read-only `SDC1` identity block at BAR2
`+0x00080000`: magic `0x31434453`, ABI version `1.0`, direct-DDR and
Option-ROM feature bits, and a 4 KiB or 32 KiB ROM aperture. `Start()` rejects
absent or incompatible hardware after enabling PCI memory decoding and before
programming the display. The
reconciled reproducible Vivado source implements this ABI; the deployed
`fpga_hardware/` snapshot remains the authority for the currently programmed
board until the new ROM-capable image passes the isolated build and board
acceptance gates.

EDID discovery is intentionally absent. The HDMI device used by this design has
no usable public register interface or driver for EDID/DDC access. The GOP
therefore installs EDID Discovered and EDID Active protocols with zero-length,
null data and advertises only fixed modes.

Local build and contract tests do not complete the UEFI hardware gates. For the
automatic-binding repair, follow the cold-state section of
the [first-board UEFI Shell runbook](../../doc/development_work/gop_firmware_implementation/04_uefi_shell_runbook.md), then the
[validation and failure-isolation reference](../../doc/development_work/gop_firmware_implementation/03_validation_and_failure_isolation.md).

For the binding gate, use `load -nc` exactly once and then a targeted,
non-recursive connection:

```text
connect <fpga-controller-handle> <simple-display-driver-handle>
```

Discover both handles again on each boot.
Plain `load` recursively connects all handles in the TianoCore Shell and can
allow a Graphics Console consumer to invoke GOP `SetMode()` immediately,
obscuring whether Driver Binding `Start()` itself preserved the framebuffer.
