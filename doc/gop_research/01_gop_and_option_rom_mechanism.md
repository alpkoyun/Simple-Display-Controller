# GOP and PCI Option ROM Mechanism

## What GOP supplies

`EFI_GRAPHICS_OUTPUT_PROTOCOL` is the firmware graphics interface used by UEFI
applications, setup screens, boot managers, and OS loaders. A GOP instance
provides:

- `QueryMode()` to enumerate supported resolutions and pixel layouts;
- `SetMode()` to program a selected mode and clear the visible buffer;
- `Blt()` for fills and image copies; and
- a mode structure containing the linear framebuffer physical address and
  size when direct framebuffer access is supported.

The UEFI specification also requires a PCI graphics driver to follow the UEFI
Driver Model. The driver binds to the PCI controller, creates a child handle
for the physical output, reads the monitor's EDID, and installs GOP plus
`EFI_EDID_DISCOVERED_PROTOCOL` and `EFI_EDID_ACTIVE_PROTOCOL` on that child.

GOP does not render 3D graphics. It exposes a scanout surface and block-copy
operations, which matches this project's display-controller scope.

## Boot sequence

1. Power is applied.
2. The AX7203 loads the complete FPGA image from SPI configuration flash.
3. PCIe link training completes and motherboard firmware enumerates
   `10ee:7024`.
4. Firmware enables and reads the PCI Expansion ROM BAR.
5. The PCI bus driver finds a UEFI boot-service driver image in the ROM and
   calls UEFI `LoadImage()`/`StartImage()`.
6. The driver's entry point installs `EFI_DRIVER_BINDING_PROTOCOL`.
7. `Supported()` accepts the FPGA PCI function.
8. `Start()` opens `EFI_PCI_IO_PROTOCOL`, creates the HDMI child handle,
   initializes device state, reads EDID, and installs GOP.
9. Firmware may add this GOP output to `ConOut` and draw setup/boot graphics.
10. The OS loader uses the same GOP framebuffer.
11. At `ExitBootServices()`, GOP calls end, but hardware keeps scanning the
    last framebuffer.
12. Linux initially uses the firmware framebuffer and later hands ownership to
    `fpga_drm`.

Whether step 9 happens automatically is platform policy. This host also has an
Intel integrated display controller. Its AMI setup may require selecting PCIe
graphics as primary, changing `ConOut`, or disabling the integrated output.
Installing GOP makes the FPGA usable by firmware; it cannot force every
firmware implementation to choose it as the primary console.

## Why an Option ROM is the final location

A UEFI PCI Option ROM travels with the PCI device. UEFI firmware discovers it
during PCI enumeration and can load its driver without already having an OS
or disk-specific installation. The UEFI specification defines the ROM image
headers, PCIR structure, PE/COFF driver image, architecture matching, and
512-byte image alignment.

For development, the same `.efi` driver can be loaded from UEFI Shell. Once it
works, EDK II's `EfiRom` tool or INF/FDF packaging creates the `.rom` image.
The driver should not depend on where it was loaded from; the same binary logic
must work from Shell, an EFI System Partition, platform flash, or Option ROM.

## Why not legacy VGA BIOS/VBE

Legacy VGA Option ROMs execute 16-bit x86 code and require VGA-compatible
hardware conventions that this design does not implement. They do not solve
native UEFI boot with Compatibility Support Module disabled. Supporting them
would add a second firmware stack and legacy VGA decode behavior. GOP-only is
the appropriate first and intended target.

## Earliest time output can appear

GOP can display output only after:

- the FPGA has configured;
- the PCIe endpoint is visible;
- PCI resources and the Expansion ROM are enumerated; and
- the UEFI driver has started and selected a mode.

It cannot show the motherboard's very earliest reset/SEC/PEI messages. AMD's
7-series PCIe documentation emphasizes that a boot-visible endpoint must be
ready for configuration requests within the platform's PCIe startup window.
The current SPI x4 image path is promising, but cold-boot enumeration must be
measured repeatedly. A warm boot succeeding when a cold boot fails is a strong
configuration-time warning.

## References

- [UEFI 2.11 GOP](https://uefi.org/specs/UEFI/2.11/12_Protocols_Console_Support.html#graphics-output-protocol)
- [UEFI 2.11 rules for PCI graphics devices](https://uefi.org/specs/UEFI/2.11/12_Protocols_Console_Support.html#rules-for-pci-agp-devices)
- [UEFI 2.11 PCI Option ROM rules and loading](https://uefi.org/specs/UEFI/2.11/14_Protocols_PCI_Bus_Support.html#pci-option-roms)
- [EDK II Driver Writer's Guide, PCI Option ROM](https://tianocore-docs.github.io/edk2-UefiDriverWritersGuide/draft/32_distributing_uefi_drivers/321_pci_option_rom.html)
- [AMD PG054 configuration access timing](https://docs.amd.com/r/en-US/pg054-7series-pcie/Configuration-Access-Specification-Requirements)
- [AMD PG054 FPGA configuration-time calculation](https://docs.amd.com/r/en-US/pg054-7series-pcie/FPGA-Configuration-Times-for-7-Series-Devices)

