# 2026-08-03 GOP test failure

The untouched Shell log is preserved at `raw/gop_test.log`.

- USB source: `/media/alpk/ALP/EFI/SimpleDisplay/evidence/gop_test.log`
- Raw size: 350 bytes
- Raw SHA-256: `a5fa3eb9252f0ebf2532cf48d5d310541c9d3717749bb672e0e019e239c1b0dc`
- Detected encoding: BOM-marked UTF-16LE with CRLF line endings

The decoded log proves that the driver was connected, both empty-EDID
protocols were found, mode 0 was reported as 1280x720, and the mode tests
passed. The first buffer-transfer test then failed while comparing the
top-left guard pixel of its 12x8 test rectangle:

```text
BLT_FAIL transfer/guard mismatch x=0 y=0
GOP_TEST_FAIL Compromised Data
```

The test places that rectangle at framebuffer coordinate `(32,32)`, so it is
intentionally near, but not at, the upper-left corner. Its purple outer guard
and small yellow/orange inner region match the test's programmed colors. That
visible pattern proves that `VideoFill` and `BufferToVideo` reached the active
framebuffer and scanout. It does not prove framebuffer readback: the failure
occurred in the following `VideoToBltBuffer` comparison.

The Shell-tested driver delegated the read to `FrameBufferBltLib`, which performs an
ordinary CPU memory copy from the physical BAR mapping. The shared hardware
library's independently working register, scratch, and color-bar paths use
`EFI_PCI_IO_PROTOCOL.Mem.Read/Write`. The next diagnostic driver therefore
routes framebuffer reads, and the read-dependent overlapping video copies,
through the same PCI I/O protocol. This is a source-derived diagnosis to be
confirmed by a new Shell run; this failed log is not GOP acceptance evidence.

The pinned DEBUG retry build completed successfully and was staged at
`build/gop/usb_payload/EFI/SimpleDisplay/` with these hashes:

```text
50c68de5bae9c57dbb44ce3279dece62d032f1b9e723ba6cd8c8c9f62a9f0aa1  SimpleDisplayBringup.efi
418be0d90072866f5bdc778a80eb56d2d212a0d053946ae22a9d926125bff3ef  SimpleDisplayGopDxe.efi
4c21b7fc8c6c0baa1bf0c09236e645087386a52d6ece2f3639579f602722a61a  SimpleDisplayGopTest.efi
```

Before refreshing the USB, the exact failing EFI files and their original
manifest were preserved under `tested_payload/`. After FAT repair, the host
mount was confirmed read-write, only the three EFI files and `SHA256SUMS` were
replaced, and direct USB readback produced the hashes above. The refreshed USB
was then cleanly unmounted with `udisksctl`.
