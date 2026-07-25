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
| [Shared 32 MiB DDR GOP prerequisite](shared_32m_ddr_gop_prerequisite/README.md) | Current; direct path validated, H2C blocked | The July 16 shared `0x3e000000` map passes scratch and correct visible `1280x720@60` direct-BAR color bars. Normal XDMA H2C stalls before `TVALID`. |
| [GOP firmware implementation](gop_firmware_implementation/README.md) | Stages 0-2 source built; Shell validation pending | Pinned EDK II package now builds the bring-up app, fixed-mode GOP driver, and GOP test. Run the bring-up app first; native Linux handoff remains gated by H2C recovery or a direct-BAR upload backend. |
| [Historical 8 MiB PCIe DDR prerequisite](pcie_ddr_bypass_gop_prerequisite/README.md) | Superseded after paired-ILA review | Bypass accesses completed, but bypass and VDMA selected different physical DDR offsets and produced corrupted bars. Retained as the hardware-evolution record. |

Each package should state its scope, authoritative hardware/software contract,
validation evidence, known limitations, and handoff to the next development
stage.

## Hardware source workflow

During active hardware development, the hand-edited export under
`fpga_hardware/PCIe_wrapper/` is the working source of truth used by the driver,
contract checker, and live tests. The tracked Vivado recreate/export source is
updated only after the hardware contract is finished and frozen. Until that
sync occurs, a clean-checkout hardware rebuild is intentionally deferred and
must not be presented as reproducing the current validated image.
