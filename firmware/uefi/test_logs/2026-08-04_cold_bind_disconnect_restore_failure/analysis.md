# Cold-bind disconnect restore failure

Status: `COLD_GOP_BIND_VALIDATION_FAIL`

This attempt used the USB-loaded driver with SHA-256
`818dde18bb07ff9642bc7128da86ef5b006cb6bb9a1bc1433278c757fef29f8a`.
The raw UEFI Shell logs and exact tested binaries are preserved without text
normalization in this directory.

Validated observations:

- the cold FPGA controller was `10ee:7024`, class `038000`, with a 32 KiB ROM;
- its pre-bind PCI Command was `0000`;
- targeted connection of the USB driver succeeded;
- the iGPU and FPGA GOP handles were both present;
- all GOP mode and BLT markers, including `GOP_TEST_PASS`, were present;
- targeted disconnect and reconnect both reported success; and
- reconnect created the FPGA GOP child again.

Blocking failure:

- the post-disconnect PCI Command was `0002`, not the required original value
  `0000`; Memory Space Enable therefore remained set after child removal.

The attempt is not eligible for `COLD_GOP_BIND_VALIDATION_PASS` and cannot be
used to package an accepted Option ROM. It also does not prove automatic
Option-ROM dispatch. The follow-up source repair saves the raw PCI Command
before enabling memory and restores and reads back that exact word after the
abstract `EFI_PCI_IO_PROTOCOL.Attributes()` restore.
