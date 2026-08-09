# Project Next Steps

This is the living project-level tracker for Simple-Display-Controller. Exact
commands and raw runtime evidence belong in
`Linux_DRM_Driver/tests/TEST_LOG.md`; focused development packages belong under
`doc/development_work/`.

Last reviewed: 2026-08-05

## Current status

The active project direction is a UEFI Graphics Output Protocol driver using a
BLT-only public contract over the existing BAR-backed DDR storage. Firmware
access remains direct PCI I/O, but no linear framebuffer is advertised.

The July 16 hardware contract is:

```text
PCI function                 10ee:7024, class 038000
BAR2                         64 MiB, 64-bit prefetchable
bypass AXI translation      0x3c000000
shared bypass/VDMA DDR      0x3e000000-0x3fffffff
GOP framebuffer             BAR2+0x02000000 / AXI 0x3e000000
scratch page                BAR2+0x03fff000 / AXI 0x3ffff000
```

Validated:

- the strict hardware checker passes;
- the non-destructive BAR2 scratch test passes;
- a complete `1280x720@60` XRGB8888 frame written through BAR2 is scanned by
  VDMA MM2S from the same address; and
- the attached monitor showed correct color bars.

The accepted shell-stage restoration produced six bounded asynchronous H2C
frame completions with zero upload failures. This is a no-regression result
for that image, not a replacement for the separate historical stalled-H2C
record or a long-duration stress claim. The direct GOP path remains independent
of H2C.

The tracked XDMA Expansion-ROM integration is now live in FPGA SRAM. After
JTAG programming and a reboot, Linux enumerated revision `0x00`, preserved
64 KiB BAR0 and 64 MiB BAR2, advertised a 4 KiB Expansion ROM, and returned
three byte-exact 4096-byte reads with the pinned transport-ROM hash. The PCI
Command, ROM BAR, enable counter, and unbound driver state were restored after
the proof, with no unexpected PCIe/AER kernel errors. Live SDC1 reads returned
magic `0x31434453`, ABI `1.0`, features `0x00000003`, and a 4096-byte ROM
aperture. The independent scratch, direct-framebuffer, VDMA, matching-LTX ILA,
normal `fpga_drm`, bounded H2C, and restoration regressions also pass.

That paragraph records the historical 4 KiB Shell-validation checkpoint.
The canonical source checkout and persistent FPGA flash have advanced to one
fixed 32 KiB uncompressed EFI/GOP configuration. Packaging, source/generated
contracts, both focused simulations, fresh recreation, and the 200 MHz host
implementation pass. The
routed result is WNS `-0.982 ns` against the explicit `-2.500 ns` project
threshold, WHS `+0.032 ns`, zero blocking DRC errors, and zero unrouted nets.
The matching image has now been programmed and reboot-enumerated. Linux
advertised a 32 KiB aperture and three 32768-byte reads matched the packaged
ROM SHA-256 byte-for-byte; state restoration passed with no new AER errors.
No pre-OS FPGA display output was observed. Live SDC1 and the remaining
controlled Linux/display regressions remain independent. Live SDC1 now passes
with aperture `32768`, and after installing the current-kernel module,
`fpga_drm` autoloaded on reset and restored a visible `1280x720@60` OS display.
Successful transport and Linux DRM output are not being reclassified as
automatic firmware dispatch. The accepted fixed image has now been written to
the AX7203 `mt25ql128-spi-x1_x2_x4` configuration flash; erase, program, and
verify all passed. A full shutdown/power-on then configured the FPGA from
flash, produced normal Linux PCI enumeration, and restored `fpga_drm` output.

The no-`load` UEFI Shell capture closes automatic firmware dispatch: the FPGA
controller's `BusSpecificDriverOverride` points to an embedded driver at
`Offset(0x200,0x7fff)`, and that handle contains both `LoadedImage` and
`DriverBinding`. Targeted `connect` nevertheless returned `Not Found`, only
the Intel GOP handle existed, and the GOP test found no Simple Display child.
The controller's PCI Command was `0000`. The confirmed source defect was
that `Supported()` and `Start()` read SDC1 through BAR2 before `Start()` enables
PCI memory decoding. Dispatch passes; automatic driver binding and GOP child
installation did not. The host-side ordering/ownership fix now builds and
passes contracts. Its separate cold USB validation also passes targeted bind,
GOP operation, exact cleanup, and reconnect; this proves the driver repair but
does not reclassify the old persistent ROM as an automatic-binding pass.
The exact repaired ROM has now also completed the isolated
`PCIe_GOP_ROM_32K_AUTOBIND1` recreation, implementation, bitstream generation,
and independent audit. The deployment candidate has WNS `-0.780 ns` against
the project minimum `-2.500 ns`, WHS `+0.017 ns`, zero blocking DRC errors,
zero unrouted or partially routed nets, and the required AX7203 lane order
`5,4,6,7`. The exact `.bit` and matching `.ltx` have now been programmed into
FPGA SRAM over JTAG. The repaired `17c2fbf6...` SPI `.bin` has also completed
erase, programming, and Vivado verification on `mt25ql128-spi-x1_x2_x4`, after
which the matching application `.bit`/`.ltx` were restored to SRAM. The stale
post-programming PCI view is not enumeration evidence; configuration from SPI
still requires a shutdown/power-on proof.

That proof is now complete for automatic GOP consumption: the FPGA-connected
display showed firmware setup and the GRUB menu. Linux then failed before
network access. Blocking `fpga_drm` and using `nomodeset` did not help. A boot
with only `simpledrm_platform_driver_init` suppressed restored Ubuntu and the
iGPU primary display while leaving the FPGA display unused. The endpoint
enumerated normally and `simple-framebuffer.0` existed below `0000:01:00.0`
without a bound driver. This isolates `simpledrm` accessing the
GOP-advertised linear framebuffer.

The host fix now reports `PixelBltOnly`, `FrameBufferBase=0`, and
`FrameBufferSize=0`, while retaining the custom 32-bit PCI-I/O `GOP.Blt()`
path. Host tests and the pinned EDK II build pass for driver SHA-256
`cb9cf66ddbc3762d1261c7153e96a5f1a5f151b74b485f9ddcfe705177bb8fbe`.
The same exact payload now passes cold manual binding, `GOP_TEST_PASS`,
disconnect cleanup with PCI Command restored to `0000`, reconnect, renewed GOP
recognition, and a second successful GOP test. The sealed evidence is under
`firmware/uefi/test_logs/2026-08-05_pixel_blt_only_cold_bind_pass/`; its tested
driver is packaged into the new 32 KiB replacement ROM. The isolated hardware
build, JTAG programming, automatic FPGA BIOS output, normal Ubuntu boot, and
SSH reconnection now pass for that exact replacement candidate. Three
byte-exact 32768-byte Linux reads also match the replacement ROM and restore
the PCI state with no new AER errors. An explicit current-boot `fpga_drm` load
binds the FPGA, configures an active `1280x720@60` CRTC, and completes two
full-frame uploads while i915 remains active. The temporary blacklist was then
disabled, and the user confirmed usable FPGA Linux output after a reboot with
boot-time autoload. The exact PixelBltOnly SPI image has now passed erase,
program, Vivado verification, and matching application-bitstream restoration.
A complete power-off/power-on then produced visible BIOS and Ubuntu output,
normal PCI enumeration, automatic `fpga_drm` binding, and sustained uploads.
Persistent PixelBltOnly deployment is accepted.

## Current workflow decisions

### Hardware source

The tracked recreate/export under `vivado_project/` is reconciled to the
validated 64 MiB BAR2/shared-DDR contract and is authoritative for the fixed
32 KiB Expansion-ROM integration. Git selects the hardware configuration;
the flow has no stage variable. The fixed 32 KiB EFI/GOP image is now the
persistent SPI-flash checkpoint. Its previous Shell-stage image can be
recovered by programming that checkpoint's preserved artifacts over JTAG or
back into flash.

### GOP modes and EDID

This board setup has no usable, publicly documented EDID/DDC read interface
through its HDMI device. The GOP driver therefore:

- installs EDID Discovered and EDID Active with `SizeOfEdid=0` and
  `Edid=NULL`;
- uses a fixed timing table derived from the modes already supported by
  `fpga_drm`;
- advertises only `1920x1080@60` as GOP mode 0 in the current source; the
  exact replacement driver now passes the cold manual bind, visible-output,
  BLT, disconnect, and reconnect gate;
- reports `PixelBltOnly` with zero framebuffer base/size so firmware consumers
  must use the validated `GOP.Blt()` path;
- keeps the other Linux-supported 60 Hz timings as source-level candidates;
  and
- advertises each additional mode only after it passes UEFI-visible scanout.

### Deployment order

Automatic driver selection and visible firmware/GRUB output now pass for the
current persistent image. The replacement `PixelBltOnly` driver now passes the
cold USB bind/disconnect/reconnect gate and has been sealed. Only that exact
tested driver is packaged in the replacement ROM. The isolated 32 KiB hardware
build and independent implementation audit pass, and the audited bit/LTX pair
is now loaded into FPGA SRAM. Automatic BIOS output, normal Ubuntu boot, exact
ROM readback, visible native output, and boot-time `fpga_drm` autoload pass.
The exact PixelBltOnly SPI image has also passed erase, program, verification,
and SRAM restoration. The first full power cycle from SPI now passes visible
FPGA output, Ubuntu/SSH, normal PCI enumeration, automatic `fpga_drm` binding,
zero-error VDMA/VTC mode readback, and 437 completed uploads. Two transient
`-22` submissions during the 1280x720-to-1920x1080 transition recovered
immediately and remain documented. Further boot cycles are optional reliability
characterization.

The `1920x1080@60` replacement now passes its exact cold manual GOP gate. Both
the initial bind and the reconnect report mode 0 as `1920x1080`,
`PixelBltOnly`, `PixelsPerScanLine=1920`, followed by every BLT pass marker.
Disconnect removes only the FPGA GOP and restores PCI Command `0000`; reconnect
re-enables memory decoding and installs a new FPGA GOP child. The exact tested
driver is packaged in the new 32 KiB ROM. The isolated
`PCIe_GOP_ROM_32K_1080P_DEFAULT` recreation, focused simulations,
implementation, and independent audit pass with WNS `-0.753 ns`, WHS
`+0.035 ns`, minimum bus-skew slack `+3.661 ns`, zero DRC errors, zero route
errors, and lane order `5,4,6,7`. Its hash-sealed bit/LTX pair is now loaded in
FPGA SRAM over JTAG. The immediate `rev ff` PCI view is expected after live
endpoint replacement; a reboot is the next required gate.

## Milestone checklist

| Status | Milestone | Evidence or gate |
|---|---|---|
| Done | Shared 32 MiB DDR map | Bypass, MM2S, and S2MM use `0x3e000000-0x3fffffff`. |
| Done | Direct BAR scratch test | Save/write/read/restore passes at BAR2 `+0x03fff000`. |
| Done | Direct `1280x720@60` scanout | Correct visible color bars from BAR2 `+0x02000000`. |
| Blocked | Native XDMA H2C upload | `BUSY`, completed descriptors zero, `TREADY=1`, `TVALID=0`. |
| Pending | Commit canonical GOP docs and validation scripts | Development packages and helper scripts are currently untracked. |
| Done | Pinned EDK II X64 build | `edk2-stable202605` clean DEBUG/GCC builds produce three repeatable X64 EFI hashes. |
| Done | UEFI firmware source set | Shared hardware library, bring-up app, GOP DXE driver, GOP test app, and host contract test are implemented. |
| Done | 4 KiB Expansion-ROM transport | Post-reboot config space advertises 4 KiB; three 4096-byte Linux reads match the pinned ROM input byte-for-byte. |
| Done | 4 KiB SDC1 shell-stage host implementation | Isolated synthesis/implementation retains ROM and SDC1, has zero DRC errors/unrouted nets, positive hold, and WNS `-2.376 ns` accepted under the explicit project-wide `-2.500 ns` hobby threshold. |
| Done | Live SDC1 and display regressions | Fresh enumeration, three byte-exact ROM reads, live SDC1, DDR save/write/read/restore, direct scanout, matching-LTX ILA, normal `fpga_drm`, bounded H2C, and kernel/AER/restoration checks pass. |
| Done | UEFI PCI/BAR/scratch probe | Shell application reports `PCI_MATCH`, `BAR2_OK`, and `SCRATCH_RESTORE_OK`. |
| Done | UEFI visible frame | User confirmed stable, correctly colored horizontal `1280x720@60` bands that remained active after the bring-up application exited. |
| Done | UEFI runtime GOP and BLT | Targeted binding left the iGPU Shell console undisturbed; empty EDID, fixed mode, transfer, overlap, and invalid-request checks ended in `GOP_TEST_PASS`. |
| Done | Firmware console/bootloader | The FPGA display showed firmware setup and the GRUB boot menu through the automatically loaded GOP. |
| Done | Native Linux handoff | The PixelBltOnly JTAG candidate boots Ubuntu normally; technical DRM state and uploads pass, the temporary blacklist is disabled, and the user confirmed usable FPGA Linux output after a reboot with boot-time `fpga_drm` autoload. |
| Done | 4 KiB shell ABI and manual GOP | Linux regressions and the sealed split-log Shell/GOP evidence pass with the exact tested driver hash. |
| Done | Fixed 32 KiB EFI Option-ROM host build | `PCIe_GOP_ROM_32K_AUTOBIND1` packaging, exact-driver check, source/generated contracts, focused simulation, fresh recreation, bitstream generation, and independent audit pass with WNS `-0.780 ns`, WHS `+0.017 ns`, zero blocking DRC errors, and zero unrouted/partially routed nets. |
| Done | PixelBltOnly 32 KiB hardware ROM transport | The replacement JTAG image passed reboot enumeration, three byte-exact 32768-byte reads matching `75f02aa...`, PCI-state restoration, and no-new-AER checks. |
| Done | Previous 32 KiB live SDC1 | The pre-repair image returned magic `SDC1`, ABI `1.0`, features `0x3`, aperture `32768`, followed by exact PCI-state restoration. |
| Done | Previous 32 KiB normal Linux display | The pre-repair image let kernel-matched `fpga_drm` autoload, bind `10ee:7024`, configure `1280x720@60`, complete uploads, and visibly restore the FPGA display. |
| Done | Fixed 32 KiB persistent SPI programming | The current repaired 9,730,652-byte `.bin` (`17c2fbf6...`) was written to `mt25ql128-spi-x1_x2_x4`; erase, program, and verify passed, then the matching `e08fd656...` bit / `fc18aae4...` LTX were restored to SRAM. |
| Done | Previous persistent-image cold boot | A full shutdown/power-on configured the pre-repair FPGA image from flash; Linux enumerated normal `10ee:7024` and bound `fpga_drm`. |
| Done | Previous 32 KiB automatic EFI dispatch | Without USB `load`, firmware exposed the pre-repair embedded `Offset(0x200,0x7fff)` image as a loaded boot-service driver with Driver Binding and associated it with `10ee:7024`. |
| Done | Repaired persistent-image cold boot and automatic GOP | SPI configured the repaired image and produced visible firmware and GRUB output through the FPGA GOP. |
| Done | PixelBltOnly persistent SPI programming | The exact `b3371e99...` SPI image passed erase, program, and Vivado verification on `mt25ql128-spi-x1_x2_x4`; the matching `d00ccfbb...` bit and `fc18aae4...` LTX were restored to SRAM. |
| Done | PixelBltOnly persistent cold boot | A complete power-off/power-on from SPI produced visible BIOS and Ubuntu output, SSH, normal `10ee:7024`, automatic `fpga_drm`, zero-error mode readback, and 437 completed uploads; two mode-switch `-22` submits recovered immediately. |
| Done | PixelBltOnly replacement GOP | Host/manual gates, 32 KiB packaging, implementation/audit, JTAG programming, automatic FPGA BIOS output, normal Ubuntu boot, SSH, exact ROM readback, visible native DRM/autoload, SPI programming, and persistent cold-boot acceptance all pass. |
| Done | 1080p-default manual GOP | The exact driver reports one `1920x1080` PixelBltOnly mode with `PixelsPerScanLine=1920`; cold bind, visible output, all BLTs, disconnect cleanup, reconnect, and the second GOP test pass. |
| Done | 1080p-default 32 KiB host build | Packaging, source/generated contracts, focused simulations, fresh implementation, and independent audit pass with WNS `-0.753 ns`, WHS `+0.035 ns`, bus-skew slack `+3.661 ns`, zero DRC errors, zero route errors, and lane order `5,4,6,7`. |
| Done | 1080p-default JTAG programming | The exact `a9ffe409...` bit and matching `fc18aae4...` LTX programmed successfully; one 58-probe XDMA ILA matched. |
| Done | 1080p-default automatic GOP boot | Without USB `load` or manual `connect`, the user observed visible FPGA firmware/bootloader output at 1920x1080 from the automatic GOP path. |
| Done | 1080p-default Linux DRM handoff | The installed `09C807...` module bound automatically; 1920x1080 is the preferred connector mode and the first CRTC enable was `1920x1080@60`, with locked clock, zero VTC errors, and continuing visible uploads. |
| Done | 1080p-default persistent SPI deployment | The exact `8114d3ee...` SPI image passed erase/program/verify; after a complete shutdown and power-on without JTAG programming, FPGA firmware-to-login output, normal `10ee:7024` enumeration, and automatic 1080p `fpga_drm` scanout passed. Evidence: `artifacts/expansion_rom/20260805_1080p_default_spi_program/` and `artifacts/expansion_rom/20260805_1080p_default_spi_cold_boot_cycle1/`. |
| Done | 1080p-default byte-exact ROM readback | In a controlled display-manager-off window, three complete 32768-byte reads matched SHA-256 `5c5cc1d6...` and the expected image byte-for-byte; the EFI Option-ROM contract, PCI/driver/display restoration, resumed 1080p uploads, and no-new-AER checks passed. Evidence: `artifacts/expansion_rom/20260805_1080p_default_spi_cold_boot_cycle1/rom_readback/`. |
| In progress | Fixed 32 KiB controlled Linux/display regression | BAR2 scratch, direct scanout, and the remaining controlled restoration evidence are still separate gates. |
| Optional | Cold/warm boot reliability | Additional AC-power and warm-reboot cycles can characterize long-run reliability; they are not required for PixelBltOnly fix acceptance. |

## Immediate next steps

### 1. Preserve the accepted fixed 32 KiB host checkpoint

- retain the implementation contract and artifact hashes from
  `PCIe_GOP_ROM_32K_TIMING2`;
- retain the repaired deployment candidate, implementation contract, and
  artifact hashes from `PCIe_GOP_ROM_32K_AUTOBIND1` without overwriting the
  prior checkpoint;
- retain both untouched raw Shell logs and the exact tested EFI payload; and
- invalidate and repeat the manual gate if the GOP driver hash changes.

### 2. Validate and deploy the PixelBltOnly handoff fix

- retain the sealed cold bind/GOP/disconnect/reconnect evidence and exact
  driver `cb9cf66d...` / GOP-test `656851b2...` payload;
- retain the independently audited isolated
  `PCIe_GOP_ROM_32K_PIXEL_BLT_ONLY` build containing only that tested driver
  under `artifacts/expansion_rom/20260805_pixel_blt_only_32k_build/`;
- require `WNS >= -2.5 ns`, nonnegative hold, zero blocking DRC errors, zero
  unrouted nets, and lane order `5,4,6,7` without timing-tuning a passing build
  (observed WNS `-0.777 ns`, WHS `+0.049 ns`, zero DRC errors, zero route
  errors, and the required lane order);
- retain the successful JTAG-loaded reboot evidence: automatic BIOS output on
  the FPGA display, a normal Ubuntu boot without diagnostic kernel options,
  and SSH reconnection with `fpga_drm` still blocked;
- retain the completed three byte-exact 32 KiB ROM reads, exact PCI-state
  restoration, and no-new-AER evidence;
- retain the user's usable FPGA Linux-screen confirmation after enabling
  boot-time `fpga_drm` autoload;
- retain the completed PixelBltOnly SPI erase/program/verify and matching SRAM
  restoration evidence under
  `artifacts/expansion_rom/20260805_pixel_blt_only_spi_program/`;
- retain the completed persistent power-cycle evidence under
  `artifacts/expansion_rom/20260805_pixel_blt_only_spi_cold_boot_cycle1/`,
  including the BIOS/OS observation and recovered mode-switch warning; and
- treat any additional cold/warm cycles as optional reliability
  characterization rather than a remaining fix gate.

Detailed evidence, source diagnosis, and the new-session execution order are
in the
[PixelBltOnly Linux-handoff fix](development_work/xdma_expansion_rom_gop_integration/2026-08-04_pixel_blt_only_linux_handoff_fix.md).

### 3. Validate and deploy the 1920x1080@60 default

- the shared table keeps all six Linux-matched 60 Hz candidates, with
  `1920x1080@60` now at index 0 and `1280x720@60` retained as a fallback
  candidate;
- GOP continues to expose exactly one mode, now `1920x1080@60`, and the Linux
  connector marks that same timing as its sole preferred mode;
- retain the sealed cold-bind, exact-mode, BLT, disconnect/reconnect, and
  visible-output evidence under
  `firmware/uefi/test_logs/2026-08-05_1080p_cold_bind_pass/`;
- retain the audited build under
  `artifacts/expansion_rom/20260805_1080p_default_32k_build/` and the JTAG
  programming evidence under
  `artifacts/expansion_rom/20260805_1080p_default_32k_jtag_program/`;
- reboot the JTAG-loaded image without USB `load` or manual `connect`, then
  validate automatic firmware/GRUB use;
- install and validate the newly built Linux module, requiring
  `1920x1080@60` to be the sole preferred connector mode; and
- defer SPI programming until both automatic UEFI and Linux gates pass; and
- keep 30 Hz timing duplicates out of generic GOP because GOP mode information
  cannot distinguish refresh rate.

### 4. Recover the native Linux upload path in parallel

Investigation order:

1. reproduce a bounded transfer with the Xilinx reference XDMA driver;
2. confirm descriptor-fetch/requester behavior and PCIe/AER status;
3. compare current XDMA requester and streaming settings with the last
   known-good export; and
4. require completed descriptors plus ILA `TVALID && TREADY` handshakes before
   calling H2C repaired.

A separately validated direct-BAR upload backend in `fpga_drm` remains an
alternative to H2C recovery.

## Later gates

- validate any remaining non-default fixed resolutions one at a time after the
  `1920x1080@60` persistent default passes;
- prove firmware-console and bootloader selection separately from GOP protocol
  existence;
- implement early Linux firmware-framebuffer ownership and native DRM takeover;
- retain the accepted automatic driver binding, GOP installation, persistent
  SPI boot, and Linux recovery evidence for the fixed 32 KiB EFI Option ROM;
  and
- run the broader cold/warm reliability and recovery matrix if extended
  robustness characterization is desired.

## Deferred Linux display work

The CPU-backed KMS overlay milestone remains valid, including direct atomic
commit, negative scaling/bounds tests, and a nonzero `cpu_compositions`
counter. Compositor-friendly plane properties, Weston plane assignment, FPGA
composition, and render-node work are deferred while GOP and native upload
recovery are the active priorities.

## Update rule

After each completed step:

1. record exact commands and evidence in the relevant test log;
2. update this checklist and current-status section;
3. update the corresponding package under `doc/development_work/`; and
4. synchronize README and design documents when the validated behavior or
   hardware contract changes.
