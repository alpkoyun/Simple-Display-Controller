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
load SimpleDisplayGopDxe.efi
connect -r
drivers
devices
dh -p GraphicsOutput
```

`dh -p GraphicsOutput` may list the motherboard GPU as well. The new Simple
Display child is distinguished by the driver having one mode, a framebuffer
inside BAR2, and empty EDID protocols. The test application performs those
checks automatically.

Loading or connecting the driver must not change the visible frame. The first
visible GOP change occurs when `SetMode()` is exercised by the test.

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

A successful run proves manual UEFI hardware access and a Shell-loaded GOP. It
does not yet prove automatic Option ROM execution, firmware-console selection,
Linux framebuffer handoff, or cold-boot reliability. Promote another fixed
mode only after this complete sequence passes for `1280x720@60`.
