# Implementation and Validation Plan

## Scope boundary

The first goal is a UEFI setup/Shell/bootloader image on the FPGA HDMI output,
followed by Linux takeover. Legacy BIOS VGA, 3D acceleration, UEFI runtime
graphics after `ExitBootServices()`, hot-plug, and seamless flicker-free
handoff are later or out of scope.

## Stage 0: freeze the hardware contract

- Use the validated shared BAR2 map for the first manual firmware experiment.
- Define register offsets, framebuffer offset, VDMA DDR address, pixel format,
  and the future Expansion ROM contract.
- During manual development, match the known PCI identity and verify BAR size
  and bounds. Add a project magic/version/feature register and a specific
  subsystem/revision policy before automatic Option ROM deployment.
- Select one initial mode: the live-validated `1280x720@60`, 32-bit pixels.

Gate: the firmware contract, generated `.hwh`, Linux driver, and strict checker
agree on every resource used by the first Shell application.

## Stage 1: prove power-on enumeration

- Build the current design with bitstream compression and the established SPI
  x4 configuration settings.
- Flash through the existing `mt25ql128` flow.
- Confirm the endpoint and BAR2 are present after a true power-on before the
  first Shell test.
- Record PCI presence, link width/speed, BAR allocation, and any failure.
- Defer the full 20 cold/10 warm reliability matrix until the Shell driver is
  stable; repeat it after the Option ROM is embedded.

Gate: the endpoint is present without a rescan on the firmware test boot. If it
is missing, fix FPGA configuration timing before debugging UEFI MMIO or GOP.

## Stage 2: add a BAR-backed scanout frame

Status on 2026-07-16: the strict hardware contract, non-destructive scratch
test, and a full active `1280x720` frame written through BAR2 and scanned by
VDMA all pass with correct visible color bars. Normal four-frame XDMA H2C
uploads fail on this export before stream `TVALID`; that is a separate native
Linux recovery track. A live maximum-resolution run remains before this stage
is fully hardened. See the
[current development record](../development_work/shared_32m_ddr_gop_prerequisite/README.md).

- Map the 32 MiB DDR frame ring into the aligned PCI aperture and route GOP
  writes to frame store 0.
- Verify every used host offset by the actual XDMA translation composition;
  do not accept an address map only because Vivado exports the intended
  segments.
- Park VDMA MM2S on that frame.
- Keep register MMIO functional and track XDMA H2C recovery independently.
- Write a Linux userspace BAR test only while `fpga_drm`/`xdma` are unbound.
- Fill solid colors and a test pattern directly through the BAR.

Gate: `scripts/check_fpga_hardware_contract.py --require-ddr-bypass` passes,
and ordinary PCI memory writes without XDMA descriptors change the HDMI image
at the first firmware mode, `1280x720@60`, and at maximum intended resolution.

## Stage 3: create a Shell-loaded fixed-mode GOP driver

- First build a verbose UEFI Shell application that discovers the FPGA BARs,
  repeats the scratch test, programs the one-mode pipeline, and displays a
  direct-BAR color pattern. Move its hardware code into a shared library.
- Add `firmware/uefi/SimpleDisplayPkg` and reproducible EDK II build scripts.
- Implement PCI binding and BAR discovery. Add hardware-version matching only
  after the version register exists.
- Port the fixed timing profiles already supported by `fpga_drm`.
- Implement GOP `QueryMode()`, `SetMode()`, and all `Blt()` operations with
  `FrameBufferBltLib`.
- Add EDID Discovered/Active protocols with zero-length data; this board setup
  has no usable EDID read path.
- Load from USB in UEFI Shell and connect the controller.

Gate: `dh -p GraphicsOutput` finds the FPGA child, a GOP test application can
set/fill/copy the mode, and UEFI Shell graphics are visible.

## Stage 4: implement Linux handoff

- Add the kernel-6.8 conflicting-framebuffer aperture removal call near the top
  of `fpga_drm_probe()`.
- Ensure Linux `simpledrm` can display the GOP frame before `fpga_drm` binds.
- Ensure `fpga_drm` takes ownership without BAR conflicts or dual drivers.
- Initially accept a short blank during native reinitialization.
- Later preserve the active mode/frame until the first atomic commit.

Gate: one cold boot visibly progresses from UEFI to bootloader/early kernel to
the normal DRM desktop without losing the PCI function.

## Stage 5: add and embed the PCI Option ROM

- Package the X64 boot-service driver using EDK II Option ROM tooling.
- Validate ROM signature, PCIR data, vendor/device IDs, code type, checksum,
  size, and PE/COFF machine type.
- Enable PF0 Expansion ROM and its AXI4-Lite read path.
- Add read-only BRAM initialized from `SimpleDisplayGop.rom`.
- Read the ROM back through Linux sysfs before attempting firmware execution.
- Flash the complete image and remove the manual ESP driver.

Gate: AMI firmware loads the card-local driver and exposes GOP with no USB/ESP
copy of the driver.

## Stage 6: hardening and portability

- Add exact fixed modes from the Linux whitelist after per-mode validation.
- Test no-monitor, empty EDID protocols, HDMI I2C timeout, VDMA/clock lock
  failure, and unsupported BAR allocation.
- Verify every `Blt()` boundary and overlapping video-to-video copy.
- Fuzz/validate Option ROM parsing inputs in host-side tests.
- Add signed-image and Secure Boot testing on a capable platform.
- Test with iGPU enabled/disabled and PCIe selected/not selected as primary.
- Add IA32 only if a real target firmware requires it.

Gate: repeatable behavior across the target host set, with recovery from a bad
GOP image through JTAG/SPI reprogramming.

## Validation evidence to retain

Store dated logs under a future `firmware/uefi/test_logs/` only when they are
small and curated. Each validated release should record:

- EDK II tag/commit and build architecture;
- `.efi`, `.rom`, `.bit`, and `.bin` hashes;
- Option ROM header dump;
- PCI config and BAR layout;
- UEFI Shell `drivers`, `devices`, and GOP protocol output;
- empty EDID protocol state and chosen fixed mode;
- cold/warm boot counts;
- monitor-visible checkpoints;
- Linux early-console and `fpga_drm` takeover logs; and
- matching Vivado build/timing reports and ILA evidence where useful.

## Main risks

| Risk | Early test or mitigation |
|---|---|
| FPGA misses cold-boot PCI enumeration | Prove SPI configuration timing before GOP work; compare cold and warm boot. |
| Old firmware does not execute external UEFI Option ROM | Manual Shell load first; check slot Option ROM policy and primary-display selection. |
| iGPU remains selected as the only console | Inspect/change `ConOut` or firmware primary-display setting after GOP is known to exist. |
| XDMA cannot expose the desired simultaneous BAR layout | Run a minimal generated-design experiment before rewriting the video design. |
| BAR translation aliases the framebuffer subwindow | Enforce aperture alignment in the export checker and require a live write/read/restore test. |
| Large/non-prefetchable BAR is slow or cannot be allocated | Start with one frame and one mode inside the current 64 MiB BAR; inspect firmware resource allocation. |
| UEFI and Linux program hardware differently | Share a hardware contract and compare exact mode/register tables. |
| `simpledrm` conflicts with `fpga_drm` | Add and test the kernel aperture-removal handoff. |
| Bad ROM/bitstream prevents normal boot display | Keep JTAG recovery and a known-good SPI image; test on a nonessential boot path first. |

## Recommended first implementation slice

Do not begin with the Option ROM. The highest-value first slice is:

1. use the live-validated shared 32 MiB DDR framebuffer mapping;
2. build a verbose X64 UEFI Shell bring-up application;
3. prove `1280x720@60` direct-BAR scanout before Linux loads;
4. build and manually load a fixed-mode X64 GOP driver with empty EDID
   protocols; and
5. exercise GOP with a dedicated BLT/mode test application.

That proves the hardware and firmware contracts independently of Option ROM
storage. Embedding the already-working driver is then a contained deployment
step.
