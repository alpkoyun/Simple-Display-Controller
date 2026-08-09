# Current Hardware and Driver Contract

## Authoritative inputs

The July 16 contract is derived from the current exported
[`PCIe.hwh`](../../../fpga_hardware/PCIe_wrapper/PCIe.hwh), recreate-oriented
[`PCIe.tcl`](../../../fpga_hardware/PCIe_wrapper/PCIe.tcl), and the matching
driver constants in
[`fpga_drm_drv.c`](../../../Linux_DRM_Driver/fpga_drm/fpga_drm_drv.c).

The files under `fpga_hardware/` are currently being edited and exported by
hand while the design is still changing. They are authoritative for this live
development baseline even though they are intentionally ignored by git. The
tracked Vivado recreate/export source will be synchronized after the hardware
contract is finished; reproducibility from a clean checkout is a freeze/release
gate, not a prerequisite for the next manual UEFI experiment.

Run this check after every hardware export and before rebuilding or installing
the driver:

```sh
python3 scripts/check_fpga_hardware_contract.py --require-ddr-bypass
```

## PCI resources

| Resource | Current contract | Purpose |
|---|---|---|
| PCI identity | `10ee:7024`, display class `0x038000` | Linux and future UEFI driver match |
| BAR0 | 64 KiB, non-prefetchable | XDMA configuration/engine registers |
| BAR2 | 64 MiB, 64-bit prefetchable | Bypass aperture for video registers and DDR |
| Bypass AXI translation | `0x3c000000` | Aligned base for the complete 64 MiB BAR |
| Expansion ROM | Disabled | Must be added only after manual GOP validation |

The live host assigned BAR2 at `0xf0000000`, but software must discover the
base on every boot. Only the BAR-relative offsets below are part of the
portable contract.

## BAR2 and AXI map

The translation is aligned to the 64 MiB aperture, so address composition is
non-aliased over the complete host-offset range:

```text
emitted bypass AXI address = 0x3c000000 OR BAR2 offset
```

| BAR2 offset | Bypass AXI address | Size | Function |
|---:|---:|---:|---|
| `0x00000000` | `0x3c000000` | 64 KiB | Pixel unpack |
| `0x00010000` | `0x3c010000` | 64 KiB | Video timing controller |
| `0x00020000` | `0x3c020000` | 64 KiB | HDMI AXI IIC |
| `0x00030000` | `0x3c030000` | 64 KiB | AXI UART Lite |
| `0x00040000` | `0x3c040000` | 64 KiB | AXI VDMA registers |
| `0x00050000` | `0x3c050000` | 64 KiB | Color conversion |
| `0x00060000` | `0x3c060000` | 64 KiB | Video clock wizard |
| `0x00070000` | `0x3c070000` | 64 KiB | Video-lock GPIO |
| `0x02000000-0x03ffffff` | `0x3e000000-0x3fffffff` | 32 MiB | Shared MIG DDR window |

VDMA MM2S and S2MM independently map that same numeric DDR range:

```text
M_AXI_BYPASS MIG  0x3e000000-0x3fffffff
M_AXI_MM2S MIG    0x3e000000-0x3fffffff
M_AXI_S2MM MIG    0x3e000000-0x3fffffff
```

Using the same numeric address removes the master-relative alias that
corrupted the earlier 8 MiB color-bar experiment.

## Frame allocation

| Use | AXI range | BAR2 range | Notes |
|---|---|---|---|
| GOP/direct frame 0, maximum mode | `0x3e000000-0x3e7e8fff` | `0x02000000-0x027e8fff` | One maximum `1920x1080` XRGB8888 frame |
| Normal four-frame ring | `0x3e000000-0x3ffa6fff` | Same DDR window | Four maximum frames with 4 KiB spacing |
| Scratch page | `0x3ffff000-0x3fffffff` | `0x03fff000-0x03ffffff` | Reserved write/read/restore diagnostic |

For the validated first mode:

```text
mode                 1280x720@60
line bytes           5120 (0x1400)
active frame bytes   3,686,400 (0x00384000)
BAR2 framebuffer     offset 0x02000000
VDMA MM2S frame      0x3e000000
pixel layout         little-endian XRGB8888 / GOP BGRR
```

The GOP path must configure one MM2S frame and leave S2MM stopped. The native
Linux path normally configures four frames and uses S2MM as the XDMA H2C stream
sink, but that transport is currently a separate failing path.
