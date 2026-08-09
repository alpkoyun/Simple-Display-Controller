# Build and Packaging

## Reproducibility Model

The accepted boot path is assembled in four controlled stages:

1. Build three X64 EFI images with a pinned EDK II checkout.
2. Validate the driver manually from a cold controller and seal the exact
   driver/test hashes with the raw logs.
3. Package that exact driver as a fixed 32 KiB uncompressed EFI Option ROM.
4. Integrate the exact ROM memory into a fresh Vivado recreation and accept the
   routed implementation before programming JTAG or SPI.

Changing the driver invalidates stages 2-4. Regenerating XDMA requires its
generated-source repairs and contracts to pass again.

## Pinned EDK II Build

The firmware build pins:

```text
tag:          edk2-stable202605
commit:       b03a21a63e3bd001f52c527e5a57feddb53a690b
architecture: X64
toolchain:    GCC
platform:     SimpleDisplayPkg/SimpleDisplayPkg.dsc
```

Check host prerequisites and the existing checkout:

```bash
./scripts/build_uefi.sh --check
```

Build the default DEBUG artifacts:

```bash
./scripts/build_uefi.sh
```

Or select RELEASE explicitly:

```bash
BUILD_TARGET=RELEASE ./scripts/build_uefi.sh
```

The build produces `SimpleDisplayBringup.efi`, `SimpleDisplayGopDxe.efi`,
`SimpleDisplayGopTest.efi`, `SHA256SUMS`, and `build-metadata.txt` under
`build/gop/artifacts/`. `SOURCE_DATE_EPOCH` is derived from the pinned EDK II
commit.

## Source Contract Test

Before board validation, run:

```bash
python3 scripts/test_uefi_contract.py
```

`UEFI_CONTRACT_PASS` verifies the shared timing table, 1080p GOP mode 0, Linux
preferred-mode agreement, `PixelBltOnly`, zero framebuffer fields, Driver
Binding ordering, PCI I/O pixel accesses, and test coverage expected by the
project.

This is a source-level gate. It does not prove the EFI binary was built or run.

## Cold-Bind Evidence Gate

Manual validation starts from a controller whose PCI Command can be `0x0000`
and does not run `SimpleDisplayBringup.efi`. The record/check scripts seal:

- untouched connect, GOP, graphics, disconnect, and reconnect logs;
- visible-output confirmation;
- exact pre/post PCI Command values;
- preservation of the iGPU GOP;
- first and post-reconnect `GOP_TEST_PASS`; and
- hashes of the tested driver and GOP test application.

The accepted 1080p record is
`firmware/uefi/test_logs/2026-08-05_1080p_cold_bind_pass/`.

Validate it with defaults:

```bash
python3 scripts/check_uefi_cold_bind_validation.py
```

## Option-ROM Packaging

Package only after the cold-bind checker passes:

```bash
./scripts/package_gop_option_rom.sh
```

The script invokes pinned EDK II `EfiRom`, finalizes the output to exactly 32
KiB, emits the binary and 32-bit memory image under `vivado_project/rom/`, and
runs the complete EFI/PCIR/PE checker. Success ends with:

```text
SIMPLE_DISPLAY_GOP_OPTION_ROM_PACKAGE_PASS
```

The generated metadata records aperture bytes, driver size/hash, raw EfiRom
size, complete ROM hash, and memory word width. `GOP_SHA256SUMS` pins both the
ROM and embedded driver.

## ROM Source Contracts

Run the tracked source-level integration checker:

```bash
python3 vivado_project/scripts/check_expansion_rom_contract.py
```

Without `--project-dir`, it checks the canonical Tcl/RTL/ROM inputs, including:

- 64 MiB BAR2 and its `0x3c000000` translation;
- shared DDR at `0x3e000000`;
- 32 KiB outer and nested Expansion ROM configuration;
- BAR ID 6 translated to `0x01000000`;
- dedicated ROM and SDC1 AXI segments;
- read-only ROM protocol features;
- byte-exact `.bin` to `.mem` conversion; and
- absence of address aliasing.

Pass a fresh recreated Vivado project with `--project-dir` to check generated
outer/nested XCI values and the target-request decoder repair as well.

The checked-in `fpga_hardware/PCIe_wrapper/PCIe.hwh` is the July 16 clean
pre-ROM export. Validate that file only with
`scripts/check_fpga_hardware_contract.py --clean-baseline`; it is not the HWH
for the active Option-ROM design and correctly fails the integrated-ROM checks.
The active tracked source contract is `vivado_project/`, while routed
implementation reports and hashes are retained under the accepted artifact
directory.

## Focused Simulation

The source flow provides two focused simulations:

- the AXI ROM test checks header/PCIR data, interior/end bytes, bursts,
  backpressure, IDs, and non-mutating write completion;
- the SDC1 test checks the four ABI words and read-only behavior.

These tests establish endpoint protocol behavior without claiming root-port
link training, firmware execution, or physical display output.

## Vivado Acceptance

Use the repository's fresh recreation flow; do not reuse an accepted generated
project as mutable source. The implementation contract requires:

- the exact ROM input hash and retained ROM/SDC1 hierarchy;
- the AX7203 lane order `5,4,6,7`;
- routed setup WNS at or above `-2.500 ns`;
- nonnegative routed hold slack;
- zero blocking DRC errors;
- zero unrouted or partially routed nets; and
- hash-sealed `.bit` and matching `.ltx` outputs.

The accepted 1080p build record is under
`artifacts/expansion_rom/20260805_1080p_default_32k_build/`. Build artifacts are
evidence, not canonical editable source.

## Hardware Deployment Gates

JTAG programming is a fast deployment check, but host PCIe state must be reset
before enumeration or ROM readback is meaningful. SPI acceptance additionally
requires erase/program/verify, restoration of the matching application image,
and a complete power-off/power-on without JTAG assistance.

Retain exact hashes for the ROM, driver, bitstream, probes, and SPI image at
each accepted checkpoint. Do not copy a later hash into an older evidence
record.
