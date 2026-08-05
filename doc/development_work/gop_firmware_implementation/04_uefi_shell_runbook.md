# First Board Run: UEFI Shell Runbook

This runbook covers the first manual execution of the Simple Display firmware.
It stops after the Shell bring-up application and manually loaded GOP tests.
It does not install an Option ROM, change the motherboard firmware, or test the
Linux handoff.

## What equipment is required?

| Item | Required? | Purpose |
|---|---:|---|
| Ordinary USB flash drive | Recommended | Carries the UEFI Shell and the three Simple Display EFI files. It is unnecessary if the motherboard already has a UEFI Shell and the files are placed on another firmware-readable FAT volume. |
| X64 UEFI Shell | Yes | Runs the bring-up application, loads the DXE driver, and runs the GOP test. |
| USB-to-JTAG cable | No for the normal test | Needed only if the FPGA is not already configured before motherboard PCI enumeration, or for later ILA debugging. |
| USB-to-TTL serial adapter | No | Optional only for capturing a serial firmware console when the normal display console is inconvenient. |
| USB keyboard | Usually | Enters firmware setup and types Shell commands. |
| FPGA HDMI monitor | Yes | Shows the color bars and GOP output. |
| Motherboard/iGPU console display | Strongly recommended | Shows the UEFI Shell text before the FPGA GOP exists. Two monitors, or one monitor with two switchable inputs, is ideal. |

The simplest arrangement is therefore one ordinary FAT32 USB flash drive, a
keyboard, the normal motherboard display, and a second HDMI input connected to
the FPGA card.

## Important prerequisite: FPGA configuration timing

The FPGA must contain the current display-controller bitstream before the
motherboard enumerates PCIe. Use one of these methods:

1. boot the FPGA from its SPI flash before or during host power-on; or
2. for a temporary test, program the FPGA through JTAG while Linux is running,
   then warm-reboot the host without removing power from the FPGA.

The second method depends on the board retaining its configuration across a
warm reboot. The SPI path is the correct cold-boot solution.

Do not program a different bitstream after UEFI has already enumerated PCIe.
If the Shell application cannot find `10ee:7024`, fix FPGA configuration or
PCIe enumeration before debugging the GOP code.

## Step 1: verify the host-side files

From the repository root in Linux:

```bash
python3 scripts/test_uefi_contract.py
python3 scripts/check_fpga_hardware_contract.py --require-ddr-bypass
sha256sum -c build/gop/artifacts/SHA256SUMS
file build/gop/artifacts/*.efi
```

Expected results:

- `UEFI_CONTRACT_PASS`;
- `hardware contract: PASS`;
- all three hashes report `OK`; and
- the files are x86-64 EFI applications or an x86-64 EFI boot-service driver.

If the artifacts are absent, rebuild them first:

```bash
scripts/build_uefi.sh --check
scripts/build_uefi.sh
```

The host needs `git`, GCC/build tools, Python 3, IASL, and NASM in `PATH`.

## Step 2: prepare an ordinary FAT32 USB drive

Use an existing FAT32 partition when possible. Identifying or formatting the
wrong block device can destroy data, so inspect the device before changing it:

```bash
lsblk -o NAME,SIZE,FSTYPE,LABEL,MOUNTPOINTS,MODEL,TRAN
```

The USB partition should show `vfat` or `fat32`. If it is already mounted, note
its mount path. Otherwise mount the identified partition; replace `/dev/sdX1`
with the real USB partition:

```bash
udisksctl mount -b /dev/sdX1
```

Set `USB_MOUNT` to the path reported by `udisksctl`. This example assumes a
volume labelled `UEFI`:

```bash
export USB_MOUNT=/media/alpk/UEFI
test -d "$USB_MOUNT"
mkdir -p "$USB_MOUNT/EFI/SimpleDisplay"
cp build/gop/artifacts/SimpleDisplayBringup.efi "$USB_MOUNT/EFI/SimpleDisplay/"
cp build/gop/artifacts/SimpleDisplayGopDxe.efi "$USB_MOUNT/EFI/SimpleDisplay/"
cp build/gop/artifacts/SimpleDisplayGopTest.efi "$USB_MOUNT/EFI/SimpleDisplay/"
cp build/gop/artifacts/SHA256SUMS "$USB_MOUNT/EFI/SimpleDisplay/"
```

Verify the files on the USB itself:

```bash
cd "$USB_MOUNT/EFI/SimpleDisplay"
sha256sum -c SHA256SUMS
sync
```

Do not unmount the drive until the optional Shell step below is complete.

## Step 3: provide the UEFI Shell

### Case A: the motherboard has a built-in UEFI Shell

No additional file is needed. Use the firmware setup or boot menu to start its
X64 UEFI Shell. The Simple Display files remain under:

```text
EFI\SimpleDisplay\
```

### Case B: the motherboard has no built-in UEFI Shell

Place an X64 EDK II Shell application at the removable-media fallback path:

```text
EFI\BOOT\BOOTX64.EFI
```

Prefer a Shell built from the same pinned EDK II source used by this project.
After `scripts/build_uefi.sh` has created `build/gop/edk2`, build the Shell:

```bash
export WORKSPACE="$PWD/build/gop/edk2"
export PACKAGES_PATH="$WORKSPACE"
export EDK_TOOLS_PATH="$WORKSPACE/BaseTools"
export CONF_PATH="$WORKSPACE/Conf"
export PYTHON_COMMAND=python3
source "$WORKSPACE/edksetup.sh"
build -a X64 -t GCC -b RELEASE \
  -p ShellPkg/ShellPkg.dsc \
  -m ShellPkg/Application/Shell/Shell.inf
```

Copy the standard full Shell image to the fallback boot path:

```bash
mkdir -p "$USB_MOUNT/EFI/BOOT"
cp "$WORKSPACE/Build/Shell/RELEASE_GCC/X64/Shell_7C04A583-9E3E-4f1c-AD65-E05268D0B4D1.efi" \
  "$USB_MOUNT/EFI/BOOT/BOOTX64.EFI"
file "$USB_MOUNT/EFI/BOOT/BOOTX64.EFI"
sync
```

This exact ShellPkg build was host-validated with the project's pinned
`edk2-stable202605` checkout.

Unmount the USB cleanly before removing it:

```bash
cd /home/alpk/codex_workspaces/Simple-Display-Controller
udisksctl unmount -b /dev/sdX1
```

Again, replace `/dev/sdX1` with the device identified using `lsblk`.

## Step 4: configure the motherboard firmware

For the first test:

1. disable Secure Boot because these development EFI images are unsigned;
2. use UEFI boot mode, not Legacy/CSM mode;
3. disable Fast Boot so PCIe and USB initialization are not skipped;
4. ensure the FPGA is configured before PCI enumeration;
5. connect the normal firmware console display and the FPGA HDMI monitor; and
6. boot either the built-in Shell or the USB `UEFI` entry.

Do not select a normal Linux boot entry yet.

## Step 5: locate the USB in UEFI Shell

At the Shell prompt:

```text
map -r
fs0:
ls
```

The USB might be `fs1:`, `fs2:`, or another mapping. Try each mapped `fsN:`
until this directory exists:

```text
cd EFI\SimpleDisplay
ls
```

You should see:

```text
SimpleDisplayBringup.efi
SimpleDisplayGopDxe.efi
SimpleDisplayGopTest.efi
SHA256SUMS
```

## Step 6: run the non-binding hardware bring-up application

Run only the bring-up application first:

```text
SimpleDisplayBringup.efi
```

Required checkpoints, in order:

```text
PCI_MATCH
BAR2_OK
SCRATCH_RESTORE_OK
HDMI_I2C_OK
CLOCK_LOCK_OK
VDMA_MM2S_OK
VTC_OK
FRAME_FILL_OK
BRINGUP_PASS mode=1280x720@60
```

Acceptance requires both:

1. the final `BRINGUP_PASS`; and
2. stable, correctly coloured bars on the FPGA HDMI monitor.

The application intentionally leaves PCI memory decoding and scanout active.
Stop here if any checkpoint fails, if `BRINGUP_FAIL` appears, or if the output
is absent or unstable. Record the complete text and a monitor photo before
rebooting.

## Step 7: load and connect the GOP driver

Only continue after Step 6 passes visibly:

```text
load -nc SimpleDisplayGopDxe.efi
drivers >a evidence\drivers-loaded.txt
devices >a evidence\devices-before-connect.txt
```

Run `load -nc` exactly once. From those two captures, identify the current
SimpleDisplay driver handle by its image path and the current FPGA controller
handle by the PCI path that corresponds to the fresh `10ee:7024` capture.
Handles are boot-local. Then substitute them below; do not use `-r`:

```text
connect <fpga-controller-handle> <simple-display-driver-handle> >a evidence\connect-targeted.txt
drivers >a evidence\drivers-bound.txt
devices >a evidence\devices-after-connect.txt
dh -p GraphicsOutput >a evidence\graphics-output-handles.txt
```

`dh -p GraphicsOutput` may list the motherboard GPU as well. The new Simple
Display child is distinguished by the driver having one mode, a framebuffer
inside BAR2, and empty EDID protocols. The test application performs those
checks automatically.

Loading with `-nc` and the targeted non-recursive connection must not change
the iGPU console or the visible FPGA frame. Stop and warm-reset if either one
changes. Plain `load` is not suitable for this gate because the TianoCore Shell
follows it with a recursive connect-all operation; a Graphics Console consumer
may then call GOP `SetMode()`. The first permitted visible GOP change in this
runbook occurs when the test explicitly exercises `SetMode()`.

## Step 8: run the GOP protocol and BLT test

```text
SimpleDisplayGopTest.efi
```

Required output:

```text
EDID_EMPTY_PASS
MODE_INFO 0 1280x720
MODE_TEST_PASS
BLT_BUFFER_TRANSFER_PASS
BLT_OVERLAP_PASS
BLT_INVALID_REQUEST_PASS
GOP_TEST_PASS
```

The screen is cleared to black by `SetMode()` and the test writes small test
rectangles near the upper-left area. A mostly black display during this step is
therefore expected; the printed pass lines and readback comparisons are the
primary BLT evidence.

If the application reports `Simple Display GOP child not found`, inspect the
output from `load`, `drivers`, `devices`, and `dh -p GraphicsOutput`. Do not
change the mode table to work around a driver-binding failure.

## Step 9: finish safely and preserve evidence

After the test, take photos of both the Shell output and FPGA monitor. Then
warm-reboot from the Shell:

```text
reset -w
```

For the first evidence bundle, record:

- motherboard and firmware version;
- whether the Shell was built-in or USB-loaded;
- whether the FPGA came from SPI or a retained JTAG configuration;
- current bitstream SHA-256;
- all bring-up checkpoints and hardware status lines;
- `drivers`, `devices`, and `dh -p GraphicsOutput` output;
- all GOP test pass/fail lines; and
- monitor observations and photos.

Store the curated results under:

```text
firmware/uefi/test_logs/YYYY-MM-DD_shell_bringup/
```

## Current cold-state automatic-binding repair gate

Use this sequence for the 2026-08-04 Driver Binding repair. It intentionally
replaces Steps 6-9 above: **do not run `SimpleDisplayBringup.efi`**, because it
enables PCI memory decoding and would mask the original cold-state failure.

On Linux, verify and stage the current build on the FAT USB volume. If the
volume is mounted read-only, unmount and remount it read-write before copying;
do not reformat it or replace the preserved Shell files:

```bash
scripts/test_uefi_contract.py
python3 scripts/test_uefi_shell_log.py
python3 scripts/test_uefi_cold_bind_validation.py
python3 scripts/test_uefi_auto_gop_validation.py
scripts/build_uefi.sh
cd build/gop/artifacts
sha256sum -c SHA256SUMS
cd ../../..
export USB_MOUNT=/media/alpk/ALP
install -m 0644 build/gop/artifacts/SimpleDisplayGopDxe.efi \
  "$USB_MOUNT/EFI/SimpleDisplay/"
install -m 0644 build/gop/artifacts/SimpleDisplayGopTest.efi \
  "$USB_MOUNT/EFI/SimpleDisplay/"
install -m 0644 build/gop/artifacts/SHA256SUMS \
  "$USB_MOUNT/EFI/SimpleDisplay/"
cd "$USB_MOUNT/EFI/SimpleDisplay"
sha256sum -c SHA256SUMS
sync
```

The required hashes for this repair build are:

```text
76183ab80b31a3a9f519b97f5a24ef26bdb917856595e34bfd97b7c8c2a74117  SimpleDisplayGopDxe.efi
4c21b7fc8c6c0baa1bf0c09236e645087386a52d6ece2f3639579f602722a61a  SimpleDisplayGopTest.efi
```

The superseded `4acd6244...` build proved exact post-disconnect restoration to
Command `0000`, but both fresh reconnect attempts returned `Not Found`. The
current build additionally verifies raw memory decode after the abstract
enable operation and writes/reads back the Command memory bit when firmware's
cached attribute state does not match hardware. The sequence below has now
passed on the board without running `SimpleDisplayBringup.efi`. The sealed
record is
`firmware/uefi/test_logs/2026-08-04_cold_bind_pass/COLD_GOP_BIND_VALIDATION_PASS.json`.

Cold boot to the Shell, map the USB, enter `EFI\SimpleDisplay`, and rediscover
the boot-local FPGA controller handle. Capture its detailed dump before
loading the fixed driver; its configuration header must show PCI Command
`0000`:

```text
map -r
fsN:
cd EFI\SimpleDisplay
devices >a cold_devices_before.log
dh -v <fpga-controller-handle> >a cold_pre_device.log
load -nc SimpleDisplayGopDxe.efi
drivers >a cold_loaded_driver.log
connect <fpga-controller-handle> <simple-display-driver-handle> >a cold_connect.log
dh -p GraphicsOutput >a cold_graphics.log
SimpleDisplayGopTest.efi >a cold_gop.log
```

Require two GraphicsOutput handles and `GOP_TEST_PASS`. Confirm separately that
the FPGA monitor shows the test graphics and the iGPU remains the Shell console.
Then exercise cleanup and restart with the same boot-local handles:

```text
disconnect <fpga-controller-handle> <simple-display-driver-handle> >a cold_disconnect.log
dh -v <fpga-controller-handle> >a cold_post_disconnect_device.log
dh -p GraphicsOutput >a cold_post_disconnect_graphics.log
connect <fpga-controller-handle> <simple-display-driver-handle> >a cold_reconnect.log
dh -v <fpga-controller-handle> >a cold_reconnect_device.log
dh -p GraphicsOutput >a cold_reconnect_graphics.log
SimpleDisplayGopTest.efi >a cold_reconnect_gop.log
```

The post-disconnect device dump must again show Command `0000`, its graphics
inventory must contain only the platform GOP, and the reconnect device dump
must have Memory Space Enable set. The reconnect capture must again contain
two GOP handles and a second exact `MODE_INFO 0 1920x1080 format=3 ppsl=1920`
plus `GOP_TEST_PASS`. After Linux resumes, seal the untouched logs and exact
tested binaries:

```bash
python3 scripts/record_uefi_cold_bind_validation.py \
  --pre-device-log "$USB_MOUNT/EFI/SimpleDisplay/cold_pre_device.log" \
  --loaded-driver-log "$USB_MOUNT/EFI/SimpleDisplay/cold_loaded_driver.log" \
  --connect-log "$USB_MOUNT/EFI/SimpleDisplay/cold_connect.log" \
  --graphics-log "$USB_MOUNT/EFI/SimpleDisplay/cold_graphics.log" \
  --gop-log "$USB_MOUNT/EFI/SimpleDisplay/cold_gop.log" \
  --disconnect-log "$USB_MOUNT/EFI/SimpleDisplay/cold_disconnect.log" \
  --post-disconnect-device-log "$USB_MOUNT/EFI/SimpleDisplay/cold_post_disconnect_device.log" \
  --post-disconnect-graphics-log "$USB_MOUNT/EFI/SimpleDisplay/cold_post_disconnect_graphics.log" \
  --reconnect-log "$USB_MOUNT/EFI/SimpleDisplay/cold_reconnect.log" \
  --reconnect-device-log "$USB_MOUNT/EFI/SimpleDisplay/cold_reconnect_device.log" \
  --reconnect-graphics-log "$USB_MOUNT/EFI/SimpleDisplay/cold_reconnect_graphics.log" \
  --reconnect-gop-log "$USB_MOUNT/EFI/SimpleDisplay/cold_reconnect_gop.log" \
  --driver build/gop/artifacts/SimpleDisplayGopDxe.efi \
  --gop-test build/gop/artifacts/SimpleDisplayGopTest.efi \
  --expected-width 1920 --expected-height 1080 \
  --bringup-not-run --visible-output-confirmed --igpu-shell-preserved
python3 scripts/check_uefi_cold_bind_validation.py
```

This exact run produced `COLD_GOP_BIND_VALIDATION_PASS`: it started and ended
with Command `0000`, created separate iGPU and FPGA GOP handles, completed
`GOP_TEST_PASS`, disconnected, and reconnected successfully. The user also
confirmed the visible FPGA rectangle and undisturbed iGPU screen. The packager
then embedded the exact driver into
`vivado_project/rom/simple_display_gop_option_rom.bin`, SHA-256
`eeacaa66703abb7fa10c2677cd9738fcb70b0dfe531c07a65bb8a52c0c11df7e`.

The matching isolated `PCIe_GOP_ROM_32K_AUTOBIND1` build and independent audit
pass with WNS `-0.780 ns` against the accepted `-2.500 ns` minimum, WHS
`+0.017 ns`, zero blocking DRC errors, zero unrouted/partially routed nets, and
lane order `5,4,6,7`. The exact deployment hashes are `e08fd656...` (`.bit`),
`17c2fbf6...` (SPI `.bin`), and `fc18aae4...` (`.ltx`). SPI erase,
programming, and verification now pass, and the matching application
bitstream/probes were restored to SRAM. A complete shutdown/power-on is still
required to prove configuration from flash; automatic Option-ROM binding also
remains a separate gate.

After booting that persistent build from a complete power-off, the final
cold-boot capture must not run `load` or `connect`. Capture the ROM-loaded
driver, associated FPGA device, existing GOP handles, and GOP test directly:

```text
dh -v <option-rom-driver-handle> >a auto_driver.log
dh -v <fpga-controller-handle> >a auto_device.log
dh -p GraphicsOutput >a auto_graphics.log
SimpleDisplayGopTest.efi >a auto_gop.log
```

Seal that separate automatic gate with:

```bash
python3 scripts/record_uefi_auto_gop_validation.py \
  --driver-log "$USB_MOUNT/EFI/SimpleDisplay/auto_driver.log" \
  --device-log "$USB_MOUNT/EFI/SimpleDisplay/auto_device.log" \
  --graphics-log "$USB_MOUNT/EFI/SimpleDisplay/auto_graphics.log" \
  --gop-log "$USB_MOUNT/EFI/SimpleDisplay/auto_gop.log" \
  --driver firmware/uefi/test_logs/2026-08-04_cold_bind_pass/tested_payload/SimpleDisplayGopDxe.efi \
  --gop-test firmware/uefi/test_logs/2026-08-04_cold_bind_pass/tested_payload/SimpleDisplayGopTest.efi \
  --option-rom vivado_project/rom/simple_display_gop_option_rom.bin \
  --no-manual-load --no-manual-connect \
  --visible-output-confirmed --igpu-shell-preserved
python3 scripts/check_uefi_auto_gop_validation.py
```

## Quick failure guide

| Symptom | First action |
|---|---|
| USB has no boot entry | Confirm FAT32, UEFI mode, and `EFI\BOOT\BOOTX64.EFI`; check that Secure Boot is disabled. |
| `Security Violation` or image refused | Disable Secure Boot; confirm the images are X64 and their hashes match. |
| No matching `10ee:7024` | Ensure the FPGA was configured before PCI enumeration; verify the current bitstream and try a warm reboot. |
| BAR2 absent or smaller than 64 MiB | Stop. Check firmware PCI resource allocation and confirm the current hardware export. |
| Scratch test fails | Stop. The DDR bypass contract is not safe for framebuffer writes. |
| `BRINGUP_PASS` but no visible bars | Record all clock, VDMA, VTC, GPIO, and I2C lines; check the FPGA HDMI cable/input before using JTAG/ILA. |
| GOP child not found | Inspect driver load and binding with `drivers`, `devices`, and `dh`; the hardware application passing does not prove Driver Binding succeeded. |
| BLT or guard-pixel failure | Preserve the exact failing line and reboot; do not proceed to additional modes. |

## Stop/go boundary

A successful cold USB run proves the repaired Shell-loaded GOP binding path.
It does not yet prove automatic binding from the rebuilt Option ROM,
firmware-console selection,
Linux framebuffer handoff, or cold-boot reliability. Promote another fixed
mode only after this complete sequence passes for `1280x720@60`.
