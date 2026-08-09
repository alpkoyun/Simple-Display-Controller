# Driver Lifecycle

## Lifecycle States

The driver separates parent management from child creation because UEFI may
call `Start()` first with an end node and later ask the bus driver to enumerate
the physical-output child.

| State | Parent marker | HDMI child | PCI memory decode |
|---|---:|---:|---:|
| Unbound | No | No | Entry platform state |
| Parent managed | Yes | No | Entry platform state |
| Child active, no mode selected | Yes | Yes | Enabled |
| Child active, mode selected | Yes | Yes | Enabled |
| Child stopped | Yes | No | Exactly restored |
| Fully stopped | No | No | Exactly restored |

## Image Entry

`SimpleDisplayGopEntryPoint()` calls
`EfiLibInstallDriverBindingComponentName2()` with the image handle as both the
driver image and Driver Binding handle. The installed protocol has version
`0x10` and points to `Supported()`, `Start()`, and `Stop()`.

Entry proves only that the EFI image executed and published Driver Binding. It
does not prove that a matching PCI controller exists or that a GOP child was
created.

## Supported

`SimpleDisplayDriverSupported()` performs only operations safe while PCI memory
decode may still be disabled:

1. Validate `RemainingDevicePath`. It accepts `NULL`, an end node, or the exact
   HDMI ACPI ADR node.
2. Open the parent's PCI I/O protocol `BY_DRIVER`.
3. If already started, read the private parent marker and distinguish a
   parent-only state from an existing child.
4. Otherwise call `SimpleDisplayHwMatchPci()`, which reads PCI configuration
   space and checks `10ee:7024` plus display base class.
5. Close the temporary `BY_DRIVER` PCI I/O open before returning.

`Supported()` does not call `GetBarAttributes()`, read SDC1, or touch BAR2.
That ordering is necessary for cold Option-ROM dispatch where PCI Command may
be `0x0000`.

## Start: Claim the Parent

On a new parent, `SimpleDisplayDriverStart()`:

1. Opens PCI I/O `BY_DRIVER`.
2. Opens the parent device path `BY_DRIVER`.
3. rechecks PCI identity and class.
4. Allocates and initializes `SIMPLE_DISPLAY_PRIVATE`.
5. Installs that private pointer on the controller with `gEfiCallerIdGuid`.

If `RemainingDevicePath` is an end node, `Start()` returns here. The parent is
managed but no GOP child or visible hardware state exists. A later call with
`NULL` or the HDMI node continues child creation using the retained private
state.

## Start: Enable BAR Access

Before the first BAR2 read, `Start()` preserves both abstract and raw PCI state:

1. Read the 16-bit PCI Command register at offset `0x04`.
2. Read the UEFI PCI I/O attribute state.
3. Reconcile the saved I/O, memory, and bus-master bits to the raw Command word.
4. Enable `EFI_PCI_IO_ATTRIBUTE_MEMORY`.
5. If firmware's attribute cache did not update the hardware bit, explicitly
   set `EFI_PCI_COMMAND_MEMORY_SPACE` in PCI Command and read it back.

Only then does `SimpleDisplayHwInitializeContext()` discover BAR2 and read the
SDC1 identity registers. This is the repaired cold-binding order.

## Start: Create the Physical-Output Child

After BAR2 and SDC1 validation, `Start()`:

1. Appends the HDMI ACPI ADR node to the parent device path.
2. Initializes GOP function pointers and mode information.
3. Sets `MaxMode=1`, `Mode=MAX_UINT32`, `PixelFormat=PixelBltOnly`, and zero
   framebuffer base/size.
4. Initializes EDID Discovered/Active with size zero and `NULL` data.
5. Installs device path, GOP, EDID Discovered, and EDID Active on a new child.
6. Opens the parent's PCI I/O protocol `BY_CHILD_CONTROLLER` for that child.

The successful return still has no selected hardware mode. The first consumer
`SetMode()` performs the visible pipeline initialization and clears the frame.

## SetMode and Operation

`SetMode()` validates the requested index and framebuffer bounds, calls the
shared hardware-programming sequence, publishes the selected mode, retains the
BLT-only zero-framebuffer contract, and clears the full visible area to black
through `Blt(EfiBltVideoFill)`.

After a mode is selected, `Blt()` serializes access at `TPL_NOTIFY` and uses
32-bit PCI I/O row transactions. There is no DMA, interrupt, event, or
background worker in the UEFI driver.

## Stop the Child

For one child handle, `SimpleDisplayDriverStop()`:

1. Obtains GOP from the proposed child and validates that it belongs to the
   requested parent.
2. Closes the `BY_CHILD_CONTROLLER` PCI I/O relationship.
3. Uninstalls child device path, GOP, and both EDID protocols.
4. If uninstall fails, attempts to reopen the child-controller relationship so
   ownership remains internally consistent.
5. Frees child resources.
6. Restores the saved PCI attributes and exact raw Command word.

The display pipeline is deliberately left programmed. The child and its APIs
are gone, but the last scanout may remain visible until another owner changes
hardware state or decoding becomes ineffective.

## Stop the Parent

For `NumberOfChildren=0`, `Stop()` requires that no child remains. It then:

1. retries exact PCI restoration if needed;
2. uninstalls the private parent marker;
3. closes the `BY_DRIVER` parent device-path and PCI I/O protocols; and
4. frees the private object.

The split stop sequence lets firmware disconnect the child first, inspect the
restored controller, and later release the parent. It also enables a targeted
reconnect to create a fresh child from the retained parent marker.

## Reconnect

After child stop, `Supported()` reports that the managed parent can create a
child again. A later `Start()` repeats state capture, memory-decode enable, SDC1
validation, child installation, and the `BY_CHILD_CONTROLLER` open. The new
child starts with no selected mode and must pass `SetMode()` again.

The sealed cold-bind evidence verifies bind, GOP tests, child disconnect,
byte-identical PCI Command restoration to `0x0000`, reconnect, and a second
GOP test while retaining the independent iGPU GOP.
