# Live Validation and GOP Handoff

> Historical record: this document preserves the July 15 8 MiB result and its
> paired-ILA correction. The current passing direct-BAR result and independent
> H2C failure are documented under
> [Shared 32 MiB DDR GOP Prerequisite](../shared_32m_ddr_gop_prerequisite/README.md).

> **Postmortem correction:** the evidence below proved successful bypass DDR
> transactions and a healthy normal H2C path, but it did not prove that bypass
> writes and VDMA reads addressed the same physical bytes. Paired ILAs later
> observed bypass writes at MIG `0x3f800000` while VDMA address `0x40000000`
> reached MIG offset zero. The color bars were visibly corrupted. The
> shared-framebuffer acceptance gate therefore failed, and the July 16 shared
> `0x3e000000` map supersedes this configuration.

## Historical test configuration

The final test was run on 2026-07-15 after installing the rebuilt driver and
rebooting with the updated FPGA/SPI image.

```text
PCI function:       0000:01:00.0, 10ee:7024
kernel driver:      fpga_drm
module srcversion:  E191F18FCE249601FF6F162 (installed and repo match)
normal mode:        1280x720@60 during final capture
BAR2:               32 MiB
bypass translation: 0x3f000000
```

The user first confirmed visible normal display output. Kernel readback showed
four VDMA frames beginning at `0x41000000`, `VTC_ERR=0`, and completed XDMA
frame uploads.

## Evidence that initially passed

With the display manager stopped, the bypass ILA was armed and the diagnostic
driver loaded. The kernel reported:

```text
DDR bypass scratch test passed at AXI=0x3ffff000 BAR2+0x00fff000
    (4 words restored)
VDMA S2MM ... frames=1 first=0x40000000
VDMA MM2S ... frames=1 first=0x40000000
DDR bypass test pattern wrote GOP frame
    bypass_AXI=0x3f800000 VDMA_AXI=0x40000000
    for 1280x720@60 (3686400 bytes)
```

The independent ILA capture contained:

| Evidence | Observed result |
|---|---|
| Trigger | Accepted `AWADDR=0x3ffff000` |
| Additional writes | `0x3ffff004`, `0x3ffff008`, `0x3ffff00c` |
| Write responses | Four `OKAY` responses |
| Read addresses | `0x3ffff000-0x3ffff00c` |
| Read responses | Five captured `OKAY` responses |
| Translation | `0x3f000000 OR 0x00fff000 = 0x3ffff000` |
| Analyzer | `PASS: intended AXI address completed with OKAY responses` |

This proves the transaction left XDMA with the expected address, decoded into
the intended bypass segment, completed successfully, and returned the written
data. The frame-pattern log proves that the bypass mechanism can cover an
entire active `1280x720` frame. It does **not** prove that VDMA scanned those
same bytes; the later paired capture disproved that claim.

## Restoration evidence

After the diagnostic, the normal module configuration and desktop were
restored:

```text
display-manager=active
ddr_bypass_test=N
upload_enabled=Y
configure_pipeline=Y
enable_fbdev=Y
enable_overlay=Y
connector=connected
VDMA S2MM/MM2S frames=4 first=0x41000000
asynchronous frame uploads completing
```

The full raw record is in
[`TEST_LOG.md`](../../../Linux_DRM_Driver/tests/TEST_LOG.md#2026-07-15-remapped-8-mib-bypass-ddr-contract).

## Acceptance status

| Gate | Status | Evidence or remaining work |
|---|---|---|
| HWH and driver maps agree | Pass | Strict contract checker passes. |
| Scratch writes reach DDR without descriptors | Pass | Save/write/read/restore plus AXI `OKAY` responses. |
| Bypass frame and VDMA scanout share physical memory | **Failed after paired-ILA review** | Bypass and VDMA completed successfully but reached different MIG offsets. |
| Normal Linux display path remains functional | Pass | Four frames at `0x41000000`, visible output, and completed uploads after restoration. |
| Maximum `1920x1080` bypass frame | Static fit proven; live test pending | `0x7e9000` bytes fits; run the live pattern at this mode before calling Stage 2 fully hardened. |
| Independent userspace BAR writer | Pending | The kernel diagnostic proves the mechanism; a userspace test can validate the same contract with display drivers unbound. |
| Repeated cold-boot enumeration | Pending | One successful reboot is not the planned multi-cycle reliability test. |
| UEFI GOP protocol | Not implemented | Begin with a Shell-loaded EDK II driver. |
| PCI Expansion ROM | Not implemented | Current hardware export has Expansion ROM disabled. |

## Lessons carried into the current GOP design

The direct-write mechanism was demonstrated, but the address alias in this
historical contract failed. Only these general lessons carry forward:

1. Match PCI device `10ee:7024` and verify the expected hardware revision or
   contract before programming it.
2. Discover BAR resources rather than assuming host physical addresses.
3. Require direct writes and VDMA scanout to select the same physical DDR
   bytes, confirmed by correct visible output and focused hardware evidence.
4. Port the existing Linux register algorithms without Linux APIs.
5. Implement and test all required GOP `Blt()` operations.

Do not reuse any address in this historical package. The current values are in
the [shared 32 MiB contract](../shared_32m_ddr_gop_prerequisite/01_current_contract.md).

## Recommended next work

1. Use the July 16 shared 32 MiB hardware contract for all firmware work.
2. Follow the current Shell-application and fixed-mode GOP roadmap.
3. Keep the hardware contract/version register and read-only Expansion
   ROM/BRAM stage-gated until the manual driver is repeatable.

The forward-looking design and staged gates remain in
[`gop_research`](../../gop_research/README.md). The executable, project-specific
plan is in the
[GOP firmware implementation roadmap](../gop_firmware_implementation/README.md).
