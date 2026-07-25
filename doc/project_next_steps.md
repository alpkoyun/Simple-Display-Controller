# Project Next Steps

This is the living project-level tracker for Simple-Display-Controller. Exact
commands and raw runtime evidence belong in
`Linux_DRM_Driver/tests/TEST_LOG.md`; focused development packages belong under
`doc/development_work/`.

Last reviewed: 2026-07-16

## Current status

The active project direction is a UEFI Graphics Output Protocol driver using a
direct BAR-backed DDR framebuffer.

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

Still failing on the current export:

- normal XDMA H2C uploads remain `BUSY` with zero completed descriptors; and
- the stream ILA shows VDMA ready (`TREADY=1`) but no XDMA stream data
  (`TVALID=0`).

The direct GOP path does not depend on H2C. Native `fpga_drm` takeover does, so
the two tracks proceed independently until the handoff milestone.

## Current workflow decisions

### Hardware source

The design is still being changed by hand. The export under
`fpga_hardware/PCIe_wrapper/` is the authority for current driver and live-test
work. Synchronizing the tracked Vivado recreate/export source is deferred until
the hardware contract is finished and becomes a freeze/release gate.

### GOP modes and EDID

This board setup has no usable, publicly documented EDID/DDC read interface
through its HDMI device. The GOP driver therefore:

- installs EDID Discovered and EDID Active with `SizeOfEdid=0` and
  `Edid=NULL`;
- uses a fixed timing table derived from the modes already supported by
  `fpga_drm`;
- begins by advertising only the live-validated `1280x720@60` mode;
- keeps the other Linux-supported 60 Hz timings as source-level candidates;
  and
- advertises each additional mode only after it passes UEFI-visible scanout.

### Deployment order

Do not begin with the PCI Option ROM. Prove the hardware from a verbose UEFI
Shell application, then build a manually loaded GOP driver, then test firmware
consumers and Linux handoff, and only then embed the driver in the FPGA image.

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
| Pending | UEFI PCI/BAR/scratch probe | Shell application reports `PCI_MATCH`, `BAR2_OK`, and `SCRATCH_RESTORE_OK`. |
| Pending | UEFI visible frame | Shell application shows stable correct `1280x720@60` color bars. |
| Pending | UEFI runtime GOP and BLT | GOP handle, empty EDID protocols, fixed tested modes, and complete BLT tests pass. |
| Pending | Firmware console/bootloader | A real firmware or bootloader consumer uses the FPGA GOP. |
| Blocked by H2C | Native Linux handoff | GOP to early framebuffer to working native `fpga_drm`. |
| Pending | Hardware version register and Option ROM | Added only after manual GOP is repeatable. |
| Pending | Cold/warm boot reliability | Planned AC-power and warm-reboot matrix passes. |

## Immediate next steps

### 1. Run the implemented Shell bring-up gate

- follow the [first-board UEFI Shell runbook](development_work/gop_firmware_implementation/04_uefi_shell_runbook.md);
- copy `build/gop/artifacts/SimpleDisplayBringup.efi` to a FAT USB or EFI System
  Partition;
- boot UEFI Shell and capture `PCI_MATCH`, `BAR2_OK`, scratch restoration, and
  every pipeline checkpoint;
- require stable visible `1280x720@60` color bars before proceeding; and
- keep the current hand-edited hardware export authoritative until the design
  is ready to freeze.

### 2. Validate the Shell-loaded GOP

- load `SimpleDisplayGopDxe.efi` only after the bring-up gate passes;
- connect the specific FPGA controller and confirm the HDMI child protocols;
- run `SimpleDisplayGopTest.efi`; and
- record empty EDID, mode, BLT, bounds, overlap, and guard-pixel results.

### 3. Promote fixed mode candidates one at a time

- the shared table already contains `640x480@60`, `800x600@60`,
  `1024x768@60`, `1280x1024@60`, and `1920x1080@60` candidates copied from
  `fpga_drm`;
- GOP continues to expose only `1280x720@60` until each additional timing has
  its own visible UEFI evidence; and
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

- validate the remaining fixed resolutions, including maximum
  `1920x1080@60` direct scanout;
- prove firmware-console and bootloader selection separately from GOP protocol
  existence;
- implement early Linux firmware-framebuffer ownership and native DRM takeover;
- add the hardware contract/version register;
- add and validate the read-only PCI Expansion ROM path; and
- run the full cold/warm reliability and recovery matrix.

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
