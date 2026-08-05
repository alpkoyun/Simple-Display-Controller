# Validation and Bring-Up

## Acceptance Matrix

| Gate | Required evidence | Claim authorized |
|---|---|---|
| Source contract | `UEFI_CONTRACT_PASS` | Firmware/Linux constants and source invariants agree |
| EFI build | pinned metadata plus hashes | Reproducible X64 artifacts exist |
| ROM package | EFI/PCIR/PE checker pass | Exact tested driver is in a valid 32 KiB image |
| ROM unit simulation | explicit PASS marker | AXI ROM endpoint serves expected words and ignores writes |
| SDC1 unit simulation | explicit PASS marker | Identity block reports expected fixed ABI |
| Vivado implementation | timing, hold, DRC, route, hierarchy, lane contract | Acceptable hardware image exists |
| PCI enumeration | normal revision, expected IDs/BARs/class | Endpoint and link enumerated |
| ROM readback | three full byte-exact reads plus state restoration | Deployed ROM transport matches packaged image |
| Manual bring-up | `BRINGUP_PASS` and observed pattern | Direct BAR2 programming and scanout work |
| Cold manual binding | sealed bind/disconnect/reconnect record | Driver owns prerequisites and restores state |
| GOP protocol | complete `GOP_TEST_PASS` | Selected driver's mode and BLT behavior work |
| Automatic dispatch | no-USB loaded image and Driver Binding | Firmware executed embedded driver |
| Automatic GOP | no manual load/connect plus visible firmware/GRUB | ROM-to-binding-to-GOP boot path works |
| Linux handoff | normal boot, `fpga_drm` binding, active 1080p scanout | Native OS display path works |
| Persistent deployment | full power cycle from verified SPI | Accepted image survives cold power-on |

Never use a later row to retroactively strengthen an earlier evidence record.

## Host-Only Checks

These checks do not access the board or change display ownership:

```bash
./scripts/build_uefi.sh --check
python3 scripts/test_uefi_contract.py
python3 scripts/test_uefi_shell_log.py
python3 scripts/check_uefi_cold_bind_validation.py
python3 scripts/test_uefi_cold_bind_validation.py
python3 scripts/test_uefi_auto_gop_validation.py
python3 vivado_project/scripts/check_expansion_rom_contract.py
```

The two `test_...validation.py` scripts exercise record/check behavior with
synthetic temporary inputs. The evidence checker separately validates the
accepted real cold-bind record.

## Manual Shell Path

The detailed operator procedure is in the
[UEFI Shell validation guide](../uefi_shell_expansion_rom_validation/README.md).
The structural sequence is:

1. Verify `SHA256SUMS` for the Shell and project EFI files.
2. Boot the X64 Shell using a built-in Shell or FAT32
   `EFI/BOOT/BOOTX64.EFI`.
3. Keep the iGPU display as the Shell console and the FPGA HDMI path as the
   target output.
4. Locate the filesystem and target `10ee:7024` controller.
5. For direct hardware diagnosis, run `SimpleDisplayBringup.efi` and require
   `BRINGUP_PASS` plus the expected visible pattern.
6. Load `SimpleDisplayGopDxe.efi`, target-connect the controller, and confirm a
   second GOP handle.
7. Run `SimpleDisplayGopTest.efi` and require every subtest plus
   `GOP_TEST_PASS`.
8. Preserve raw logs and the exact tested binaries before changing the USB.

USB-to-JTAG hardware is optional for normal Shell validation. It is recovery
equipment when the FPGA has not configured early enough or the persistent
image must be replaced.

## Packaging Cold-Bind Path

The packaging gate is stricter than ordinary manual bring-up:

- start from a cold controller with the pre-bind PCI Command recorded;
- do not run `SimpleDisplayBringup.efi`;
- manually load only the candidate driver;
- targeted-connect the FPGA controller;
- require the 1920x1080 `PixelBltOnly` GOP and `GOP_TEST_PASS`;
- confirm visible FPGA output without disturbing the iGPU Shell;
- targeted-disconnect and require exact Command restoration;
- targeted-reconnect and rerun the GOP test; and
- seal all logs and exact tested payload hashes.

This proves the driver performs its own memory-decode and initialization work.
It still does not prove automatic Option-ROM dispatch because USB supplied the
driver.

## Automatic GOP Path

For the production gate, boot without `load`, `connect`, or the bring-up
application. Require:

1. the embedded image appears as a loaded boot-service driver with Driver
   Binding;
2. firmware associates it with `10ee:7024`;
3. the FPGA physical-output GOP child exists;
4. firmware setup or the bootloader visibly uses the FPGA display; and
5. no USB-loaded copy can account for the result.

If a Shell is used only to inspect handles after automatic dispatch, preserve
the no-load command history and raw `drivers`, `devices`, `dh`, and GOP output.

## ROM Readback

Run byte-exact Linux ROM readback only after a real PCIe reset or cold boot.
The state-preserving helper must:

- reject a missing BDF or revision `ff`;
- record driver binding, enable count, PCI Command, and ROM BAR;
- enable the device/ROM only as required;
- read all 32768 bytes three times;
- compare every copy and SHA-256 with the packaged input;
- disable the ROM and restore the exact entry PCI/driver state; and
- capture relevant kernel/AER evidence.

This operation may disrupt the display driver's ownership. Stop the display
manager or unbind DRM only in a controlled window after warning that the
screen will blank and obtaining approval.

## Linux Handoff

After automatic firmware output, Linux acceptance requires all of:

- normal `10ee:7024` enumeration;
- no `simpledrm` attempt to map the GOP storage as a linear framebuffer;
- automatic or explicit `fpga_drm` binding for the installed kernel module;
- 1920x1080 as the preferred and active connector/CRTC mode;
- locked clock and zero VTC errors;
- continuing successful frame uploads; and
- usable visible FPGA output while the iGPU remains independently available.

Firmware output and Linux output are separate observations even when they
occur during one boot.

## Failure Isolation Ladder

When the FPGA display is blank, stop at the first failed layer:

1. Did the FPGA configure before host PCI enumeration?
2. Does `10ee:7024` enumerate with a normal revision and expected BARs?
3. Does the ROM aperture have the expected size and exact bytes?
4. Did firmware load the embedded EFI image?
5. Does Driver Binding associate with and start on the controller?
6. Was PCI memory decoding enabled before SDC1/BAR2 reads?
7. Does the physical-output child expose GOP and empty EDID protocols?
8. Does mode 0 report 1920x1080 `PixelBltOnly`?
9. Does `SetMode()` complete clock, VDMA, and VTC programming?
10. Do BLT write/read tests pass?
11. Is physical firmware output visible?
12. Does Linux bind `fpga_drm` and take over without simpledrm?

Do not skip from PCI enumeration directly to a GOP or display diagnosis.

## Current Accepted Evidence

The living [project status](../project_next_steps.md) records the accepted
1080p sequence: cold manual bind/reconnect, exact-driver 32 KiB packaging,
fresh implementation and JTAG load, automatic firmware/bootloader output,
Linux DRM handoff, verified persistent SPI programming, full cold boot, and
three byte-exact ROM reads with restored state.

The exact cold-bind evidence is under
`firmware/uefi/test_logs/2026-08-05_1080p_cold_bind_pass/`; implementation and
deployment evidence is under the corresponding `20260805_1080p_default_*`
directories in `artifacts/expansion_rom/`.
