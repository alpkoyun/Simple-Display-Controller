# GOP Execution Roadmap

## Guiding sequence

Use the smallest artifact that isolates the next unknown:

```text
Pinned EDK II build
  -> Shell bring-up application
  -> Shell-loaded fixed-mode GOP driver
  -> expand and validate the fixed mode table
  -> firmware-console and bootloader use
  -> Linux handoff
  -> card-local Option ROM
  -> cold-boot hardening
```

The current FPGA image is sufficient for the Shell application, fixed-mode GOP,
and firmware-consumer stages because those use the live-validated direct BAR
framebuffer. EDID acquisition is not planned: this board setup has no usable,
publicly documented EDID/DDC read interface through its HDMI device. Native
Linux handoff is not yet clear: the current XDMA H2C requester stays `BUSY`
without emitting stream `TVALID`. Before Stage 5, either repair that hardware
path or validate a direct-BAR upload backend in `fpga_drm`. A separate hardware
export is still required when adding a contract/version register and the
Expansion ROM BRAM path.

Implementation status on 2026-07-16: Stages 0-2 are source-complete and the
three X64 EFI images pass a clean pinned EDK II host build. Stage 1 has not run
in UEFI Shell, so its visible-output gate remains open and the Stage 2 driver
must not yet be treated as hardware-validated.

## Stage 0: freeze GOP v0 and scaffold the build

Create a source-controlled firmware area without committing an EDK II checkout
or generated `Build/` output:

```text
firmware/uefi/
  README.md
  SimpleDisplayPkg/
    SimpleDisplayPkg.dec
    SimpleDisplayPkg.dsc
    Include/SimpleDisplayHardware.h
    Library/SimpleDisplayHwLib/
    Application/SimpleDisplayBringup/
    Application/SimpleDisplayGopTest/
    SimpleDisplayGopDxe/

scripts/
  build_uefi.sh
  validate_gop_rom.sh

build/gop/                 generated and ignored
```

Actions:

- pin an explicit EDK II commit and record its hash in `firmware/uefi/README.md`;
- build X64 with the EDK II `GCC` toolchain family (`GCC5` was removed before
  the pinned `edk2-stable202605` release);
- define one shared hardware contract header;
- keep mode timing and register code in a library usable by both the Shell
  application and DXE driver;
- add host-side tests for address bounds, mode metadata, and rectangle
  overflow before any firmware test; and
- make build output deterministic enough to record hashes.

The hand-edited export under `fpga_hardware/` remains authoritative while the
hardware is changing. Synchronizing the tracked Vivado recreate/export source
is required before a hardware freeze or release, but it does not block the
manual Shell and GOP experiments on the validated image.

Gate:

- a minimal `SimpleDisplayBringup.efi` builds from a clean checkout;
- the EDK II commit, compiler version, build command, and output hash are
  recorded; and
- no generated EDK II tree is added to the repository.

## Stage 1: run a verbose UEFI Shell bring-up application

Build an application before a driver. It should:

1. locate every handle exposing `EFI_PCI_IO_PROTOCOL`;
2. match vendor `0x10ee`, device `0x7024`, and display class `0x03`;
3. print PCI location, command register, revision, BAR descriptors, and sizes;
4. enable PCI memory decoding without depending on PCI bus mastering;
5. require BAR2 to be at least 64 MiB;
6. verify scratch read/write/restore through BAR2 offset `0x03fff000`;
7. configure HDMI I2C, pixel unpack, color conversion, clock, VTC, and VDMA
   MM2S for `1280x720@60`;
8. leave VDMA S2MM stopped so it cannot overwrite the GOP frame;
9. fill BAR2 offset `0x02000000` with a deterministic color-bar pattern; and
10. print all status/readback registers with bounded timeout results.

Use `EFI_PCI_IO_PROTOCOL.Mem.Read()` and `Mem.Write()` initially. This avoids
assuming a host physical BAR address and gives every MMIO failure an EFI status
code. The later GOP driver will additionally obtain the physical BAR base for
`FrameBufferBase` through `GetBarAttributes()`.

Gate:

- the application reports scratch restoration and healthy clock/VTC/VDMA
  status;
- the color bars are visibly stable on the FPGA HDMI output while still in
  UEFI Shell; and
- returning from the application does not stop scanout.

This is the first proof that the path works before Linux. Do not start GOP
protocol debugging until this gate passes.

## Stage 2: build the Shell-loaded fixed-mode GOP driver

Implement `SimpleDisplayGopDxe` using the UEFI Driver Model:

- `Supported()` opens `EFI_PCI_IO_PROTOCOL` by driver and validates PCI
  identity and required BAR resources;
- `Start()` enables memory decoding, discovers BAR2 with
  `GetBarAttributes()`, creates an HDMI child handle, and installs Device Path,
  GOP, EDID Discovered, and EDID Active protocols without making a visible
  change to the output;
- `Stop()` uninstalls child protocols and releases owned resources; and
- boot-service exit must not blank the display or stop VDMA.

The HDMI device cannot provide EDID in this setup. Install both EDID protocol
instances with `SizeOfEdid=0` and `Edid=NULL`; do not invent monitor data. Use a
fixed mode table built from the exact timings already supported by `fpga_drm`.
The first hardware build advertised only the validated `1280x720@60`
development mode. The current replacement candidate still advertises exactly
one mode, but promotes `1920x1080@60` to mode 0 after the accepted PixelBltOnly
handoff and successful Linux 1080p60 scanout. The remaining timings stay in the
shared candidate table and are not advertised.

For the current `1920x1080@60` default candidate:

```text
PixelFormat       = PixelBltOnly
PixelsPerScanLine = 1920
FrameBufferBase   = 0
FrameBufferSize   = 0
```

Implement `QueryMode()`, `SetMode()`, and all four required `Blt()` operations.
Use explicit 32-bit `EFI_PCI_IO_PROTOCOL.Mem.Read/Write` transactions for all
framebuffer operations; ordinary CPU accesses and `FrameBufferBltLib` are not
valid for this PCI window.
All visible pipeline programming belongs in `SetMode()` or the explicit Shell
bring-up application; Driver Binding `Start()` must not visibly change the
display.

Gate:

- `load -nc SimpleDisplayGopDxe.efi` succeeds from UEFI Shell;
- connecting the FPGA controller creates one GOP handle;
- `dh -p GraphicsOutput` finds the FPGA output;
- the HDMI child has EDID Discovered and EDID Active with zero-length data;
- a small `SimpleDisplayGopTest.efi` application queries the mode and exercises
  fill, buffer-to-video, video-to-buffer, and overlapping video-to-video BLTs;
- invalid mode and out-of-bounds requests return correct errors; and
- unloading/stopping the driver removes protocols without corrupting other
  PCI devices.

## Stage 3: expand and validate the exact fixed mode list

Keep the mode policy independent of monitor discovery:

- retain the zero-length EDID protocol instances;
- expose only exact timing profiles copied or generated from the Linux
  whitelist;
- retain `1280x720@60` as the known fallback candidate while validating
  `1920x1080@60` as the new development default;
- validate `640x480@60` and `800x600@60` as conservative plug-in-display
  modes; and
- add `1024x768@60` and `1280x1024@60` only after each mode passes
  firmware-visible scanout.

GOP mode information does not report refresh rate. Keep the Linux 30 Hz
profiles in the shared timing source, but do not expose indistinguishable 30 Hz
and 60 Hz entries for the same resolution to generic GOP consumers without a
separate, documented selection policy.

Generate or cross-check Linux and UEFI timing tables from one source so pixel
clock, totals, porches, sync polarity, line bytes, and clock-wizard values
cannot drift independently.

Gate:

- both EDID protocols consistently report no data;
- every reported GOP mode sets successfully and every unreported mode fails;
  and
- mode changes preserve correct `FrameBufferSize` and BLT bounds.

## Stage 4: prove firmware-console and bootloader use

Installing GOP is different from firmware selecting it as a console. Test:

- `connect` of the specific FPGA controller first, then `connect -r`;
- `ConOut` and firmware primary-display policy;
- UEFI Shell/GraphicsConsole output on FPGA HDMI;
- a bootloader splash/menu using the FPGA GOP; and
- iGPU enabled and disabled cases.

Do not change PCI class code merely because the motherboard continues to use
the iGPU console. If GOP exists but is not selected, diagnose console policy
separately from driver correctness.

Gate:

- the FPGA GOP is independently queryable;
- at least one firmware or bootloader graphics consumer uses it visibly; and
- the result is reproducible after warm reboot with the driver loaded from a
  FAT USB or EFI System Partition.

## Stage 5: implement Linux framebuffer handoff

Current status: **blocked by the native frame-upload path, not by GOP BAR
scanout**.

Leave VDMA and the GOP frame active across `ExitBootServices()`. Then verify:

1. the EFI stub/sysfb path creates a firmware framebuffer;
2. `simpledrm` or the platform firmware-framebuffer owner shows early graphics;
3. `fpga_drm` removes the conflicting PCI framebuffer aperture near probe;
4. `fpga_drm` reinitializes its normal four-frame ring; and
5. the desktop appears without BAR ownership conflicts.

First accept a short blank during native takeover. Treat seamless preservation
of the GOP frame until the first atomic commit as a later refinement.

Gate:

- one boot visibly progresses from GOP to early Linux to native DRM;
- no duplicate framebuffer owner or resource conflict remains; and
- normal `fpga_drm` uploads and four-frame scanout pass, using either recovered
  XDMA H2C or a separately accepted direct-BAR backend.

Do not use the current direct color-bar pass as evidence for this gate. The
latest isolated normal upload configured VDMA successfully but timed out with
XDMA `BUSY`, zero completed descriptors, and ILA `TREADY=1`, `TVALID=0`.

## Stage 6: add the PCI Option ROM hardware path

Only after the Shell-loaded GOP driver is stable:

- add a read-only hardware magic/version/feature register;
- reserve a project-specific subsystem ID or revision policy;
- package the X64 driver with EDK II Option ROM tooling;
- validate ROM signature, PCIR fields, image size, checksum, code type,
  machine type, vendor/device IDs, and last-image flag;
- enable PF0 Expansion ROM in XDMA;
- connect its read path to read-only BRAM initialized from the ROM image;
- prove Linux can read the exact ROM bytes through the PCI ROM sysfs resource;
  and
- rebuild and program the SPI configuration image.

The version register is required before automatic Option ROM deployment so an
embedded driver can refuse an incompatible hardware image. It is not required
to obtain the first manual UEFI pixels on this known board.

Gate:

- the ROM readback hash matches the packaged image;
- motherboard firmware loads the driver without USB/ESP assistance;
- the FPGA GOP exists after cold boot; and
- disabling slot Option ROM execution provides a predictable recovery path.

## Stage 7: harden boot reliability and recovery

- Run at least 20 true AC-power cold boots and 10 warm boots.
- Record endpoint presence, BAR assignment, ROM execution, GOP mode, monitor
  output, bootloader use, early Linux, and native takeover for every cycle.
- Test no monitor, HDMI I2C timeout, VDMA error, clock unlock, BAR
  allocation failure, unsupported hardware version, and corrupted ROM.
- Keep JTAG recovery and a known-good non-GOP SPI image available.
- Test Secure Boot/signing on a capable platform before calling the ROM
  portable; the current B85 host does not provide that production gate.

Gate: no unexplained failures across the target boot matrix, and every injected
fault returns a bounded error without hanging firmware.

## Recommended implementation order

The immediate coding order is:

1. firmware scaffold and pinned EDK II build;
2. `SimpleDisplayHwLib` contract/types;
3. `SimpleDisplayBringup.efi` PCI discovery and scratch test;
4. bring-up application pipeline programming and visible color bars;
5. `SimpleDisplayGopDxe.efi` with the initial fixed modes, empty EDID protocols,
   and complete BLT operations; and
6. `SimpleDisplayGopTest.efi` plus Shell evidence.

Maximum-resolution Linux bypass testing can run in parallel, but it does not
need to delay the first `1280x720` UEFI application because that exact frame
size has already passed the current hardware diagnostic.

XDMA H2C recovery must also run in parallel and is a hard dependency only when
the work reaches native Linux takeover.
