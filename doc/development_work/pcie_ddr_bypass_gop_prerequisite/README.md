# PCIe DDR Bypass GOP Prerequisite

Status: **superseded after the 2026-07-16 paired-ILA postmortem**

> Correction: the original scratch test proved that bypass accesses completed,
> but paired VDMA/MIG captures later showed that the bypass frame and VDMA
> scanout selected different DDR offsets. The visible color-bar result was
> corrupted, so this package must not be treated as end-to-end GOP framebuffer
> validation. The July 16 hardware replaces it with a 64 MiB BAR and a shared
> `0x3e000000-0x3fffffff` DDR map.

This package documents the superseded hardware and Linux-driver experiment
that first tested the framebuffer mechanism required by a future UEFI Graphics
Output Protocol (GOP) driver. It does not claim that GOP itself exists, and it
must not be used as the current firmware address contract. The result was
narrower than originally reported:

> Ordinary CPU MMIO writes through PCIe BAR2 can reach an 8 MiB DDR3 slice.
> The later paired ILA capture disproved the claimed alias with VDMA scanout.

The diagnostic wrote a `1280x720` XRGB8888 color-bar frame through bypass AXI
address `0x3f800000`; VDMA was programmed with master-relative address
`0x40000000`. An AXI ILA independently confirmed that scratch traffic reached
`0x3ffff000-0x3ffff00c` and completed with `OKAY` responses. Paired ILA evidence
later proved those two frame addresses selected different physical DDR
locations. Normal Linux operation was then restored to four VDMA frames
beginning at `0x41000000` on that historical export.

## Documents

1. [Problem and hardware evolution](01_problem_and_hardware_evolution.md)
2. [Historical intended address contract](02_final_hardware_contract.md)
3. [Driver and diagnostic implementation](03_driver_and_diagnostics.md)
4. [Live validation and GOP handoff](04_live_validation_and_gop_handoff.md)

## Related project documents

- [Current shared 32 MiB prerequisite](../shared_32m_ddr_gop_prerequisite/README.md)
- [GOP research and target architecture](../../gop_research/README.md)
- [Detailed dated test evidence](../../../Linux_DRM_Driver/tests/TEST_LOG.md#2026-07-15-remapped-8-mib-bypass-ddr-contract)
- [Current hardware-interface documentation](../../Linux_DRM_Driver_AI_GEN_documents/hardware_interface.md)
- [Current hardware-contract checker](../../../scripts/check_fpga_hardware_contract.py)
- [Next GOP firmware implementation roadmap](../gop_firmware_implementation/README.md)

## Scope boundary

Completed here:

- a bootable 32 MiB BAR2 allocation on the test host;
- a non-aliased 8 MiB DDR window within BAR2;
- successful direct bypass scratch and full-frame writes;
- proof that the proposed bypass and VDMA addresses did **not** select the same
  physical frame;
- driver-side address validation and bounded mappings;
- non-destructive scratch and full-active-frame diagnostics;
- static HWH/source checks and live ILA confirmation; and
- restoration of the then-working normal four-frame `fpga_drm` display path.

Still outside this package:

- an EDK II UEFI driver that installs GOP;
- a PCI Expansion ROM containing that driver;
- firmware framebuffer handoff to Linux;
- maximum-resolution direct-BAR runtime validation; and
- repeated cold-boot enumeration and Option ROM reliability testing.
