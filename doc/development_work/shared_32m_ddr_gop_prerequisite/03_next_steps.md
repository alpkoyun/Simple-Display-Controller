# Go/No-Go Gates and Next Work

## Work that can proceed now

The following GOP tasks depend only on PCI memory decoding and the validated
direct BAR path, so the current H2C failure does not block them:

1. scaffold the pinned EDK II X64 build;
2. define the shared BAR/register/mode contract;
3. build `SimpleDisplayBringup.efi`;
4. discover `10ee:7024` and BAR2 through `EFI_PCI_IO_PROTOCOL`;
5. repeat the scratch write/read/restore at BAR2 offset `0x03fff000`;
6. program one `1280x720@60` MM2S frame at AXI `0x3e000000`;
7. fill BAR2 offset `0x02000000` and prove visible UEFI color bars; and
8. move the hardware library into a manually loaded fixed-mode GOP driver.

PCI bus mastering should remain unnecessary for these stages. Do not
reimplement XDMA H2C in UEFI.

## H2C recovery work

Normal Linux takeover is blocked until one of these paths is validated:

- repair XDMA requester/descriptor fetch so H2C again produces
  `TVALID && TREADY` handshakes; or
- add and performance-validate a native direct-BAR upload backend in
  `fpga_drm`.

Recommended H2C investigation order:

1. reproduce a minimal bounded H2C write with the Xilinx reference XDMA
   driver, independent of DRM;
2. confirm the installed `xdma.ko` is the vendor PCI reference driver, not the
   unrelated Ubuntu in-tree module;
3. capture PCIe requester/AER evidence around descriptor fetch and clear/read
   sticky PCI status in a controlled test;
4. compare current XDMA IP requester, tag, interrupt, and streaming settings
   against the last known-good H2C export; and
5. keep the stream ILA armed on `TVALID=1` as the first recovery gate.

H2C recovery succeeds only when completed descriptor count advances, the ILA
records nonzero stream handshakes, VDMA S2MM writes the selected frame, and a
native `fpga_drm` upload is visible.

## Remaining acceptance gates

| Gate | Current status | Required result |
|---|---|---|
| Direct `1280x720@60` framebuffer | Pass | Preserve as regression test. |
| Direct maximum `1920x1080` frame | Pending | Correct stable pattern and bounds/readback evidence. |
| UEFI Shell BAR/scratch access | Pending | `EFI_PCI_IO_PROTOCOL` accesses pass before Linux. |
| UEFI Shell visible frame | Pending | Stable color bars remain after application returns. |
| Manually loaded fixed-mode GOP | Pending | GOP handle, empty EDID protocols, fixed tested modes, and complete BLT tests pass. |
| Native Linux takeover | Blocked by H2C | GOP frame hands off to a working native upload path. |
| Expansion ROM | Not implemented | ROM bytes, packaging, execution, and recovery all pass. |
| Cold-boot reliability | Pending | Planned repeated AC and warm-boot matrix passes. |

## Decision

Proceed with the Shell application and fixed-mode GOP implementation on the
current direct-BAR hardware contract. Run H2C recovery as a parallel hardware
track, but treat it as a hard prerequisite for the final GOP-to-native-Linux
handoff milestone.
