# Storage, Build, and Loading Model

## Where source should live

Keep the new source in this repository, separate from Linux and from generated
Vivado output:

```text
firmware/uefi/
  README.md
  SimpleDisplayPkg/
    SimpleDisplayPkg.dec
    SimpleDisplayPkg.dsc
    Include/
      SimpleDisplayHardware.h
    Library/
      SimpleDisplayHwLib/
    Application/
      SimpleDisplayBringup/
      SimpleDisplayGopTest/
    SimpleDisplayGopDxe/
      SimpleDisplayGopDxe.inf
      DriverBinding.c
      Gop.c
      Hardware.c
      Modes.c
      Edid.c                    empty EDID protocol instances
      Hdmi.c
      SimpleDisplay.h

scripts/
  build_gop_rom.sh
  validate_gop_rom.sh

build/gop/                       generated, ignored
  SimpleDisplayGopDxe.efi
  SimpleDisplayGop.rom
  SimpleDisplayGop.mem

vivado_project/                  source-controlled integration/recreate Tcl
fpga_hardware/PCIe_wrapper/      validated release bundle only
```

Do not commit an EDK II build tree or normal `Build/` output. Pin and record the
EDK II tag/commit used by the build script. Keep the driver source, INF/DSC/DEC,
ROM conversion, BRAM initialization conversion, and validators reproducible.

## What is stored where at runtime

There are three different images and two different flash/storage roles:

| Image | Development location | Final location | Who loads it |
|---|---|---|---|
| FPGA configuration image, `PCIe_wrapper.bin` | Repo build/release output | AX7203 `mt25ql128` SPI configuration flash | FPGA configuration logic at power-on |
| UEFI driver, `SimpleDisplayGopDxe.efi` | USB or system EFI System Partition | Contained in the Option ROM image | UEFI Shell/Boot Manager during development |
| PCI Option ROM, `SimpleDisplayGop.rom` | `build/gop/` | BRAM initialized inside the FPGA bitstream | Motherboard UEFI PCI bus driver through Expansion ROM BAR |

The final GOP ROM does not need to be a separately writable file on the host
disk. It becomes data inside the FPGA image. Updating the final GOP therefore
requires rebuilding and reprogramming the FPGA SPI image unless a later design
adds independently updateable ROM storage.

## Build dependency chain

```text
EDK II sources
  -> X64 PE/COFF boot-service driver (.efi)
  -> EfiRom / INF / FDF packaging (.rom)
  -> ROM header validator
  -> BRAM initialization file (.mem/.coe)
  -> Vivado synthesis and implementation
  -> PCIe_wrapper.bit
  -> SPI x4 PCIe_wrapper.bin
  -> program AX7203 mt25ql128
```

The Option ROM header must match the PCI function's vendor/device identity and
contain a UEFI image for the machine type supported by the host. For this
machine, build X64 first. A multi-architecture product can place multiple UEFI
images in one Option ROM, but that is not needed for the first milestone.

## Development loading sequence

### Phase 0: UEFI Shell bring-up application

Run `SimpleDisplayBringup.efi` first. It should enumerate the PCI function,
discover and validate BAR2, perform the non-destructive scratch test, program
the `1280x720@60` pipeline, and display a deterministic frame while printing
each checkpoint. This isolates firmware MMIO and video initialization from GOP
Driver Binding and protocol installation.

### Phase 1: manual UEFI Shell driver load

Use a FAT-formatted USB drive so driver failures cannot make the installed OS
unbootable:

```text
fs0:\EFI\SimpleDisplay\SimpleDisplayGopDxe.efi
```

From UEFI Shell, the intended workflow is:

```text
load fs0:\EFI\SimpleDisplay\SimpleDisplayGopDxe.efi
connect -r
drivers
devices
dh -p GraphicsOutput
```

Exact Shell device names and command options must be recorded from the actual
firmware. Serial/debug output should identify `Supported()`, `Start()`, BAR
discovery, empty EDID protocol installation, `SetMode()`, and GOP installation.

The detailed execution and evidence gates are in the
[GOP firmware development package](../development_work/gop_firmware_implementation/README.md).

### Phase 2: automatic load from an EFI System Partition

Copy the `.efi` driver to a device-specific directory and create a UEFI
`Driver####` load option with `DriverOrder`. This is useful for repeated tests,
but the UEFI specification does not require every platform to load arbitrary
drivers from the ESP. It is a development bridge, not the portable final
deployment.

### Phase 3: card-local PCI Option ROM

Enable the Expansion ROM, embed the validated `.rom` into FPGA BRAM, flash the
complete FPGA image, and remove the ESP-installed driver. UEFI should discover
and load the Option ROM automatically when option-ROM execution is enabled for
the slot.

## Secure Boot and platform policy

This AMI B85 test host reports that it does not support Secure Boot, so the
first X64 development driver can be unsigned. Do not generalize that result.
Other firmware may reject unsigned third-party Option ROM drivers, disable
external Option ROMs, or allow only signed images under its Secure Boot policy.
Signing, key enrollment, and revocation/update policy are required before
calling the design portable or production-ready.

The firmware must also choose the FPGA GOP as a console. Test setup options
such as primary display `PCIe/PEG` versus integrated graphics and inspect
`ConOut`. Do not change PCI class code solely to work around console policy
until the Option ROM is known to load successfully.

## FPGA flashing

The project already builds an SPI x4 configuration image and provides
`vivado_project/scripts/program_spi_from_bin.tcl` for the `mt25ql128` device.
That is the correct final loader path. Keep JTAG programming for development
only; a JTAG-loaded bitstream appears too late to provide cold-boot GOP.

## References

- [EDK II Option ROM distribution](https://tianocore-docs.github.io/edk2-UefiDriverWritersGuide/draft/32_distributing_uefi_drivers/321_pci_option_rom.html)
- [EDK II EFI System Partition distribution](https://tianocore-docs.github.io/edk2-UefiDriverWritersGuide/draft/32_distributing_uefi_drivers/323_efi_system_partition.html)
- [EDK II INF Option ROM packaging](https://tianocore-docs.github.io/edk2-UefiDriverWritersGuide/draft/18_pci_driver_design_guidelines/187_pci_option_rom_images/1872_using_inf_file_to_generate_pci_option_rom_ima.html)
- [Current SPI programming script](../../vivado_project/scripts/program_spi_from_bin.tcl)
