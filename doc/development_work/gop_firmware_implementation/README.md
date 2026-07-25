# GOP Firmware Implementation

Status: **Stages 0-2 source implemented and host-built; UEFI Shell hardware
validation pending**

This package converts the completed PCIe DDR-bypass milestone into an
executable UEFI/GOP development sequence. The plan deliberately separates four
questions that otherwise fail together and are difficult to diagnose:

1. Can UEFI software find the FPGA and access its BARs?
2. Can UEFI software initialize the pipeline and display a direct-BAR frame?
3. Can a UEFI driver expose a correct Graphics Output Protocol instance?
4. Can the motherboard load that driver automatically from a PCI Option ROM?

## Primary decision

Do **not** change the direct-BAR framebuffer contract or begin with the Option
ROM. The next executable artifact should be a verbose X64 UEFI Shell
application named `SimpleDisplayBringup.efi`. It will use the already
validated BAR2 contract to
initialize one `1280x720@60` mode and fill the framebuffer. The application can
print every result to the existing firmware console, making BAR discovery,
register access, clock/VDMA status, and HDMI failures observable.

The current FPGA image is sufficient for the Shell application and manually
loaded GOP stages. Its independent XDMA H2C requester currently emits no AXI
stream during normal Linux uploads. That does not block GOP pixels, because
GOP uses CPU writes through BAR2, but it does block the final native-Linux
handoff until H2C is repaired or `fpga_drm` gains a validated direct-BAR upload
backend.

The shared hardware library, Shell bring-up application, Shell-loaded GOP
driver, and GOP test application now build from the pinned EDK II release.
This source availability does not waive the stop/go gate: run the bring-up
application and obtain visible FPGA output before loading the DXE driver on
hardware. Package an Option ROM only after the Shell-loaded driver is
repeatable.

`1280x720@60` is the first mode because that exact bypass-frame and VDMA setup
has already passed the Linux diagnostic. The HDMI device on this board does not
provide a usable, publicly documented EDID/DDC read interface for this setup,
so the firmware does not attempt EDID acquisition. The GOP driver installs
EDID Discovered and EDID Active protocol instances with `SizeOfEdid=0` and
`Edid=NULL`, then exposes only exact modes already supported by the Linux
driver and individually validated in UEFI.

## Implemented artifacts

- `firmware/uefi/SimpleDisplayPkg` pins the PCI/BAR/register contract and six
  Linux-matched 60 Hz timing candidates.
- GOP initially exposes only the proven `1280x720@60` mode. Promote the other
  fixed candidates only after their visible UEFI tests pass.
- `SimpleDisplayBringup.efi` enumerates PCI handles, validates BAR2, performs a
  destructive-test-with-restore on the scratch page, programs MM2S-only
  scanout, fills color bars, and prints bounded status checkpoints.
- `SimpleDisplayGopDxe.efi` creates an HDMI child, makes no visible change in
  `Start()`, installs GOP plus empty EDID protocols, and delegates all four BLT
  operations to `FrameBufferBltLib` after overflow-safe validation.
- `SimpleDisplayGopTest.efi` checks modes, empty EDID, buffer transfers, guard
  pixels, overlapping video copies, and invalid requests.
- `scripts/test_uefi_contract.py` cross-checks the fixed 60 Hz timing candidates
  against `fpga_drm`; `scripts/build_uefi.sh` pins EDK II and emits hashes.

On 2026-07-16 the host contract test passed and two clean DEBUG/GCC X64 builds
produced identical `.efi` hashes. UEFI Shell, monitor-visible, and protocol
runtime evidence remain pending.

## Documents

1. [Execution roadmap](01_execution_roadmap.md)
2. [First firmware contract](02_first_firmware_contract.md)
3. [Validation, evidence, and failure isolation](03_validation_and_failure_isolation.md)
4. [First board run: UEFI Shell runbook](04_uefi_shell_runbook.md)

## Inputs from completed work

- [Current shared 32 MiB prerequisite](../shared_32m_ddr_gop_prerequisite/README.md)
- [Current hardware address contract](../shared_32m_ddr_gop_prerequisite/01_current_contract.md)
- [Live direct-path and H2C evidence](../shared_32m_ddr_gop_prerequisite/02_live_validation_and_h2c_gap.md)
- [UEFI driver design research](../../gop_research/03_uefi_driver_design.md)
- [Storage, build, and loading research](../../gop_research/04_storage_build_and_loading.md)

## Completion definition

This package is complete only when a cold boot, without JTAG and without a
driver copied from disk, visibly progresses through:

```text
FPGA configuration from SPI
  -> PCI enumeration
  -> card-local X64 Option ROM execution
  -> GOP plus empty EDID protocol instances installed
  -> UEFI or bootloader graphics on FPGA HDMI
  -> early Linux firmware framebuffer
  -> native fpga_drm desktop
```

A Shell-loaded pattern or GOP instance is an important intermediate milestone,
not the final deployment state. The final chain additionally requires a
working native Linux upload path; that gate is currently open work.
