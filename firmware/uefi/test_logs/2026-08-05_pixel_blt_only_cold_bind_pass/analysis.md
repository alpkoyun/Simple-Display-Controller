# PixelBltOnly cold bind, cleanup, and reconnect pass

Status: `COLD_GOP_BIND_VALIDATION_PASS`

The preserved logs collectively cover repeated cold Shell runs of the same
byte-exact payload. The driver SHA-256 is
`cb9cf66ddbc3762d1261c7153e96a5f1a5f151b74b485f9ddcfe705177bb8fbe`;
the GOP test SHA-256 is
`656851b2d143029842ec09df1f4c05359b36696f03386399836ff2e39de8df91`.

Validated observations:

- the FPGA controller was `10ee:7024`, class `038000`, with the deliberate
  4 KiB shell-stage ROM;
- its pre-bind PCI Command was `0000`;
- the USB driver loaded and targeted connection succeeded;
- separate iGPU and FPGA GOP handles appeared;
- the GOP reported `PixelBltOnly` and every required test ended in
  `GOP_TEST_PASS`;
- the user observed the rectangle on the FPGA-connected display;
- targeted disconnect succeeded and removed the FPGA GOP child;
- the post-disconnect controller dump was byte-identical to the pre-bind dump,
  proving exact PCI Command restoration to `0000`;
- targeted reconnect succeeded, retained the iGPU GOP, and created a new FPGA
  GOP child; and
- the full GOP test passed again after reconnect.

The machine-readable record is `COLD_GOP_BIND_VALIDATION_PASS.json`. The
additional `raw/post_reconnect_gop.log` preserves the stronger functional test
after recreation of the FPGA GOP child.

This authorizes packaging the exact tested driver. It remains manual USB-load
evidence and does not prove automatic Option-ROM dispatch or GOP installation.
