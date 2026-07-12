# Implementation and Validation Plan

## Scope boundary

The first goal is a UEFI setup/Shell/bootloader image on the FPGA HDMI output,
followed by Linux takeover. Legacy BIOS VGA, 3D acceleration, UEFI runtime
graphics after `ExitBootServices()`, hot-plug, and seamless flicker-free
handoff are later or out of scope.

## Stage 0: freeze the hardware contract

- Reserve a project-specific PCI subsystem ID/revision for GOP-capable images.
- Define register BAR, framebuffer BAR, Expansion ROM, DDR address, pixel
  format, and hardware-version register.
- Select one initial mode: `1024x768@60`, 32-bit pixels.
- Decide whether the framebuffer uses a dedicated BAR or a translated subwindow
  after a small XDMA/Vivado experiment.

Gate: a short contract document and generated `.hwh` agree on every resource.

## Stage 1: prove power-on enumeration

- Build the current design with bitstream compression and the established SPI
  x4 configuration settings.
- Flash through the existing `mt25ql128` flow.
- Perform at least 20 AC-power cold boots and 10 warm boots.
- Record PCI presence, link width/speed, BAR allocation, and failures.

Gate: the endpoint is present without rescans on every cold boot. Do not start
GOP debugging if firmware cannot reliably enumerate the FPGA.

## Stage 2: add a BAR-backed scanout frame

- Add a 16 MiB framebuffer PCI aperture translated to DDR frame store 0.
- Park VDMA MM2S on that frame.
- Keep existing register MMIO and XDMA H2C streaming functional.
- Write a Linux userspace BAR test only while `fpga_drm`/`xdma` are unbound.
- Fill solid colors and a test pattern directly through the BAR.

Gate: ordinary PCI memory writes, without XDMA descriptors, change the HDMI
image at `1024x768@60` and at maximum intended resolution.

## Stage 3: create a Shell-loaded GOP driver

- Add `firmware/uefi/SimpleDisplayPkg` and reproducible EDK II build scripts.
- Implement PCI binding and BAR/version discovery.
- Port fixed-mode pipeline setup.
- Implement GOP `QueryMode()`, `SetMode()`, and all `Blt()` operations with
  `FrameBufferBltLib`.
- Add EDID Discovered/Active protocols; allow a safe fallback mode on EDID
  failure.
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

- Add exact EDID-filtered modes from the Linux whitelist.
- Test no-monitor, invalid EDID, HDMI I2C timeout, VDMA/clock lock failure, and
  unsupported BAR allocation.
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
- EDID hash and chosen mode;
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
| Large/non-prefetchable BAR is slow or cannot be allocated | Start with one 16 MiB 32-bit aperture and one mode; inspect firmware resource allocation. |
| UEFI and Linux program hardware differently | Share a hardware contract and compare exact mode/register tables. |
| `simpledrm` conflicts with `fpga_drm` | Add and test the kernel aperture-removal handoff. |
| Bad ROM/bitstream prevents normal boot display | Keep JTAG recovery and a known-good SPI image; test on a nonessential boot path first. |

## Recommended first implementation slice

Do not begin with the Option ROM. The highest-value first slice is:

1. add the 16 MiB DDR framebuffer BAR;
2. prove direct BAR scanout from Linux userspace;
3. build a one-mode X64 GOP driver;
4. load it manually from UEFI Shell; and
5. show the UEFI Shell or a GOP test pattern on HDMI.

That proves the hardware and firmware contracts independently of Option ROM
storage. Embedding the already-working driver is then a contained deployment
step.

