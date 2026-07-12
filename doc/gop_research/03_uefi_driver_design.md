# UEFI Driver Design

## Driver shape

Create an EDK II X64 UEFI boot-service PCI driver named
`SimpleDisplayGopDxe`. It follows the UEFI Driver Model and creates one HDMI
child handle beneath the FPGA PCI controller.

Suggested source split:

| File | Responsibility |
|---|---|
| `SimpleDisplayGopDxe.inf` | Module metadata, PCI Option ROM metadata, packages, libraries, and protocols. |
| `DriverBinding.c` | Entry point and `Supported()`, `Start()`, `Stop()`. |
| `Gop.c` | `QueryMode()`, `SetMode()`, `Blt()`, mode state. |
| `Hardware.c` | PCI BAR discovery and video-IP register access. |
| `Modes.c` | Exact timing table and pixel-clock parameters. |
| `Edid.c` | DDC/EDID acquisition, validation, and EDID protocol instances. |
| `Hdmi.c` | HDMI transmitter I2C programming. |
| `SimpleDisplay.h` | Private data, signatures, constants, and prototypes. |

## Driver binding behavior

`Supported()` should open `EFI_PCI_IO_PROTOCOL` with
`EFI_OPEN_PROTOCOL_BY_DRIVER`, read PCI configuration space, and accept only
the intended identity. During development that is vendor `0x10ee`, device
`0x7024`, display class `0x03`. Add a project-specific subsystem ID or revision
before deployment so the Option ROM does not claim unrelated Xilinx XDMA
cards.

`Start()` should:

1. open and retain `EFI_PCI_IO_PROTOCOL`;
2. enable PCI memory decoding; bus mastering is unnecessary for the direct-BAR
   design;
3. use `GetBarAttributes()` to discover BAR bases and sizes instead of assuming
   host physical addresses;
4. verify register and framebuffer apertures with a hardware magic/version
   register;
5. create the HDMI child device path;
6. read EDID through AXI IIC;
7. build a safe mode list;
8. allocate GOP mode/private structures;
9. install GOP, EDID Discovered, EDID Active, and Device Path protocols; and
10. allow firmware console policy to connect the child.

`Stop()` must uninstall child protocols and release resources. Do not turn off
scanout merely because boot services are ending.

## GOP operations

### `QueryMode()`

Return a newly allocated `EFI_GRAPHICS_OUTPUT_MODE_INFORMATION` with exact
resolution, `PixelsPerScanLine`, and pixel format. Start with one validated
mode, `1024x768@60`, to reduce clocking and EDID variables. Then add modes from
the existing exact timing whitelist that are also supported by the monitor.

### `SetMode()`

Validate the mode index, stop/reset the relevant VDMA channels, program clock
and timing blocks, select the GOP DDR frame, start VDMA MM2S, update GOP mode
state, and clear the visible framebuffer to black. Every wait must have a
timeout and return `EFI_DEVICE_ERROR` on hardware failure.

### `Blt()`

Implement all four required operations:

- `EfiBltVideoFill`;
- `EfiBltVideoToBltBuffer`;
- `EfiBltBufferToVideo`; and
- `EfiBltVideoToVideo`.

Use EDK II `FrameBufferBltLib` against the BAR-mapped framebuffer. This is a
better starting point than custom rectangle code and follows the pattern used
by upstream EDK II GOP drivers such as `QemuVideoDxe`.

## EDID and mode policy

UEFI's PCI graphics rules require EDID retrieval for the physical output.
Reuse the existing AXI IIC path, validate EDID header/checksum, and install the
discovered bytes on the HDMI child handle. If EDID read fails, expose a small
safe fallback list rather than arbitrary modelines.

The firmware mode table should share exact timings with Linux. A future
hardware-contract generator can emit C tables for both environments from one
data file; until then, add a cross-check test that compares the UEFI and DRM
mode names, active sizes, clocks, porches, sync widths, and polarities.

## Alternative: GOP on today's streaming hardware

A proof-of-concept could report `PixelBltOnly`, maintain a shadow framebuffer
in UEFI memory, and port enough XDMA descriptor/polling logic to upload the
shadow image after each `Blt()`. UEFI's PCI I/O `Map()` service would be
required for every DMA-visible host buffer.

This is not the recommended product path because it:

- ports a large and security-sensitive DMA engine into firmware;
- can require full-frame uploads for small text changes;
- provides no direct `FrameBufferBase` for early OS handoff;
- complicates IOMMU/mapping and timeout behavior; and
- duplicates the Linux transport rather than presenting a simple display BAR.

Use it only if a quick Shell-loaded GOP experiment is worth more than the
engineering cost of the framebuffer BAR.

## Linux handoff

GOP leaves the display scanning out at `ExitBootServices()`. Linux can bind a
generic EFI/simple firmware framebuffer before `fpga_drm`. The native PCI DRM
driver must remove that conflicting owner near the beginning of probe with the
kernel-6.8 API:

```c
#include <drm/drm_aperture.h>

ret = drm_aperture_remove_conflicting_pci_framebuffers(
        pdev, &fpga_drm_driver);
if (ret)
        return ret;
```

This host's installed 6.8 headers and symbol table contain that API. Re-check
the helper name when porting to newer kernels because it moved to the generic
Linux aperture interface after 6.12.

For a seamless transition, defer destructive pipeline resets until the native
driver is ready to present its first frame. Functional takeover can precede
flicker-free takeover as a separate milestone.

## References

- [UEFI 2.11 GOP and EDID protocols](https://uefi.org/specs/UEFI/2.11/12_Protocols_Console_Support.html#graphics-output-protocol)
- [UEFI 2.11 PCI I/O and DMA mapping](https://uefi.org/specs/UEFI/2.11/14_Protocols_PCI_Bus_Support.html#efi-pci-io-protocol)
- [EDK II `QemuVideoDxe` GOP implementation](https://github.com/tianocore/edk2/blob/master/OvmfPkg/QemuVideoDxe/Gop.c)
- [EDK II INF-based Option ROM generation](https://tianocore-docs.github.io/edk2-UefiDriverWritersGuide/draft/18_pci_driver_design_guidelines/187_pci_option_rom_images/1872_using_inf_file_to_generate_pci_option_rom_ima.html)
- [Linux 6.8 firmware framebuffer handoff](https://docs.kernel.org/6.8/gpu/drm-internals.html#managing-ownership-of-the-framebuffer-aperture)

