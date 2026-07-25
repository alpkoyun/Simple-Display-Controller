# Shared 32 MiB DDR GOP Prerequisite

Status: **direct-BAR prerequisite validated at `1280x720@60`; native XDMA
H2C upload path blocked**

This is the current development record for the July 16 FPGA export. It
supersedes the earlier 8 MiB experiment, whose bypass and VDMA addresses
decoded to different DDR locations.

The current design gives the PCIe bypass master and both VDMA masters the same
32 MiB DDR address range. A Linux diagnostic wrote a complete XRGB8888 frame
through BAR2, VDMA MM2S scanned the same address, and the attached monitor
showed correct color bars.

```text
BAR2+0x02000000
  -> bypass AXI 0x3e000000
  -> DDR frame 0
  -> VDMA MM2S 0x3e000000
  -> HDMI
```

That is the hardware mechanism required by the first UEFI Shell application
and GOP driver. GOP does not need PCI bus-master DMA: firmware can publish the
BAR-backed DDR frame as its linear framebuffer.

## Current conclusion

| Path | Status | Evidence |
|---|---|---|
| BAR2 control-register access | Pass | Driver programmed pixel unpack, color conversion, I2C, clock, VDMA, VTC, and GPIO through offsets below 1 MiB. |
| BAR2 scratch access to DDR | Pass | Four-word write/read/restore passed at AXI `0x3ffff000`, BAR2 offset `0x03fff000`. |
| Direct BAR frame write | Pass at `1280x720@60` | Driver wrote `3,686,400` bytes at BAR2 offset `0x02000000`. |
| VDMA MM2S scanout of that frame | Pass | VDMA read `0x3e000000`; `VTC_ERR=0`, clock status was locked, and the user confirmed correct visible color bars. |
| Normal four-frame address fit | Static pass | Four maximum frames fit at `0x3e000000-0x3ffa6fff` inside the 32 MiB window. |
| XDMA H2C into VDMA S2MM | Fail on current export | XDMA stayed `BUSY` with zero completed descriptors; ILA showed `TREADY=1`, `TVALID=0`. |
| PCI Expansion ROM | Not implemented | The current export still has PF0 Expansion ROM disabled. |

The direct GOP prerequisite passes even though normal H2C uploads do not. The
two results must remain separate: the direct path uses CPU PCIe memory writes,
whereas H2C requires the XDMA endpoint to fetch host descriptors and emit an
AXI stream.

## Documents

1. [Current hardware and driver contract](01_current_contract.md)
2. [Live validation and remaining H2C fault](02_live_validation_and_h2c_gap.md)
3. [Go/no-go gates and next work](03_next_steps.md)

## Related records

- [Historical 8 MiB prerequisite](../pcie_ddr_bypass_gop_prerequisite/README.md)
- [GOP firmware implementation roadmap](../gop_firmware_implementation/README.md)
- [Detailed kernel test log](../../../Linux_DRM_Driver/tests/TEST_LOG.md#2026-07-16-paired-ila-correction-and-shared-32-mib-ddr-map)
- [Current hardware-interface reference](../../Linux_DRM_Driver_AI_GEN_documents/hardware_interface.md)
- [Hardware-contract checker](../../../scripts/check_fpga_hardware_contract.py)

