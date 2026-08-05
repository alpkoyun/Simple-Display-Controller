# 2026-08-03 manual GOP test pass

The untouched Shell log is preserved at `raw/gop_test_3.log`.

- USB source: `/media/alpk/ALP/EFI/SimpleDisplay/gop_test_3.log`
- Raw size: 185 bytes
- Raw SHA-256: `4678a11625457f473fbe1784f7a288761759996bafd82191b9e2119952b17987`
- Detected encoding: ASCII/UTF-8 with CRLF line endings
- Tested GOP driver SHA-256: `dfc349b67d8bbbd4cad7fa0ae09c70a056c99c31855039c3ce5e7b66c6830eb9`
- Tested GOP test SHA-256: `4c21b7fc8c6c0baa1bf0c09236e645087386a52d6ece2f3639579f602722a61a`

The log contains every required GOP marker and no failure marker:

```text
EDID_EMPTY_PASS discovered=0/0 active=0/0
MODE_INFO 0 1280x720 format=1 ppsl=1280
MODE_TEST_PASS
BLT_BUFFER_TRANSFER_PASS
BLT_OVERLAP_PASS
BLT_INVALID_REQUEST_PASS
GOP_TEST_PASS
```

This proves that the manually Shell-loaded and connected driver exposes the
expected empty EDID protocols and fixed 1280x720 mode, and that its buffer
transfer, overlapping video copy, and invalid-request BLT behavior passed on
the FPGA hardware. The exact tested EFI files and manifest are preserved under
`tested_payload/`; the driver is byte-identical to the current workspace
artifact.

The earlier raw bring-up capture at
`../2026-08-03_shell_load_nc/raw/uefi-shell.log` independently contains all
required markers through `BRINGUP_PASS`; its raw SHA-256 is
`ac26dc212ebbe7bbb4dd801062582e3945f72c3833ba22ad00cb81585fa2b86b`.
The bring-up application binary is unchanged. The two raw logs must remain
separate evidence inputs rather than being rewritten into a synthetic Shell
log.

This result is manual Shell GOP validation. It does not prove automatic EFI
Option-ROM dispatch, because the deployed 4 KiB ROM remains the non-executable
code-type-`0xff` transport image. The final evidence record therefore required the
user's explicit confirmation of stable correct visible output during bring-up
and that targeted driver binding itself did not change the displays before it
can be sealed.

The user subsequently confirmed both observations: the broad horizontal
bring-up bars were stable and correct, targeted binding did not disturb the
iGPU-hosted UEFI Shell screen, and the FPGA display remained a graphics-only
output showing the bring-up and GOP-test framebuffer patterns rather than the
Shell console. That is the expected two-display topology.

The split-log evidence recorder pins both untouched logs and the exact tested
driver. Its independent checker reports:

```text
SHELL_BRINGUP_LOG_SHA256=ac26dc212ebbe7bbb4dd801062582e3945f72c3833ba22ad00cb81585fa2b86b
SHELL_GOP_LOG_SHA256=4678a11625457f473fbe1784f7a288761759996bafd82191b9e2119952b17987
SHELL_TESTED_DRIVER_SHA256=dfc349b67d8bbbd4cad7fa0ae09c70a056c99c31855039c3ce5e7b66c6830eb9
SHELL_GOP_VALIDATION_PASS
```

The sealed record is `build/gop/evidence/SHELL_GOP_VALIDATION_PASS.json`.
