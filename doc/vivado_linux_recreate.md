# Recreating the Canonical Vivado Project

The repository contains one active hardware configuration:
`PCIe_GOP_ROM_32K_1080P_DEFAULT`. It is the validated AX7203 display design
with XDMA 4.1 Gen2 x4, the 64 MiB BAR2 display window, SDC1 identity registers,
and the sealed 32 KiB 1920x1080 GOP Option ROM.

Git stores the inputs needed to recreate that design. The generated `.xpr`,
IP output, synthesis/implementation runs, reports, bitstreams, and hardware
manager state stay under the ignored `vivado_project/linux_build/` directory.

## Requirements

- Vivado 2023.2
- Vitis HLS 2023.2
- Python 3
- Bash

The scripts default to the tool locations used on the development machine.
Override them when necessary:

```sh
export VIVADO_BIN=/path/to/Vivado/2023.2/bin/vivado
export VITIS_HLS_BIN_DIR=/path/to/Vitis_HLS/2023.2/bin
```

## Fresh-clone recreation

From the repository root, run:

```sh
python3 vivado_project/scripts/check_vivado_source_tree.py
vivado_project/scripts/run_expansion_rom_unit_sim.sh
vivado_project/scripts/run_linux_vivado_flow.sh --recreate
```

Simulation scratch directories are created beneath the ignored
`vivado_project/.sim_work/` workspace path, not under `/tmp`. Set
`KEEP_SIM_WORK=1` when the generated simulator transcript must be retained.

The last command creates exactly one generated project:

```text
vivado_project/linux_build/PCIe_GOP_ROM_32K_1080P_DEFAULT/
  PCIe_GOP_ROM_32K_1080P_DEFAULT.xpr
```

`--recreate` refuses to overwrite that directory. Move or remove the generated
directory explicitly when a genuinely fresh recreation is required. Do not
change the project name to preserve an old hardware variant; use the relevant
Git commit instead.

The flow verifies the sealed cold-GOP evidence and ROM hashes, regenerates
missing HLS packages, recreates the clean 64 MiB display block design,
integrates ROM and SDC1, regenerates the outer XDMA and nested `pcie_7x`,
repairs exactly four BAR6 decoder sites, checks the address contract, and
enforces AX7203 PCIe lane order `5,4,6,7`.

The selected HLS C++/Tcl projects and the frame-counter RTL package are source
inputs. Their generated HLS `solution1/` trees and the former standalone
frame-counter Vivado project are build products. The source-tree checker also
rejects a frame-counter package that refers outside its own package directory.

## Build and verify

Run synthesis, implementation, and bitstream generation with:

```sh
vivado_project/scripts/run_linux_vivado_flow.sh --build --jobs 2
```

Audit an existing routed checkpoint without launching a new implementation:

```sh
vivado_project/scripts/run_linux_vivado_flow.sh --verify
```

The routed build is accepted only when:

- setup WNS is at least `-2.500 ns` and hold slack is nonnegative;
- blocking DRC, unrouted-net, and partially-routed-net counts are zero;
- logical PCIe lanes 0 through 3 use GT channels `5,4,6,7`;
- `GTPE2_COMMON_X0Y1`, `PCIE_X0Y0`, refclock buffer `X0Y3`, and PERST# `J20`
  are retained;
- the 32 KiB ROM and SDC1 logic remain in the routed netlist; and
- the XDMA OOC checkpoint was rebuilt after the generated BAR6 repair.

Reports and hashes are written beneath the generated project's
`linux_reports/` directory. Vivado implementation can produce different
bitstream hashes across runs, so acceptance is based on the recorded contract
and exact embedded ROM hash rather than a hard-coded bitstream hash.

To copy the generated `.bit`, SPI `.bin`, `.ltx`, ROM, and reports into the
ignored local board-artifact directory, run:

```sh
vivado_project/scripts/run_linux_vivado_flow.sh \
  --build --install-artifacts --jobs 2
```

This does not program the FPGA. Board programming, PCIe reset, ROM readback,
UEFI GOP validation, and Linux `fpga_drm` regression remain separate hardware
gates.

## Source-control policy

Keep the recreate Tcl, RTL/XDC/MIG inputs, ROM/SDC1 RTL and tests, selected HLS
and custom-IP source, the sealed 1080p EFI payload/evidence, and the deterministic
32 KiB ROM files. Do not commit `.xpr`, `.runs`, `.cache`, `.gen`, `.hw`,
`.ip_user_files`, `.dcp`, bit/bin/LTX build artifacts, HLS `solution1/`, logs,
or local hardware-manager state. The ROM `.bin` under `vivado_project/rom/` is
the intentional exception because it is a small, byte-exact synthesis input.

Before committing, run:

```sh
python3 vivado_project/scripts/check_vivado_source_tree.py --require-tracked
git status --short
```

The source-tree checker accepts zero generated projects in a fresh clone or the
single canonical local project. In pre-commit mode it also requires every
recreation input to be tracked. Any additional `.xpr` is treated as stale
workspace state.
