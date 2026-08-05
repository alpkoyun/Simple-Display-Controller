# Problems Encountered

This document retains reusable engineering lessons. Dated logs and artifact
hashes remain in the development/evidence trees; the descriptions here focus
on cause, repair, and regression rule.

## BAR2 Was Read Before PCI Memory Decode

### Symptom

Firmware automatically dispatched the embedded EFI image and exposed Driver
Binding, but targeted `connect` returned `Not Found`. The controller's PCI
Command was `0x0000`, and no FPGA GOP child appeared.

### Cause

The original `Supported()`/`Start()` path initialized the hardware context and
read SDC1 through BAR2 before enabling PCI memory-space decoding. Automatic
dispatch began from a colder controller state than prior manual tests.

### Repair

`Supported()` now performs only PCI configuration-space matching. `Start()`
saves abstract attributes and the raw Command word, enables and verifies memory
decode, and only then calls `SimpleDisplayHwInitializeContext()`.

### Regression Rule

The cold-bind gate must not run `SimpleDisplayBringup.efi`. Disconnect must
restore PCI Command exactly, and reconnect must pass again.

## Ordinary CPU Framebuffer Stores Corrupted BLTs

### Symptom

Early GOP tests could read pixel 0 but subsequent pixels were zero or partially
updated. Changing readback alone did not fix fills and transfers.

### Cause

The PCI framebuffer window requires ordered 32-bit transactions. Ordinary CPU
loads/stores and generic memory-copy behavior did not preserve that contract.

### Repair

All register, framebuffer fill, buffer transfer, readback, and overlap traffic
uses `EFI_PCI_IO_PROTOCOL.Mem.Read/Write` with
`EfiPciIoWidthUint32`. Video-to-video copies use a PCI-I/O row bounce buffer.

### Regression Rule

Keep exact buffer/readback, guard-pixel, and upward/downward overlap tests.
Reject code that directly dereferences BAR2.

## Linear GOP Framebuffer Broke Linux Handoff

### Symptom

Automatic firmware and GRUB output worked, but Linux failed during boot.
Suppressing `simpledrm_platform_driver_init` restored Ubuntu and isolated a
`simple-framebuffer` object below the FPGA PCI device.

### Cause

The GOP advertised a framebuffer base/size even though the PCI storage could
not be safely treated as normal linear CPU memory. Linux simpledrm consumed
that advertisement before `fpga_drm` could take over.

### Repair

The driver now advertises `PixelBltOnly`, `FrameBufferBase=0`, and
`FrameBufferSize=0` while retaining the validated custom PCI-I/O BLT path.

### Regression Rule

The source contract, GOP test, cold evidence, and packaged exact driver must
all require the BLT-only zero-framebuffer fields. Linux must boot normally
without suppressing simpledrm globally.

## Manual Loading Was Mistaken for Automatic ROM Success

### Symptom

A USB-loaded driver bound and passed `GOP_TEST_PASS`, leading to an overly
strong conclusion about the Option ROM.

### Cause

`load SimpleDisplayGopDxe.efi` bypasses ROM discovery, PCI EFI image parsing,
and automatic dispatch.

### Repair

Manual binding became the exact-driver packaging gate. Automatic validation
uses no USB `load`, no manual `connect`, and no bring-up application.

### Regression Rule

Always record whether the driver came from USB or the controller's
BusSpecificDriverOverride/ROM path.

## EFI Dispatch Was Mistaken for GOP Binding

### Symptom

The embedded image had `LoadedImage`, `EfiBootServicesCode`, Driver Binding,
and an association with `10ee:7024`, but no FPGA GOP child existed.

### Cause

Image execution and Driver Binding publication occur before `Supported()` and
`Start()` successfully create a child.

### Repair

The acceptance model now separates ROM transport, dispatch, binding, child
installation, GOP tests, and visible output.

### Regression Rule

Do not call dispatch a GOP pass. Require the child and consumer-visible result.

## ROM Enumeration Was Mistaken for ROM Identity

### Symptom

PCI configuration displayed an Expansion ROM BAR and plausible size, but the
result was described as though the intended image had been deployed.

### Cause

An aperture does not verify the bytes behind it, and a valid-looking image does
not prove it matches the tested driver.

### Repair

The Linux readback helper performs three complete byte comparisons and hashes,
then restores the exact PCI and driver state. Packaging separately verifies
that the embedded driver is byte-exact.

### Regression Rule

Record aperture proof and byte-identity proof as different rows.

## Live JTAG Programming Produced Revision FF

### Symptom

Immediately after programming a new endpoint image, Linux reported revision
`ff` or could not find the expected BDF.

### Cause

Replacing PCIe endpoint logic does not force the host to retrain and
reenumerate the device.

### Repair

Use a reboot or complete power cycle after JTAG before ROM or endpoint
validation. Use SPI cold boot for the persistent acceptance path.

### Regression Rule

Reject revision `ff` as stale enumeration rather than interpreting it as
successful or failed new hardware behavior.

## Historical 4 KiB Checkpoints Drifted into Current Docs

### Symptom

Transport-only, SDC1 Shell-stage, 1280x720, and current EFI/GOP descriptions
appeared together without a clear active configuration.

### Cause

Development documents correctly retained milestones, but their wording was
later read as a single current design.

### Repair

The structural docs define the fixed 32 KiB, 1920x1080 `PixelBltOnly` design as
current. Older configurations remain dated evidence or Git checkpoints.

### Regression Rule

Git selects hardware versions. Do not add a runtime stage variable or describe
multiple generated Vivado projects as simultaneous sources of truth.

## Direct BAR, H2C, and DRM Evidence Were Conflated

### Symptom

Successful direct BAR2 frame writes or ROM transport were used to imply the
normal XDMA H2C or Linux DRM path had passed.

### Cause

The paths share a PCI function and DDR pipeline but use different request,
upload, ownership, and validation mechanisms.

### Repair

Keep direct BAR2/VDMA scanout, ROM BAR6 transport, UEFI GOP, XDMA H2C, and
Linux `fpga_drm` as independent gates.

### Regression Rule

Every status statement must name the exact path, tested artifact, and evidence
type. A passing adjacent path is context, not proof.
