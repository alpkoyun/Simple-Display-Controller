# Vivado Project Source Control

This directory is the canonical Linux Vivado project source for the AX7203
PCIe/HDMI design. Git should store the files needed to recreate the Vivado
project, not the generated Vivado project directory.

For the full operational runbook, see
[`../doc/vivado_linux_recreate.md`](../doc/vivado_linux_recreate.md).

## Toolchain

- Vivado 2023.2
- Vitis HLS 2023.2
- Python 3 for `scripts/check_fix_pcie_lane_order.py`

The default script paths match this development machine:

```sh
VIVADO_BIN=/home/alpk/xilinx/Vivado/2023.2/bin/vivado
VITIS_HLS_BIN_DIR=/home/alpk/xilinx/Vitis_HLS/2023.2/bin
```

Override those environment variables when the tools live elsewhere.

## What belongs in git

Keep only the recreate inputs:

- `export/PCIe.tcl`
- `scripts/`
- HLS source projects under `hls/`, excluding `solution1/`
- custom IP package source under `IPs/IP_Packages/`
- the small set of source/constraint seed files under `PCIe.srcs/` that the
  exported Tcl script imports

Do not commit `.xpr` projects, `.runs`, `.cache`, `.gen`, `.hw`,
`.ip_user_files`, hardware-manager captures, logs, reports, or generated
bit/bin/ltx artifacts.

## Recreate and check the project

Run a check-only flow:

```sh
vivado_project/scripts/run_linux_vivado_flow.sh
```

Force recreation into `vivado_project/linux_build/PCIe`:

```sh
vivado_project/scripts/run_linux_vivado_flow.sh --recreate
```

Run synthesis and implementation:

```sh
vivado_project/scripts/run_linux_vivado_flow.sh --build --jobs 2
```

Copy generated `.bit`, `.bin`, and `.ltx` files into `fpga_hardware/` for
local board use:

```sh
vivado_project/scripts/run_linux_vivado_flow.sh --build --install-artifacts --jobs 2
```

`fpga_hardware/` is ignored by git. If a known-good FPGA image must be shared,
publish it separately as a release artifact or adopt Git LFS explicitly.

## Rebuild behavior

`run_linux_vivado_flow.sh` rebuilds missing AX7203 HLS IP packages before
recreating the Vivado project. The default HLS project set is:

```sh
color_convert pixel_pack pixel_unpack trace_cntrl_32 trace_cntrl_64
```

The `_2` HLS projects are excluded from the AX7203 source-control set because
they target a different part and are not used by this Vivado project.

The Vivado build upgrades locked HLS IPs, validates the block design, and runs
`scripts/check_fix_pcie_lane_order.py` so the generated AX7203 PCIe GT lane LOC
order is `5,4,6,7` for `pipe_lane[0..3]`.

The old `FPGA_Src/` tree is retained for history, but this directory is the
active source of truth for the Linux Vivado flow.
