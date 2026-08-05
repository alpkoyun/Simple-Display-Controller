# 2026-08-03 GOP MMIO-read retry

The untouched Shell log is preserved at `raw/gop_test_2.log`.

- USB source: `/media/alpk/ALP/EFI/SimpleDisplay/gop_test_2.log`
- Raw size: 426 bytes
- Raw SHA-256: `3739ae189b6606f42a0b5e22d07efb50228eb8280b899f80dfe0e9ff0106a185`
- Detected encoding: BOM-marked UTF-16LE with CRLF line endings
- Tested GOP driver SHA-256: `418be0d90072866f5bdc778a80eb56d2d212a0d053946ae22a9d926125bff3ef`
- Tested GOP test SHA-256: `4c21b7fc8c6c0baa1bf0c09236e645087386a52d6ece2f3639579f602722a61a`

The retry again proves connected GOP discovery, both empty-EDID protocols,
mode 0 at 1280x720, and the mode tests. The PCI-I/O readback change advanced
the first mismatch from pixel `(0,0)` to `(1,0)` and reported:

```text
actual=0x00000000 expected=0x00C35AA5
```

Pixel 0 therefore matched the purple guard, while the immediately adjacent
pixel remained zero. This is consistent with the user's earlier observation
of only six narrow purple columns in the intended 12-pixel-wide guard region.
Inspection of the pinned EDK II `FrameBufferBltLib` shows that `VideoFill`
uses wide ordinary CPU stores and `BufferToVideo` uses `CopyMem` directly into
the PCI BAR. The first retry replaced framebuffer reads but retained those
write paths. The next diagnostic driver routes `VideoFill`, `BufferToVideo`,
the `SetMode()` clear, framebuffer reads, and overlapping copies through
explicit 32-bit `EFI_PCI_IO_PROTOCOL.Mem.Read/Write` operations.

This remains a failed GOP test and does not emit GOP acceptance evidence. The
exact tested EFI files and manifest are preserved under `tested_payload/`.

The all-PCI-I/O retry driver then built successfully with the pinned EDK II
checkout, `UEFI_CONTRACT_PASS`, all 301 BaseTools tests, and the Shell-log
encoding tests. The payload was copied to the FAT USB, read back with matching
hashes, and cleanly unmounted:

```text
50c68de5bae9c57dbb44ce3279dece62d032f1b9e723ba6cd8c8c9f62a9f0aa1  SimpleDisplayBringup.efi
dfc349b67d8bbbd4cad7fa0ae09c70a056c99c31855039c3ce5e7b66c6830eb9  SimpleDisplayGopDxe.efi
4c21b7fc8c6c0baa1bf0c09236e645087386a52d6ece2f3639579f602722a61a  SimpleDisplayGopTest.efi
073393834940c91ba7bff14572ce9faa03653499512b4f169d91fe37d1fd64e6  SHA256SUMS
```
