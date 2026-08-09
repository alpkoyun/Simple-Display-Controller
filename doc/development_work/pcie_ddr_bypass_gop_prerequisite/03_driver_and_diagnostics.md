# Driver and Diagnostic Implementation

> Historical record: the addresses and commands below describe the superseded
> July 15 8 MiB experiment. Use the
> [current live-validation record](../shared_32m_ddr_gop_prerequisite/02_live_validation_and_h2c_gap.md)
> for the 64 MiB BAR and shared `0x3e000000` design.

## Driver changes

The implementation in
[`fpga_drm_drv.c`](../../../Linux_DRM_Driver/fpga_drm/fpga_drm_drv.c) now
distinguishes three address roles:

| Role | Address | Behavior |
|---|---:|---|
| CPU/bypass GOP frame | `0x3f800000` | Converted to BAR2 offset `0x00800000` for CPU MMIO writes |
| VDMA GOP/test frame | `0x40000000` | Programmed into VDMA while `ddr_bypass_test=1` |
| Normal VDMA ring | `0x41000000` | Four-frame H2C display operation when the diagnostic is off |

The normal frame count remains four. The diagnostic deliberately selects one
frame because it parks scanout on the single bypass-visible physical region;
turning the diagnostic off restores the four-frame ring.

## Alias-aware validation

`fpga_drm_axi_to_bar_offset()` rejects any mapping unless:

1. the requested AXI range is inside the 32 MiB BAR resource;
2. the resulting host range stays below `0x01000000`;
3. `translation OR start_offset` equals the requested start address; and
4. `translation OR end_offset` equals the requested end address.

This prevents a future source or hardware change from silently reintroducing
the upper-half alias found by the ILA.

## Bounded BAR mappings

The embedded XDMA library keeps only the first 1 MiB of a large BAR permanently
mapped. That is sufficient for the current control registers and avoids a
large, unnecessary kernel virtual mapping. The library now reports both:

- the full PCI BAR resource length; and
- the length of the permanently mapped prefix.

The optional DDR diagnostic maps only the requested scratch page or active
frame with `pci_iomap_range()`, then unmaps it immediately. Contract checks use
the full 32 MiB PCI resource length, not the 1 MiB permanent mapping length.

## Diagnostic behavior

The diagnostic is opt-in through:

```text
ddr_bypass_test=1 upload_enabled=0 configure_pipeline=1
```

Its probe/modeset sequence is:

1. Validate the exported BAR size, address constants, frame bounds, scratch
   separation, and translation representability.
2. Map scratch page `BAR2+0x00fff000`.
3. Save four existing 32-bit words.
4. Write `55aa00ff`, `a55ac33c`, `01234567`, and `89abcdef`.
5. Read and compare every word.
6. Restore and verify the original contents.
7. Configure VDMA for one frame at its address `0x40000000`.
8. Generate an XRGB8888 color-bar frame, map the required active-frame bytes
   at `BAR2+0x00800000`, copy it with `memcpy_toio()`, and check the first and
   last pixels.

A detected transaction, bounds, or readback failure aborts probe or modeset.
This diagnostic did not detect that the bypass and VDMA masters selected
different physical DDR locations; that gap required paired ILA captures and a
monitor-visible correctness check.

## Reusable test tools

### Static hardware/source contract

[`check_fpga_hardware_contract.py`](../../../scripts/check_fpga_hardware_contract.py)
parses the HWH export, checks the XDMA parameters and all master address ranges,
compares them with driver literals, and applies the same OR-based
representability rule:

```bash
python3 scripts/check_fpga_hardware_contract.py --require-ddr-bypass
```

### Bypass AXI capture

[`capture_bypass_axi_ila.tcl`](../../../scripts/capture_bypass_axi_ila.tcl)
finds the restored XDMA System ILA and triggers on an accepted write to
`0x3ffff000`:

```bash
vivado -mode batch \
  -source scripts/capture_bypass_axi_ila.tcl \
  -tclargs fpga_hardware/PCIe_wrapper/PCIe_wrapper.ltx \
  tmp_bypass_ila_capture_8m
```

### Capture decoder

[`analyze_bypass_axi_ila.py`](../../../scripts/analyze_bypass_axi_ila.py)
extracts accepted AXI address, data, and response handshakes and compares the
observed address with both OR and additive translation models:

```bash
python3 scripts/analyze_bypass_axi_ila.py \
  tmp_bypass_ila_capture_8m/bypass_axi_aw.csv
```

The decoder returns success only when the intended address is observed and all
captured read/write responses are `OKAY`.

## Live reload sequence

Run the disruptive portion only after saving work; it temporarily stops the
desktop:

```bash
sudo systemctl stop display-manager
sudo modprobe -r fpga_drm
sudo modprobe fpga_drm \
  debug_logging=1 upload_enabled=0 configure_pipeline=1 \
  ddr_bypass_test=1 connector_connected=1 connector_non_desktop=0 \
  enable_fbdev=1 enable_overlay=0
```

After capturing the evidence, restore the normal path:

```bash
sudo modprobe -r fpga_drm
sudo modprobe fpga_drm \
  debug_logging=1 upload_enabled=1 configure_pipeline=1 \
  ddr_bypass_test=0 connector_connected=1 connector_non_desktop=0 \
  enable_fbdev=1 enable_overlay=1
sudo systemctl start display-manager
```
