# First Firmware Contract

This is the fixed contract for the Shell bring-up application and first
fixed-mode GOP driver. It describes the current validated FPGA image; it is not
a proposal for the later Expansion ROM hardware.

This contract covers direct BAR scanout only. It deliberately does not depend
on PCI bus mastering or XDMA H2C; the latter is currently failing before AXI
stream `TVALID` and is tracked as a native-Linux handoff issue.

## PCI match and capabilities

| Field | Required value or rule |
|---|---|
| Vendor ID | `0x10ee` |
| Device ID | `0x7024` |
| Base class | Display controller, `0x03` |
| Architecture | X64 UEFI |
| BAR0 | XDMA configuration, 64 KiB; not needed for ordinary GOP pixels |
| BAR2 | At least 64 MiB and memory-decodable |
| PCI bus mastering | Not required for the direct-BAR GOP path |
| Expansion ROM | Not required for Shell loading; currently disabled |

During manual development, PCI identity plus BAR/resource validation is
sufficient on the known board. Before Option ROM deployment, add a project
magic/version/feature register so the driver cannot bind to an incompatible
Xilinx design sharing the same device ID.

The current hardware export is maintained by hand while the FPGA design is
still changing. Firmware development targets that validated export. The
tracked Vivado recreate/export flow is synchronized after the hardware is
finished and is a freeze/release requirement rather than a blocker for manual
UEFI testing.

## BAR2 subwindows

All addresses below are BAR-relative. Firmware must discover BAR2's assigned
physical base; it must never embed the host address observed on one boot.

| Offset | Size | Function |
|---:|---:|---|
| `0x00000000` | 64 KiB | Pixel unpack |
| `0x00010000` | 64 KiB | Video timing controller |
| `0x00020000` | 64 KiB | HDMI AXI IIC |
| `0x00030000` | 64 KiB | AXI UART Lite, not required by GOP v0 |
| `0x00040000` | 64 KiB | AXI VDMA control |
| `0x00050000` | 64 KiB | Color conversion |
| `0x00060000` | 64 KiB | Video clock wizard |
| `0x00070000` | 64 KiB | Video-lock GPIO |
| `0x02000000` | `0x01fa7000` maximum used | Four-frame DDR ring; GOP v0 uses frame 0 |
| `0x03fff000` | 4 KiB | Non-destructive scratch page |

The hardware emits bypass AXI addresses as:

```text
0x3c000000 OR BAR2_offset
```

The translation is aligned to the 64 MiB aperture, so offsets through
`0x03ffffff` are uniquely representable.

## Fixed mode policy

The HDMI device used on this board has no usable, publicly documented EDID/DDC
read interface in this setup. Firmware must not fabricate EDID bytes or claim
monitor-specific discovery. The HDMI child installs:

```text
EFI_EDID_DISCOVERED_PROTOCOL: SizeOfEdid=0, Edid=NULL
EFI_EDID_ACTIVE_PROTOCOL:     SizeOfEdid=0, Edid=NULL
```

GOP modes come from a fixed table whose timings are shared with or checked
against `fpga_drm`. The initial accepted implementation advertised only
`1280x720@60`; the current replacement candidate promotes `1920x1080@60` to
mode 0 while keeping `MaxMode=1`. Conservative `800x600@60` and `640x480@60`
timings remain in the candidate table, but they and the remaining
Linux-whitelist resolutions are not advertised without visible UEFI
validation.

Because `EFI_GRAPHICS_OUTPUT_MODE_INFORMATION` has no refresh-rate field, do
not publish both 30 Hz and 60 Hz entries for the same resolution unless a
separate documented policy makes the timing choice unambiguous.

## Current default timing

The replacement firmware mode is `1920x1080@60`; its deployment remains gated
on the same visible UEFI and persistent cold-boot evidence as the accepted
1280x720 baseline:

| Item | Value |
|---|---:|
| Pixel clock | 148,500 kHz |
| Horizontal active | 1920 |
| Horizontal sync start/end/total | 2008 / 2052 / 2200 |
| Vertical active | 1080 |
| Vertical sync start/end/total | 1084 / 1089 / 1125 |
| Sync polarity | Positive HSync, positive VSync |
| Bytes per pixel | 4 |
| Bytes per line | 7680 (`0x1e00`) |
| Active frame bytes | 8,294,400 (`0x007e9000`) |
| GOP pixel format | `PixelBltOnly` |
| Physical storage layout | XRGB8888-compatible 32-bit pixels |
| Pixels per scan line | 1920 |
| Clock-wizard configuration | `CFG0=0x007d250a`, `CFG2=0x00000005` |

On little-endian x86, the GOP pixel bytes are blue, green, red, reserved. This
matches the project's DRM `XRGB8888` memory layout.

## Framebuffer and VDMA views

Firmware publishes:

```text
FrameBufferBase = 0
FrameBufferSize = 0
```

The driver retains BAR2+`0x02000000` internally and reaches it only through
explicit 32-bit PCI I/O operations in `GOP.Blt()`.

VDMA uses the same numeric DDR address as the bypass master:

```text
MM2S frame address = 0x3e000000
stride             = 5120
horizontal size    = 5120
vertical size      = 720
frame count        = 1
parked frame       = 0
```

Do not enable S2MM in GOP v0; CPU writes already populate the scanout frame,
and an active S2MM channel could overwrite memory.

## Required hardware initialization

Keep the firmware sequence behaviorally aligned with `fpga_drm`:

1. Confirm video-lock/status GPIO is readable.
2. Configure pixel unpack for the project's 32-bit XRGB input.
3. Configure color conversion as identity.
4. Program the HDMI transmitter through AXI IIC with bounded waits.
5. Reset/program the clock wizard and wait for lock with a timeout.
6. Reset and configure only VDMA MM2S for one frame at `0x3e000000`.
7. Program VTC timing and polarity.
8. Clear or pattern-fill the active framebuffer through BAR2.
9. Start MM2S/VTC in the order proven by the Linux driver.
10. Read back status and fail rather than spin forever.

Port the register algorithms, not Linux implementation details: UEFI code must
not copy kernel sleeps, allocation APIs, logging, or MMIO helpers.

## GOP v0 behavior

`QueryMode()`:

- accepts every index in the fixed, validated GOP mode table;
- allocates and returns a fresh mode-information structure; and
- reports version zero, the selected resolution and pixel format, and the
  corresponding pixels per scan line.

`SetMode()`:

- rejects unsupported indices;
- validates BAR and frame bounds again;
- performs bounded pipeline initialization;
- updates GOP mode state only after hardware succeeds; and
- clears the full visible frame to black.

`Blt()`:

- supports VideoFill, VideoToBltBuffer, BufferToVideo, and VideoToVideo;
- checks every coordinate, delta, multiplication, and addition for overflow;
- supports overlapping VideoToVideo copies correctly; and
- uses `FrameBufferBltLib` for the established pixel/rectangle behavior.

Driver Binding `Start()` discovers resources, creates the HDMI child, and
installs Device Path, GOP, and empty EDID protocols without making a
user-visible output change. Pipeline programming and the clear-to-black
operation occur only when `SetMode()` is called. The standalone Shell bring-up
application may program a visible test mode because that is its explicit
purpose.

## Exit and ownership behavior

- Returning from the Shell bring-up application should leave scanout active.
- `ExitBootServices()` must leave the selected GOP frame and pipeline active.
- Driver `Stop()` during an explicit disconnect must uninstall protocols and
  release resources; whether it blanks the screen should be a deliberate
  disconnect policy, not an accidental side effect.
- The native Linux driver may later expand frame store 0 into its four-frame
  ring at `0x3e000000-0x3ffa6fff`; firmware must stop accessing the GOP frame
  after native ownership transfers.
- That native takeover is not yet an accepted path on the current export:
  firmware development may proceed through manual GOP validation, but final
  handoff testing waits for H2C recovery or a validated direct-BAR Linux
  backend.
