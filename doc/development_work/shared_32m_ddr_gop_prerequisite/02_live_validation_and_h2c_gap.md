# Live Validation and Remaining H2C Fault

## Test setup

The July 16 live run used:

- PCI endpoint `0000:01:00.0`, `10ee:7024`;
- the current 64 MiB BAR2 FPGA export and matching `.ltx`;
- repository and installed `fpga_drm.ko` with matching `srcversion`
  `D270FDB2548287DBC83C505`;
- display manager stopped; and
- connector 36 and CRTC 34 for that module load. DRM object IDs are not stable
  across reloads and must be enumerated again.

The static export check and driver build passed:

```sh
python3 scripts/check_fpga_hardware_contract.py --require-ddr-bypass
make -C Linux_DRM_Driver/fpga_drm
```

## Direct-BAR acceptance test

The diagnostic module was loaded without fbdev or normal H2C uploads:

```sh
sudo modprobe -r fpga_drm
sudo modprobe fpga_drm \
  debug_logging=1 enable_overlay=0 composition_backend=cpu \
  connector_connected=1 connector_non_desktop=0 enable_fbdev=0 \
  upload_enabled=0 upload_full_frame=1 configure_pipeline=1 \
  ddr_bypass_test=1
```

Probe performed a non-destructive scratch test and reported:

```text
BAR2 resource=0x04000000
translation=0x3c000000
DDR bypass=0x3e000000-0x3fffffff
scratch AXI=0x3ffff000 BAR2+0x03fff000: PASS
```

The active-frame test was started with:

```sh
modetest -M fpga_drm -s 36@34:1280x720-60@XR24
```

The driver reported:

```text
VDMA S2MM: frames=1 first=0x3e000000 SR=0x00010000
VDMA MM2S: frames=1 first=0x3e000000 SR=0x00011000
DDR bypass pattern: 3,686,400 bytes
  bypass_AXI=0x3e000000 VDMA_AXI=0x3e000000
VTC_ERR=0x00000000
CLK_WIZ_STATUS=0x00000001
```

The user confirmed a correct visible color bar on the attached display. This
is the decisive end-to-end proof: the BAR writer and VDMA reader select the
same DDR bytes and the downstream video pipeline consumes them correctly.

## Normal H2C isolation test

The module was reloaded with one clean normal upload, with fbdev disabled to
remove continuous retry noise:

```sh
sudo modprobe fpga_drm \
  debug_logging=1 enable_overlay=0 composition_backend=cpu \
  connector_connected=1 connector_non_desktop=0 enable_fbdev=0 \
  upload_enabled=1 upload_full_frame=1 configure_pipeline=1 \
  ddr_bypass_test=0
```

VDMA accepted the four-frame configuration at `0x3e000000`, but XDMA timed
out before delivering a stream:

```text
VDMA S2MM ... frames=4 first=0x3e000000 SR=0x00010000
VDMA MM2S ... frames=4 first=0x3e000000 SR=0x00010000
0-H2C0-ST status: BUSY
completed_desc_count=0
async frame upload failed: err=-110 len=0
```

The current `xdma_ila` monitors the H2C AXI stream and bypass AXI interface.
Two trigger tests isolated the failure:

| ILA observation | Result | Interpretation |
|---|---|---|
| Trigger on H2C `TVALID=1` while starting a transfer | No trigger before timeout | XDMA emitted no stream beat. |
| Trigger on H2C `TREADY=0` | No trigger | VDMA did not backpressure the idle interface. |
| Trigger on H2C `TREADY=1` | Immediate capture | VDMA sink was ready. |
| Captured stream state | `TREADY=1`, `TVALID=0`, `Inactive`, `No Streams` | Failure is upstream of VDMA S2MM and DDR writes. |

The PCI status also showed sticky `<MAbort+`, which is consistent with a
requester-side PCIe read problem but is not timestamped proof by itself.

## What is and is not proved

Proved:

- the 64 MiB BAR is allocated and memory-decodable;
- the `0x3c000000` translation exposes non-aliased control and DDR offsets;
- direct PCIe writes reach the shared DDR frame;
- VDMA MM2S reads that exact frame; and
- `1280x720@60` produces correct visible pixels.

Not proved or currently failing:

- XDMA requester descriptor fetch and H2C streaming on this export;
- normal native desktop uploads;
- a live maximum-size `1920x1080` direct frame;
- UEFI access before Linux; and
- Expansion ROM loading.

The run ended with the display manager inactive and `fpga_drm` deliberately
loaded in direct diagnostic mode (`ddr_bypass_test=Y`, `upload_enabled=N`,
`enable_fbdev=N`, `enable_overlay=N`). That was a test-state choice, not the
intended final boot configuration.

