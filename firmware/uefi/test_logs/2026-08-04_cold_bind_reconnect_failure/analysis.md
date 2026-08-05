# Cold-bind reconnect failure after exact Command restore

Status: `COLD_GOP_BIND_VALIDATION_FAIL`

This attempt used the USB-loaded driver with SHA-256
`4acd6244b54643d2de37f85ca65396da95ee1bbc27a153be1df6302211d794a8`.
The raw UEFI Shell logs and exact tested binaries are preserved without text
normalization in this directory.

Validated observations:

- the cold FPGA controller was `10ee:7024`, class `038000`, with a 32 KiB ROM;
- its pre-bind PCI Command was `0000`;
- targeted connection succeeded, two GOP handles appeared, and
  `GOP_TEST_PASS` was present;
- targeted disconnect succeeded; and
- the post-disconnect PCI Command was restored exactly to `0000`.

Blocking failure:

- targeted reconnect returned `Not Found` and no FPGA GOP child reappeared;
- an additional full-controller disconnect preserved Command `0000`, but a
  second targeted reconnect also returned `Not Found`.

The attempt is not eligible for `COLD_GOP_BIND_VALIDATION_PASS` and does not
prove automatic Option-ROM dispatch. The follow-up repair verifies raw Memory
Space Enable after the abstract attribute-enable call and directly sets and
reads back that Command bit if firmware cache state prevents the abstract call
from changing the hardware register.
