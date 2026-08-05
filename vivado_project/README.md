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
- source-controlled RTL under `rtl/`
- deterministic ROM inputs and memory initialization under `rom/`
- the sealed 1080p GOP validation manifest, raw logs, and tested EFI payload
  under `firmware/uefi/test_logs/2026-08-05_1080p_cold_bind_pass/`
- source-level simulation under `sim/`
- HLS source projects under `hls/`, excluding `solution1/`
- custom IP package source under `IPs/IP_Packages/`
- the small set of source/constraint seed files under `PCIe.srcs/` that the
  exported Tcl script imports

Do not commit `.xpr` projects, `.runs`, `.cache`, `.gen`, `.hw`,
`.ip_user_files`, hardware-manager captures, logs, reports, or generated
bit/bin/ltx artifacts.

## Recreate and check the project

Run the fixed 32 KiB EFI/GOP Expansion-ROM check-only flow:

```sh
vivado_project/scripts/run_linux_vivado_flow.sh
```

Create the canonical generated project at
`vivado_project/linux_build/PCIe_GOP_ROM_32K_1080P_DEFAULT`:

```sh
vivado_project/scripts/run_linux_vivado_flow.sh --recreate
```

Run synthesis and implementation:

```sh
vivado_project/scripts/run_linux_vivado_flow.sh --build --jobs 2
```

This checkout contains one fixed hardware configuration: the uncompressed
32 KiB EFI/GOP Option ROM at BAR6 and the SDC1 identity endpoint at
BAR2+`0x00080000`. Earlier transport-only and Shell-validation configurations
remain available through their Git checkpoints, not through a stage selector.

The fixed configuration keeps the validated nine-output display/control
interconnect unchanged. A separate same-clock three-output AXI splitter sits
at the XDMA bypass master: output 0 forwards the original BAR2 traffic, output
1 serves the BAR6 ROM at AXI `0x01000000`, and output 2 serves SDC1 at AXI
`0x3c080000`. The production checkpoint retains the two-stage XDMA ILA only;
the old cross-domain video-status ILA is deliberately absent. Visible scanout
and display regression evidence remain separate hardware gates.

Copy generated `.bit`, `.bin`, and `.ltx` files into `fpga_hardware/` for
local board use:

```sh
vivado_project/scripts/run_linux_vivado_flow.sh \
  --build --install-artifacts --jobs 2
```

The fixed EFI/GOP build is fail-closed unless the recorded manual UEFI Shell
evidence matches the packaged driver. See the
[Expansion ROM/GOP integration runbook](../doc/development_work/xdma_expansion_rom_gop_integration/README.md).

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

The frame counter is preserved as a self-contained custom IP package:
`IPs/IP_Packages/component.xml` references only
`IPs/IP_Packages/src/frame_counter.v`. The old standalone
`IPs/frame_counter/*.xpr` project was generated duplication and is not needed
to recreate the block design.

The Vivado flow validates the block design, configures and regenerates both
XDMA layers, repairs exactly four BAR6 decoder sites, disables stale IP-cache
reuse for proof builds, and runs `scripts/check_fix_pcie_lane_order.py` so the
generated AX7203 PCIe GT lane LOC order is `5,4,6,7` for `pipe_lane[0..3]`.
Implementation requires routed setup WNS of at least `-2.500 ns` and
nonnegative hold timing. Blocking DRC errors, unrouted nets, wrong PCIe
placement, missing ROM storage, or missing SDC1 logic remain hard failures.

Audit the canonical completed routed checkpoint without resetting or
relaunching implementation. This is the preferred path when timing is accepted
and another implementation seed is not wanted.

```sh
vivado_project/scripts/run_linux_vivado_flow.sh --verify
```

Run `python3 vivado_project/scripts/check_vivado_source_tree.py
--require-tracked` after staging and before committing. It rejects missing or
untracked canonical inputs, tracked generated products, and any local `.xpr`
other than the canonical project. The recreate flow deliberately has no
project-name selector; historical hardware variants belong in Git history,
not beside the active build.

The old `FPGA_Src/` tree is retained for history, but this directory is the
active source of truth for the Linux Vivado flow.
