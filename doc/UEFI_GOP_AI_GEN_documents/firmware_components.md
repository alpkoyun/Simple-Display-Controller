# Firmware Components

## Package Layout

The EDK II package is rooted at `firmware/uefi/SimpleDisplayPkg` and produces
three X64 EFI images from one shared hardware library.

| Component | Output | Role |
|---|---|---|
| `SimpleDisplayGopDxe` | `SimpleDisplayGopDxe.efi` | UEFI Driver Model GOP driver packaged into the Option ROM |
| `SimpleDisplayBringup` | `SimpleDisplayBringup.efi` | Explicit non-binding hardware diagnostic for Shell use |
| `SimpleDisplayGopTest` | `SimpleDisplayGopTest.efi` | Protocol-level GOP and BLT acceptance test |
| `SimpleDisplayHwLib` | Statically linked library | Shared PCI/BAR/SDC1 validation and display-pipeline programming |
| `SimpleDisplayHardware.h` | Shared header | PCI identity, BAR layout, SDC1 ABI, mode table limits, and frame bounds |

`SimpleDisplayPkg.dsc` selects X64, GCC, DEBUG/RELEASE builds and the EDK II
library instances. The component INF files declare their protocols, package
dependencies, entry points, and module types.

## GOP DXE Driver

The driver contains four major areas:

1. GOP methods: `SimpleDisplayQueryMode()`, `SimpleDisplaySetMode()`, and
   `SimpleDisplayBlt()`.
2. PCI row helpers that perform framebuffer reads and writes with
   `EfiPciIoWidthUint32`.
3. Driver Binding methods: `Supported()`, `Start()`, and `Stop()`.
4. Resource and PCI-state cleanup helpers.

`SimpleDisplayGopEntryPoint()` installs the Driver Binding protocol. It does
not search for hardware, program a mode, or write pixels. Those operations are
driven later through UEFI Driver Model calls.

The per-controller `SIMPLE_DISPLAY_PRIVATE` object retains:

- parent and child handles;
- parent `EFI_PCI_IO_PROTOCOL` and device path;
- exact original PCI attributes and raw Command register;
- flags identifying which resources and protocol opens are live;
- the validated hardware context;
- child device path, GOP object, mode state, and mode information; and
- empty EDID Discovered and Active protocol objects.

The private object is installed on the parent under `gEfiCallerIdGuid`. This
lets repeated `Supported()`/`Start()` calls recognize a parent-only start, an
already-created child, and cleanup state that may need a later `Stop()` retry.

## Shared Hardware Library

`SimpleDisplayHwLib` is the only firmware component that knows the complete
video-register sequence. Its public interface provides:

| Function | Responsibility |
|---|---|
| `SimpleDisplayHwMatchPci()` | Match vendor, device, and base display class without BAR access |
| `SimpleDisplayHwInitializeContext()` | Discover BAR2 with `GetBarAttributes()` and validate SDC1 |
| `SimpleDisplayHwScratchTest()` | Save, write, read, and exactly restore four scratch words |
| `SimpleDisplayHwValidateModeBounds()` | Check active dimensions, frame size, and BAR containment |
| `SimpleDisplayHwProgramMode()` | Configure GPIO, unpack, color, HDMI I2C, clock, VDMA, and VTC |
| `SimpleDisplayHwFillColorBars()` | Write a diagnostic pattern through 32-bit PCI I/O |
| `SimpleDisplayHwReadStatus()` | Capture key GPIO, clock, VDMA, VTC, IIC, and unpack registers |
| `SimpleDisplayHwFrameBytes()` | Calculate active XRGB8888 frame size |

The library contains six source-level mode definitions so firmware and Linux
can share validated timing values. Only the first entry is exported through
GOP because `SIMPLE_DISPLAY_GOP_MODE_COUNT` is one.

## Bring-Up Application

`SimpleDisplayBringup.efi` is deliberately non-binding. It locates PCI I/O
handles, matches `10ee:7024`, prints PCI/BAR information, enables PCI memory
access, validates SDC1, performs the restorative scratch test, programs mode 0,
writes color bars, and prints status readback.

Its success marker is:

```text
BRINGUP_PASS mode=1920x1080@60 frame_bytes=0x7e9000
```

The application deliberately leaves PCI memory decoding and scanout active so
an engineer can observe the result. It is a diagnostic side effect and is not
the Driver Binding cleanup policy. Automatic GOP boot does not run this
application.

## GOP Test Application

`SimpleDisplayGopTest.efi` locates the Simple Display child by the combined
protocol contract rather than assuming a handle number. It requires GOP,
empty EDID Discovered/Active, one mode, `PixelBltOnly`, and zero framebuffer
base/size.

The test covers:

- exact `QueryMode()` information and invalid-mode behavior;
- `SetMode()` state and the BLT-only framebuffer contract;
- video fill;
- buffer-to-video and video-to-buffer transfers with nontrivial `Delta`;
- guard pixels around a copied subrectangle;
- upward and downward overlapping video-to-video copies; and
- zero-sized, out-of-bounds, overflow, null-buffer, and invalid-layout calls.

Success ends with `GOP_TEST_PASS`. It intentionally changes visible output and
is a validation consumer, not a production dependency.

## Host Scripts

The firmware package is supported by host-side scripts rather than embedding
build policy inside the EDK II sources:

- `scripts/build_uefi.sh` pins EDK II, builds all three images, and records
  hashes and build metadata.
- `scripts/test_uefi_contract.py` compares firmware and Linux-facing source
  contracts.
- the Shell, cold-bind, and automatic-GOP record/check pairs seal raw logs and
  exact tested payloads.
- `scripts/package_gop_option_rom.sh` refuses packaging unless the exact driver
  and GOP test match a passing cold-bind record.

The structural relationship is therefore source -> build artifacts -> sealed
hardware evidence -> exact-driver Option ROM -> FPGA deployment evidence.
