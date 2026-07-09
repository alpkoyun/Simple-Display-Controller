# Recreating the Vivado Hardware Project on Linux

This document describes the Linux Vivado flow for recreating the AX7203
PCIe/HDMI hardware project from git-tracked source files.

The source of truth is `vivado_project/`. Generated Vivado projects, build
runs, HLS `solution1/` directories, logs, and bitstreams are intentionally not
tracked in git.

## Toolchain

Use the same major tool versions as the project export:

- Vivado 2023.2
- Vitis HLS 2023.2
- Python 3

The scripts default to the local paths used on this machine:

```sh
VIVADO_BIN=/home/alpk/xilinx/Vivado/2023.2/bin/vivado
VITIS_HLS_BIN_DIR=/home/alpk/xilinx/Vitis_HLS/2023.2/bin
```

If your install paths differ, set those variables before running the flow:

```sh
export VIVADO_BIN=/path/to/Vivado/2023.2/bin/vivado
export VITIS_HLS_BIN_DIR=/path/to/Vitis_HLS/2023.2/bin
```

## What the Scripts Do

`vivado_project/scripts/run_linux_vivado_flow.sh` is the top-level entry point.
It performs these steps:

1. Checks that Vivado is available.
2. Adds Vitis HLS to `PATH`.
3. Regenerates missing AX7203 HLS IP packages.
4. Recreates the Vivado project under `vivado_project/linux_build/PCIe` if the
   project is missing.
5. Opens the recreated project.
6. Refreshes the HLS and custom IP repositories.
7. Upgrades locked HLS IPs when required.
8. Validates the block design.
9. Generates IP targets.
10. Checks and fixes the AX7203 PCIe GT lane LOC order.

The default HLS IP set is:

```sh
color_convert pixel_pack pixel_unpack trace_cntrl_32 trace_cntrl_64
```

The `_2` HLS projects are excluded from the default flow because they target a
different FPGA part and are not used by this AX7203 Vivado project.

## Check-Only Recreate Flow

Run this first after cloning or after changing hardware sources:

```sh
vivado_project/scripts/run_linux_vivado_flow.sh
```

Expected result:

- HLS IP packages are generated if missing.
- `vivado_project/linux_build/PCIe/PCIe.xpr` exists.
- Vivado validates the block design.
- The lane-order checker reports:

```text
pipe_lane[0..3] -> [5, 4, 6, 7]
```

This flow does not run synthesis or implementation.

## Force a Fresh Recreate

`--recreate` refuses to overwrite an existing generated project. Move the old
generated project first:

```sh
mv vivado_project/linux_build/PCIe vivado_project/linux_build/PCIe.old
vivado_project/scripts/run_linux_vivado_flow.sh --recreate
```

Use this when testing whether the checked-in sources are sufficient to recreate
the project from scratch.

## Build Bitstream and Reports

Run synthesis and implementation:

```sh
vivado_project/scripts/run_linux_vivado_flow.sh --build --jobs 2
```

Generated outputs stay under:

```text
vivado_project/linux_build/PCIe/
```

Important generated files:

- `PCIe.runs/impl_1/PCIe_wrapper.bit`
- `PCIe.runs/impl_1/PCIe_wrapper.bin`
- `PCIe.runs/impl_1/PCIe_wrapper.ltx`
- `linux_reports/timing_summary.rpt`
- `linux_reports/route_status.rpt`
- `linux_reports/drc.rpt`
- `linux_reports/utilization.rpt`

These files are ignored by git.

## Install Local Board Artifacts

To copy generated programming artifacts into the local board-artifact path:

```sh
vivado_project/scripts/run_linux_vivado_flow.sh --build --install-artifacts --jobs 2
```

This copies `.bit`, `.bin`, and `.ltx` files to:

```text
fpga_hardware/PCIe_wrapper/
```

`fpga_hardware/` is ignored by git. Share known-good bitstreams through release
artifacts or an explicit Git LFS policy, not through normal source commits.

## Session Notes and Known Warnings

The current check-only flow was validated on Linux with Vivado/Vitis HLS
2023.2. During validation, HLS IP packages were regenerated, Vivado upgraded
locked `color_convert` and `pixel_unpack` IP instances, block-design target
generation completed, and the PCIe lane-order check passed.

Expected non-fatal messages include:

- Vitis HLS warning that `vitis_hls` is deprecated in favor of `vitis-run`.
- Vivado warning about no write access to the local Tcl store under
  `~/.Xilinx/Vivado/2023.2/XilinxTclStore`.
- HLS scheduling warnings for the trace controller IPs.
- Block-design warnings about the XDMA stream width mismatch into
  `frame_counter_0`; the design intentionally uses the lower-order stream bits
  in this path.
- HLS IP revision changes after regeneration; `build_pcie_linux.tcl` upgrades
  locked HLS IPs before validation.

Treat the run as failed if:

- `validate_bd_design` exits with an error.
- `scripts/check_fix_pcie_lane_order.py` cannot find or fix the PCIe GT lane
  LOC constraints.
- The final lane order is not `[5, 4, 6, 7]`.
- `--build` completes with negative routed setup slack that is unacceptable for
  the validation you are doing.

## Git Notes

The tracked hardware source set should include:

- `vivado_project/export/PCIe.tcl`
- `vivado_project/scripts/`
- `vivado_project/hls/` source projects, excluding generated `solution1/`
- `vivado_project/IPs/IP_Packages/`
- selected seed files under `vivado_project/PCIe.srcs/`

Before committing hardware-flow changes, inspect the staged files:

```sh
git add .gitignore vivado_project doc/vivado_linux_recreate.md README.md
git diff --cached --name-only
```

Do not commit generated Vivado build directories such as `.runs`, `.cache`,
`.gen`, `.hw`, `.ip_user_files`, `linux_build`, HLS `solution1/`, or bitstream
artifacts.
