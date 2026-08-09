# Hardware Interface

## PCI Function

The firmware matches one PCI display controller:

| Field | Required value |
|---|---:|
| Vendor ID | `0x10ee` |
| Device ID | `0x7024` |
| Base class | `0x03` (display) |
| Current full class code | `0x038000` |
| BAR used by firmware | BAR2 |
| Minimum BAR2 size | 64 MiB |
| Expansion ROM aperture | 32 KiB in the accepted design |

`SimpleDisplayHwMatchPci()` intentionally stops at PCI configuration space.
`SimpleDisplayHwInitializeContext()` is the stronger match: after memory decode
is enabled, it discovers BAR2 with `GetBarAttributes()` and validates the SDC1
identity registers.

## BAR2 Address Map

BAR2 is a 64-bit prefetchable 64 MiB window translated to FPGA AXI base
`0x3c000000`.

| Function | FPGA AXI address | BAR2 offset |
|---|---:|---:|
| Pixel unpack | `0x3c000000` | `0x00000000` |
| VTC | `0x3c010000` | `0x00010000` |
| AXI IIC | `0x3c020000` | `0x00020000` |
| AXI UART Lite | `0x3c030000` | `0x00030000` |
| AXI VDMA | `0x3c040000` | `0x00040000` |
| Color conversion | `0x3c050000` | `0x00050000` |
| Video clock wizard | `0x3c060000` | `0x00060000` |
| Video lock GPIO | `0x3c070000` | `0x00070000` |
| SDC1 identity | `0x3c080000` | `0x00080000` |
| Shared DDR framebuffer | `0x3e000000` | `0x02000000` |
| Last scratch page | `0x3ffff000` | `0x03fff000` |

Each low control region reserves 64 KiB. The shared DDR window spans the upper
32 MiB of BAR2, `0x3e000000-0x3fffffff`. The accepted GOP frame begins at the
first byte of that DDR window.

## SDC1 Identity ABI

SDC1 is a read-only hardware identity block that prevents a plausible PCI/BAR
layout from being mistaken for compatible display hardware.

| Offset | Value | Meaning |
|---|---:|---|
| `0x00` | `0x31434453` | ASCII-style `SDC1` magic in little-endian word form |
| `0x04` | `0x00010000` | ABI version 1.0 |
| `0x08` | `0x00000003` | direct-DDR and Option-ROM features |
| `0x0c` | `32768` | current Expansion ROM aperture in bytes |

The firmware library accepts either 4096 or 32768 bytes because the same
driver was manually validated against the historical Shell-stage hardware.
The tracked production hardware contract, packaging flow, and current ROM all
require 32768 bytes.

## Framebuffer Contract

| Item | Value |
|---|---:|
| Host-visible start | BAR2 `+0x02000000` |
| FPGA VDMA start | `0x3e000000` |
| Storage | 32-bit XRGB8888-compatible pixels |
| Bytes per pixel | 4 |
| Current dimensions | `1920 x 1080` |
| Current stride | `7680` bytes |
| Current frame bytes | `8,294,400` / `0x007e9000` |
| Maximum frame bytes | `0x007e9000` |

The maximum frame ends before the scratch page. The hardware library validates
both its compile-time maximum and actual BAR containment before programming a
mode or writing a diagnostic frame.

## PCI I/O Access Rule

Every firmware access to control registers, SDC1, scratch storage, and GOP
pixels uses `EFI_PCI_IO_PROTOCOL.Mem.Read/Write` with
`EfiPciIoWidthUint32`. Ordinary CPU dereferences of the BAR are not equivalent
on this system: earlier testing produced partial-width or missing pixel writes.

This rule is why GOP exposes `PixelBltOnly` and zero framebuffer fields. It is
also why video-to-buffer readback and overlapping copies are implemented
through PCI I/O rather than `CopyMem()` on a mapped BAR address.

## Mode Timing

The one public GOP timing is:

| Parameter | Value |
|---|---:|
| Pixel clock | 148500 kHz |
| Horizontal active | 1920 |
| Horizontal sync start/end | 2008 / 2052 |
| Horizontal total | 2200 |
| Vertical active | 1080 |
| Vertical sync start/end | 1084 / 1089 |
| Vertical total | 1125 |
| H/V sync polarity | positive / positive |

The clock-wizard dynamic values are part of the mode table and are written by
`ConfigureClock()`. The library waits for the dynamic-load request to clear
and for the clock status to report locked.

## Pipeline Programming

`SimpleDisplayHwProgramMode()` programs blocks in this order:

1. GPIO direction/status access.
2. Pixel unpack mode `1` with readback verification.
3. Identity 3x3 color matrix with zero bias and diagonal readback checks.
4. AXI IIC reset/enable and two HDMI transmitter register writes.
5. Pixel-clock dynamic configuration and lock wait.
6. VDMA S2MM and MM2S reset; one MM2S frame store is configured.
7. VTC timing, polarity, software source selection, and generator enable.
8. VTC error readback must be zero.

The GOP path writes pixels through BAR2. It does not use VDMA S2MM or XDMA H2C
to upload the frame. VDMA MM2S reads the shared DDR address continuously and
feeds the video pipeline.

## VDMA Contract

The firmware resets both channels to leave a defined state, clears MM2S error
and interrupt status, selects one frame store, and programs:

```text
horizontal size = active_width * 4
stride          = active_width * 4
start address   = 0x3e000000
vertical size   = active_height
```

MM2S runs in tail mode with synchronization and internal genlock. A nonzero
VDMA error field after start is a mode-programming failure.

## VTC and Output

The VTC uses software-programmed totals, active dimensions, sync ranges, blank
offsets, and polarities. The path is progressive (`FRAME_ENCODING=0`). The
video clock, VDMA MM2S, VTC, pixel unpack, identity color conversion, and HDMI
transmitter together turn the DDR pixels into the FPGA HDMI/DVI signal.

No firmware EDID/DDC read contract is available, so the mode is selected from
the fixed table rather than monitor discovery.

## Expansion ROM AXI Address

BAR6 traffic is translated to AXI `0x01000000-0x01007fff`, outside BAR2's
`0x3c000000-0x3fffffff` destination range. A dedicated read-only AXI ROM slave
serves 32-bit memory initialization. Writes complete as protocol transactions
but have no storage mutation path.

The ROM and SDC1 endpoints are separate from the original nine-output
display/control interconnect, preserving normal BAR2 decoding.
