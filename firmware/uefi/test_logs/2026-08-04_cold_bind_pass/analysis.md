# Cold-state USB GOP binding, cleanup, and reconnect pass

Status: `COLD_GOP_BIND_VALIDATION_PASS`

This run used the USB-loaded driver with SHA-256
`76183ab80b31a3a9f519b97f5a24ef26bdb917856595e34bfd97b7c8c2a74117`.
The raw UEFI Shell logs and exact tested payload are preserved without text
normalization in this directory.

Validated observations:

- the cold FPGA controller was `10ee:7024`, class `038000`, with a 32 KiB ROM;
- its pre-bind PCI Command was `0000`;
- `load -nc` exposed the USB driver and targeted connection succeeded;
- separate iGPU and FPGA GOP handles appeared;
- the GOP test reported every required marker through `GOP_TEST_PASS`;
- the user observed the small rectangle test on the FPGA output and confirmed
  that the operations did not disturb the iGPU main screen;
- the user confirmed that `SimpleDisplayBringup.efi` was not run during this
  boot;
- targeted disconnect succeeded and restored PCI Command exactly to `0000`;
- targeted reconnect succeeded; and
- the FPGA GOP was recreated on a new child handle while the iGPU GOP remained.

This closes the exact USB-loaded cold bind/disconnect/reconnect prerequisite
for packaging this driver. It does not prove automatic Option-ROM binding,
because the run intentionally used manual `load -nc` and targeted `connect`.
That separate gate requires a rebuilt ROM and a cold Shell capture before any
manual `load` or `connect`.
