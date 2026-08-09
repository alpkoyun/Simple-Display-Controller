# UEFI Driver Design

## Driver shape

Create an EDK II X64 UEFI boot-service PCI driver named
`SimpleDisplayGopDxe`. It follows the UEFI Driver Model and creates one HDMI
child handle beneath the FPGA PCI controller.

Before building the driver, create a verbose `SimpleDisplayBringup.efi` UEFI
Shell application and a shared `SimpleDisplayHwLib`. The application should
prove PCI/BAR discovery, scratch access, pipeline programming, and a visible
frame while it can still print each step to the existing firmware console.
Only then add Driver Binding and GOP protocol ownership around the same library.

Suggested source split:

| File | Responsibility |
|---|---|
| `SimpleDisplayGopDxe.inf` | Module metadata, PCI Option ROM metadata, packages, libraries, and protocols. |
| `DriverBinding.c` | Entry point and `Supported()`, `Start()`, `Stop()`. |
| `Gop.c` | `QueryMode()`, `SetMode()`, `Blt()`, mode state. |
| `Hardware.c` | PCI BAR discovery and video-IP register access. |
| `Modes.c` | Exact timing table and pixel-clock parameters. |
| `Edid.c` | Empty EDID Discovered/Active protocol instances for this board; no DDC read path is available. |
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
4. validate register and framebuffer apertures; require a hardware
   magic/version register only after that register is added for Option ROM
   deployment;
5. create the HDMI child device path;
6. install EDID Discovered and EDID Active with `SizeOfEdid=0` and
   `Edid=NULL` because this setup has no usable EDID read interface;
7. build the fixed tested mode list;
8. allocate GOP mode/private structures;
9. install GOP and Device Path protocols without visibly updating the output;
   and
10. allow firmware console policy to connect the child.

`Stop()` must uninstall child protocols and release resources. Do not turn off
scanout merely because boot services are ending.

## GOP operations

### `QueryMode()`

Return a newly allocated `EFI_GRAPHICS_OUTPUT_MODE_INFORMATION` with exact
resolution, `PixelsPerScanLine`, and pixel format. Start with the live-validated
direct-BAR mode, `1280x720@60`, plus `800x600@60` or `640x480@60` from the
existing Linux timing table. Add other fixed whitelist modes only after UEFI
scanout validation. GOP mode information does not expose refresh rate, so avoid
simultaneously advertising indistinguishable 30 Hz and 60 Hz entries for one
resolution without a separate timing-selection policy.

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

## Unavailable EDID and fixed mode policy

The HDMI device on this board does not expose a usable, publicly documented
EDID/DDC read interface in this setup. Do not attempt undocumented transactions
or fabricate monitor data. Install EDID Discovered and EDID Active on the HDMI
child with zero size and a null pointer, which is the protocol representation
for unavailable EDID data.

The firmware uses a conservative fixed mode table and does not claim that a
connected monitor supports every entry. The table should share exact timings
with Linux. A future
hardware-contract generator can emit C tables for both environments from one
data file; until then, add a cross-check test that compares the UEFI and DRM
mode names, active sizes, clocks, porches, sync widths, and polarities.

## BLT-only contract on the current PCI framebuffer

The implemented driver reports `PixelBltOnly`, `FrameBufferBase=0`, and
`FrameBufferSize=0`. It does not need a shadow framebuffer or XDMA descriptors:
each GOP operation uses explicit 32-bit `EFI_PCI_IO_PROTOCOL.Mem.Read/Write`
transactions against the existing BAR2-backed DDR storage. This preserves the
validated firmware rendering path without inviting firmware or an early OS
driver to use unreliable ordinary CPU framebuffer accesses.

The native Linux `fpga_drm` driver remains responsible for taking ownership
after boot. The generic `simpledrm` path must not bind this FPGA GOP because no
linear framebuffer is advertised.

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
