# Error Paths

## Error-Handling Principles

The driver follows four rules:

1. Reject incompatible paths and PCI identities before touching BAR2.
2. Save ownership and PCI state before enabling resources.
3. Unwind in the reverse order of acquisition.
4. If cleanup itself fails, retain enough state for `Stop()` to retry rather
   than hiding live protocols or modified PCI state.

## Supported Errors

| Condition | Result |
|---|---|
| Unsupported remaining device path | `EFI_UNSUPPORTED` |
| PCI I/O cannot be opened | Firmware-provided open error |
| Controller already fully started | `EFI_ALREADY_STARTED` |
| Managed parent exists but child is absent and requested | `EFI_SUCCESS` |
| Vendor/device/base class mismatch | `EFI_UNSUPPORTED` |

`Supported()` always closes a newly acquired temporary `BY_DRIVER` PCI I/O
open. It does not enable memory or require SDC1.

## Parent-Start Failures

On a newly bound parent, failures can occur while opening the device path,
matching PCI identity, allocating private state, or installing the private
marker. Cleanup closes only resources that were actually acquired.

If the private marker is installed and a later child-start step fails, the
driver first restores PCI state and frees child resources. For a newly claimed
parent it then uninstalls the marker, frees private state, and closes both
`BY_DRIVER` opens. For an existing parent it retains the marker and returns to
the parent-managed state.

## PCI Memory-Decode Failures

Child start can fail while reading the original Command word, reading abstract
attributes, enabling `EFI_PCI_IO_ATTRIBUTE_MEMORY`, writing the raw Command
bit, or verifying it. The driver records snapshot-valid flags before changing
hardware so the shared failure path can restore both representations.

`RestorePciAttributes()` first sets the original abstract attributes and then
writes the exact original raw Command. It reads Command back and returns
`EFI_DEVICE_ERROR` if the hardware value differs.

If restoration fails, the driver retains the parent marker and protocol opens.
This makes the modified state visible and gives `Stop()` a defined retry path.

## Hardware-Context Failures

`SimpleDisplayHwInitializeContext()` rejects:

- a missing or malformed BAR2 descriptor;
- a non-memory BAR;
- a BAR shorter than 64 MiB;
- failed 32-bit SDC1 reads;
- wrong magic or ABI version;
- missing direct-DDR or Option-ROM features; and
- a ROM aperture other than the accepted compatibility values.

On failure it frees the BAR descriptor and zeroes the hardware context. Driver
Binding then restores PCI state and unwinds the child attempt.

## Child-Installation Failures

Failure to allocate the appended device path returns
`EFI_OUT_OF_RESOURCES`. Failure to install child protocols returns the
firmware error and clears the uncreated child handle.

If child protocols install but the `BY_CHILD_CONTROLLER` open fails, the
driver attempts to uninstall all child protocols. If uninstall also fails, it
returns that cleanup error while retaining the child and parent state so
`Stop()` can retry. It must not free the protocol backing objects while the
protocols remain installed.

## SetMode Failures

| Failure | External result |
|---|---|
| Invalid mode number | `EFI_UNSUPPORTED` |
| Frame does not fit bounds/BAR | `EFI_DEVICE_ERROR` |
| GPIO/unpack/color/IIC programming error | `EFI_DEVICE_ERROR` |
| Clock load or lock timeout | `EFI_DEVICE_ERROR` |
| VDMA reset/start/status error | `EFI_DEVICE_ERROR` |
| VTC programming or nonzero error status | `EFI_DEVICE_ERROR` |
| Full-screen black clear fails | `EFI_DEVICE_ERROR` |

Mode state is published only after hardware programming succeeds. A failure
during the final clear can leave the pipeline programmed even though the
method reports an error; diagnose the earliest hardware or PCI I/O failure.

## Blt Errors

`Blt()` rejects use before a selected mode with `EFI_NOT_READY`. Invalid
operations, zero dimensions, out-of-bounds rectangles, arithmetic overflow,
missing buffers, and inconsistent `Delta` return `EFI_INVALID_PARAMETER`.

Temporary-row allocation failure returns `EFI_OUT_OF_RESOURCES`. PCI I/O read
and write failures are propagated after TPL restoration and buffer cleanup.
There is no partially-completed rollback of pixel writes; callers can retry or
redraw the affected rectangle.

## Stop Errors

Parent stop rejects an attempt while a child still exists. Child stop rejects
the wrong number of children, a missing buffer, a non-GOP handle, or a child
whose private parent does not match the requested controller.

If closing the child-controller relationship fails, protocols remain installed
and stop returns. If protocol uninstall fails after close, the driver attempts
to reopen the relationship. It reports the uninstall error even if reopen also
fails, while the flag records whether the relationship was restored.

After successful protocol removal, PCI restoration errors are returned and
the parent marker remains so a later full stop can retry.

## ROM and Boot Errors

| Symptom | First interpretation |
|---|---|
| No PCI function | FPGA configuration/link/enumeration problem |
| Revision `ff` after JTAG | Stale host enumeration; reset before diagnosis |
| ROM BAR exists but read fails | ROM enable/state or BAR6 transport problem |
| ROM bytes differ | Packaging/deployment/RTL initialization mismatch |
| ROM exact but no loaded image | PCI EFI header/platform dispatch problem |
| Driver Binding exists but connect fails | `Supported()`/`Start()`/ownership problem |
| Child exists but `Blt()` is not ready | Consumer has not called `SetMode()` |
| GOP tests pass only after bring-up app | Driver failed to establish its own prerequisites |
| Firmware output works but Linux fails | Handoff, simpledrm, PCI binding, or DRM problem |

Keep the raw observation and failed layer in evidence. Do not relabel an exact
ROM read as EFI execution or a dispatched image as GOP success.
