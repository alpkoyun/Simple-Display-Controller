# Validation, Evidence, and Failure Isolation

For the exact USB preparation, firmware settings, Shell commands, expected
messages, and first-run stop/go boundary, use the
[UEFI Shell runbook](04_uefi_shell_runbook.md).

## Evidence policy

Every stage should produce a small, dated evidence bundle rather than only a
statement that the screen worked. Store curated text logs and metadata under:

```text
firmware/uefi/test_logs/YYYY-MM-DD_<milestone>/
```

Do not commit full EDK II build trees. Record:

- EDK II commit and compiler version;
- exact build command;
- SHA-256 of `.efi`, and later `.rom`, `.bit`, and `.bin` artifacts;
- PCI identity and BAR descriptors reported by UEFI;
- scratch original/test/restored values;
- clock, VTC, VDMA, I2C, and GPIO status/readback;
- GOP mode information and BLT test results;
- confirmation that both EDID protocols report zero-length data;
- monitor-visible observation and mode;
- firmware console/bootloader selection result;
- Linux early-framebuffer and native-takeover logs; and
- cold/warm boot totals and every failure.

The current Linux raw bypass and H2C-isolation evidence remains in
[`TEST_LOG.md`](../../../Linux_DRM_Driver/tests/TEST_LOG.md#2026-07-16-paired-ila-correction-and-shared-32-mib-ddr-map);
do not duplicate its full kernel/ILA output in each firmware log.

## Stage-specific checks

### Shell bring-up application

Run from a FAT USB path such as:

```text
fs0:\EFI\SimpleDisplay\SimpleDisplayBringup.efi
```

Required printed checkpoints:

```text
PCI_MATCH
BAR2_OK
SCRATCH_RESTORE_OK
HDMI_I2C_OK
CLOCK_LOCK_OK
VDMA_MM2S_OK
VTC_OK
FRAME_FILL_OK
BRINGUP_PASS
```

Acceptance requires both `BRINGUP_PASS` and a stable, visibly correct pattern
on the FPGA HDMI monitor. A printed pass without visible output is not enough.

### Shell-loaded GOP driver

The initial workflow isolates image loading, Driver Binding `Start()`, and GOP
use as three separate steps:

```text
load -nc fs0:\EFI\SimpleDisplay\SimpleDisplayGopDxe.efi
drivers >a fs0:\EFI\SimpleDisplay\evidence\drivers-loaded.txt
devices >a fs0:\EFI\SimpleDisplay\evidence\devices-before-connect.txt
connect <fpga-controller-handle> <simple-display-driver-handle>
drivers >a fs0:\EFI\SimpleDisplay\evidence\drivers-bound.txt
devices >a fs0:\EFI\SimpleDisplay\evidence\devices-after-connect.txt
dh -p GraphicsOutput >a fs0:\EFI\SimpleDisplay\evidence\graphics-output-handles.txt
fs0:\EFI\SimpleDisplay\SimpleDisplayGopTest.efi
```

Record actual controller/child handles from the test machine rather than
hard-coding examples into scripts. Load exactly one driver image and connect
only the specific FPGA controller and SimpleDisplay driver, without `-r`.
Plain `load` performs a recursive connect-all operation in the TianoCore Shell
and can cause a Graphics Console consumer to call `SetMode()` immediately,
which does not isolate Driver Binding `Start()`.

The GOP test application must verify:

- `MaxMode=1` for GOP v0;
- exact mode information and framebuffer bounds;
- full-screen and clipped color fills;
- patterned buffer-to-video copy with nontrivial `Delta`;
- video-to-buffer readback comparison;
- overlapping video-to-video copies in both directions;
- zero-size, out-of-bounds, overflow, null-buffer, and invalid-mode errors; and
- preservation of guard pixels outside each test rectangle.

### Fixed modes and unavailable EDID

- Confirm EDID Discovered and EDID Active are installed on the HDMI child.
- Confirm both report `SizeOfEdid=0` and `Edid=NULL`.
- Confirm the driver performs no EDID/DDC read attempt and fabricates no EDID
  bytes.
- For each exposed mode, record exact timing, framebuffer size, clock lock,
  VDMA status, VTC error status, and visible output.
- Test the fixed fallback modes, unplugged monitor behavior, and HDMI I2C
  initialization timeout.

### Linux handoff

Current status: blocked after the firmware-framebuffer portion until the
native frame-upload path is repaired or replaced.

Record the boot sequence with both kernel and monitor evidence:

```text
GOP frame remains after ExitBootServices
  -> firmware framebuffer owner appears
  -> early kernel graphics appear
  -> fpga_drm removes conflicting aperture
  -> VDMA changes from one GOP frame to four native frames
  -> desktop uploads complete
```

Check for BAR/resource conflicts, duplicate DRM ownership, stale GOP memory,
VDMA errors, and a native-module source-version mismatch.

### Option ROM

Before firmware execution, Linux must read the PCI ROM resource and produce a
byte-for-byte hash match with the packaged `.rom`. Validate all ROM/PCIR fields
with a host-side tool. Then record separately:

- endpoint enumerated;
- ROM BAR present and enabled;
- ROM bytes readable;
- ROM image structurally valid;
- motherboard chose to execute it;
- driver bound; and
- GOP/console appeared.

These are separate gates. A valid readable ROM does not prove that platform
policy executed it.

## Failure isolation ladder

| Symptom | Most likely layer | First evidence to inspect |
|---|---|---|
| UEFI application finds no `10ee:7024` | FPGA SPI configuration or firmware PCI enumeration | UEFI `pci` output and cold-boot timing; do not debug GOP code yet. |
| Device found but BAR2 absent/too small | Firmware PCI allocation or mismatched bitstream | `GetBarAttributes()` output and PCI command register. |
| BAR control reads fail | Memory decoding, BAR selection, or translation mismatch | Pixel/GPIO reads at low BAR2 offsets, then HWH contract. |
| Low registers work but scratch fails | DDR bypass segment or aliasing regression | Scratch addresses and bypass AXI ILA. |
| Scratch works but frame fill fails | Frame size/bounds or large MMIO transfer | First/last pixel readback and exact byte count. |
| Frame writes read back but no HDMI image | Clock, VTC, VDMA MM2S, HDMI I2C, or monitor | Status registers in initialization order; capture video-stream ILA last. |
| Pattern app works but DXE driver will not bind | UEFI Driver Binding/child-handle ownership | `Supported()` status, protocol open information, `drivers`, and `devices`. |
| GOP handle exists but firmware console stays on iGPU | `ConOut`/primary-display policy | `dh -p GraphicsOutput`, console variables, and setup policy; do not change the framebuffer path. |
| GOP works but BLT test corrupts edges | Rectangle, delta, overlap, overflow, or PCI access-width logic | GOP test vectors and guard pixels; require explicit 32-bit PCI I/O. |
| GOP works before boot but Linux hangs | A linear GOP framebuffer lets `simpledrm` issue unsupported ordinary CPU accesses | Require `PixelBltOnly`, zero framebuffer base/size, a normal boot without initcall blacklists, then native `fpga_drm` takeover. |
| `fpga_drm` modeset succeeds but native upload times out | XDMA requester/descriptor fetch before VDMA S2MM | XDMA engine completed-descriptor count, PCI requester/AER status, then H2C ILA `TVALID`/`TREADY`. Current signature is `BUSY`, count zero, `TREADY=1`, `TVALID=0`. |
| Shell driver works but Option ROM does not | ROM packaging, Expansion ROM read path, or firmware policy | ROM hash/headers first, execution policy second. |
| Warm boots pass but cold boots miss the device | FPGA configuration timing | SPI image, compression/config clock, and repeated AC-power results. |

## Milestone matrix

| Milestone | Hardware change? | OS reboot? | Success signal |
|---|---:|---:|---|
| Reproducible X64 EDK II build | No | No | Host gate passed 2026-07-16; three clean-built EFI hashes recorded in `firmware/uefi/README.md`. |
| UEFI PCI/BAR/scratch probe | No | Enter UEFI Shell | `SCRATCH_RESTORE_OK` |
| UEFI visible color bars | No | Enter UEFI Shell | `BRINGUP_PASS` plus monitor observation |
| Fixed-mode GOP and BLT | No | Enter UEFI Shell | GOP handle, empty EDID protocols, and complete test-app pass |
| Expanded fixed mode table | No | Enter UEFI Shell | Per-mode timing and visible-output passes |
| Firmware console/bootloader | Usually no | Warm reboot may help | Visible consumer using FPGA GOP |
| Linux handoff | GOP metadata plus Linux driver ownership | Yes | BLT-only GOP -> no FPGA `simpledrm` binding -> working native `fpga_drm` |
| Expansion ROM/version register | Yes | FPGA/SPI rebuild and cold boot | ROM hash match and automatic GOP |
| Reliability release gate | No further design change | Many cold/warm boots | Complete boot matrix without unexplained failure |

## Stop/go rules

- Do not start DXE Driver Binding work until the Shell application produces a
  visible frame.
- Do not diagnose console policy until a GOP handle is independently visible.
- Do not modify Linux handoff until GOP survives to `ExitBootServices()`.
- Do not call native takeover complete until a frame-upload path produces
  visible native pixels; successful direct GOP scanout alone is insufficient.
- Do not embed the driver in FPGA ROM until manual Shell loading is repeatable.
- Do not call the deployment reliable based on one successful reboot.
