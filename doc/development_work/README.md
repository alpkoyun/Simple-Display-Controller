# Development Work

This directory records completed or actively validated engineering work. It
sits between the forward-looking design documents and the raw test log:

- development packages explain why a change was needed, what was implemented,
  and what was proved;
- [`../gop_research/`](../gop_research/README.md) describes the intended GOP
  architecture and future work; and
- [`../../Linux_DRM_Driver/tests/TEST_LOG.md`](../../Linux_DRM_Driver/tests/TEST_LOG.md)
  retains the dated command output and detailed runtime evidence.

## Work packages

| Package | Status | Result |
|---|---|---|
| [XDMA Expansion ROM and GOP Option-ROM integration](xdma_expansion_rom_gop_integration/README.md) | Milestone 1 ROM transport passed; display regressions pending | Adds a deterministic 4 KiB non-executable ROM path, fail-closed two-layer XDMA generation/repair checks, SDC1 ABI registers, state-preserving three-read Linux verification, and a Shell-gated 32 KiB EFI packaging path. The fresh routed build has WNS `-1.165 ns` under the explicit `-2.500 ns` hobby threshold, WHS `+0.049 ns`, zero blocking DRC errors, and zero unrouted nets. After JTAG programming and reboot, Linux advertised a 4 KiB ROM and three 4096-byte reads matched the pinned build input exactly. |
| [Shared 32 MiB DDR GOP prerequisite](shared_32m_ddr_gop_prerequisite/README.md) | Current; direct path validated, H2C blocked | The July 16 shared `0x3e000000` map passes scratch and correct visible `1280x720@60` direct-BAR color bars. Normal XDMA H2C stalls before `TVALID`. |
| [GOP firmware implementation](gop_firmware_implementation/README.md) | Stages 0-2 source built; Shell validation pending | Pinned EDK II package now builds the bring-up app, fixed-mode GOP driver, and GOP test. Run the bring-up app first; native Linux handoff remains gated by H2C recovery or a direct-BAR upload backend. |
| [Historical 8 MiB PCIe DDR prerequisite](pcie_ddr_bypass_gop_prerequisite/README.md) | Superseded after paired-ILA review | Bypass accesses completed, but bypass and VDMA selected different physical DDR offsets and produced corrupted bars. Retained as the hardware-evolution record. |

Each package should state its scope, authoritative hardware/software contract,
validation evidence, known limitations, and handoff to the next development
stage.

## Hardware source workflow

The tracked recreate source under `vivado_project/` is now reconciled to the
validated 64 MiB BAR2/shared-DDR hardware contract and is the persistent source
for the Expansion-ROM integration. The generated project, XDMA sources, and
build products remain disposable. The 2026-07-25 board proof establishes the
implemented ROM transport image through post-reboot PCI readback; display and
DRM behavior remain separate regression gates.
