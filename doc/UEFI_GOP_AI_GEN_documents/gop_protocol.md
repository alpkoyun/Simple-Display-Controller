# GOP Protocol Contract

## Published Protocols

The HDMI child handle publishes four interfaces:

| Protocol | Contract |
|---|---|
| Device Path | Parent path plus one external-digital ACPI ADR node |
| Graphics Output | One fixed mode and all four standard BLT operations |
| EDID Discovered | Present with `SizeOfEdid=0`, `Edid=NULL` |
| EDID Active | Present with `SizeOfEdid=0`, `Edid=NULL` |

The empty EDID protocols are intentional. This hardware path has no usable,
publicly documented DDC/EDID read interface. Mode policy is therefore fixed by
the project rather than synthesized from monitor data.

## Mode Contract

The current GOP exports exactly one mode:

| Field | Value |
|---|---:|
| Mode number | `0` |
| Resolution | `1920 x 1080` |
| Refresh/timing | `1920x1080@60`, 148.5 MHz pixel clock |
| Pixel format | `PixelBltOnly` |
| Pixels per scan line | `1920` |
| Framebuffer base | `0` |
| Framebuffer size | `0` |

`PixelBltOnly` is not merely a performance choice. Firmware CPU loads and
stores did not preserve the width and ordering required by this PCI BAR. The
driver must mediate access through 32-bit `EFI_PCI_IO_PROTOCOL` transactions.
Publishing BAR2 as a linear framebuffer would tell consumers to bypass that
requirement and also invites Linux `simpledrm` to map it incorrectly.

The source mode table contains six timings, with 1080p first, but
`SIMPLE_DISPLAY_GOP_MODE_COUNT` is one. `MaxMode` therefore remains one and a
consumer cannot select the other table entries through GOP.

## Initial State

Driver Binding `Start()` publishes the GOP object with:

```text
MaxMode         = 1
Mode            = MAX_UINT32
Info            = mode-0 descriptive information
FrameBufferBase = 0
FrameBufferSize = 0
```

This means the protocol exists but no hardware mode is active on behalf of the
new child. `Blt()` returns `EFI_NOT_READY` until a successful `SetMode()`.

## QueryMode

`QueryMode()` requires non-`NULL` `This`, `SizeOfInfo`, and `Info`, and a mode
number below `MaxMode`. It allocates a new
`EFI_GRAPHICS_OUTPUT_MODE_INFORMATION` object that the caller owns.

| Condition | Result |
|---|---|
| Valid mode 0 | `EFI_SUCCESS` and allocated mode information |
| Null required pointer | `EFI_INVALID_PARAMETER` |
| Mode number at or above `MaxMode` | `EFI_INVALID_PARAMETER` |
| Allocation failure | `EFI_OUT_OF_RESOURCES` |

## SetMode

`SetMode(0)` performs the visible hardware transition:

1. Validate that the mode fits the project maximums and BAR2 frame window.
2. Program GPIO, pixel unpack, identity color conversion, HDMI I2C, pixel
   clock, VDMA MM2S, and VTC.
3. Publish the new GOP mode state while retaining `PixelBltOnly` and zero
   framebuffer fields.
4. Clear the complete frame to black through `EfiBltVideoFill`.

The black clear is part of successful mode selection. A hardware programming
or clear failure becomes `EFI_DEVICE_ERROR`. An invalid index returns
`EFI_UNSUPPORTED`.

## Blt Validation

All operations require a selected mode, nonzero width and height, and rectangles
fully contained in the active resolution. Coordinate tests use subtraction
forms that reject integer wraparound.

Buffer operations also validate `Delta` and pointer arithmetic:

- `Delta=0` is accepted only for a tightly packed buffer starting at buffer
  coordinate `(0,0)`.
- A nonzero `Delta` must cover the requested buffer X coordinate and width.
- Last-row calculations must fit in `UINTN`.
- A required `BltBuffer` must not be `NULL`.

Invalid requests return `EFI_INVALID_PARAMETER`; calls before `SetMode()`
return `EFI_NOT_READY`; allocation failures return `EFI_OUT_OF_RESOURCES`;
PCI I/O failures are propagated.

## EfiBltVideoFill

The driver allocates one row of `Width` pixels, fills it with the caller's
single color, and writes that row to every destination line. Each write is one
ordered 32-bit PCI I/O transaction per pixel across the row.

This path is also used by `SetMode()` to clear the full screen to black.

## EfiBltBufferToVideo

For each output row, the source address is:

```text
(UINT8 *)BltBuffer + (SourceY + row) * Delta + SourceX * sizeof(pixel)
```

The requested width is then written to the destination framebuffer row through
PCI I/O. The driver does not retain the caller's buffer after return.

## EfiBltVideoToBltBuffer

For each input row, the destination address is:

```text
(UINT8 *)BltBuffer + (DestinationY + row) * Delta
                    + DestinationX * sizeof(pixel)
```

The corresponding video row is read with 32-bit PCI I/O into that host buffer.
The operation is therefore available even though no linear framebuffer is
advertised.

## EfiBltVideoToVideo

The driver uses a one-row bounce buffer so no CPU mapping of BAR2 is required.
For vertical overlap, a destination below the source copies bottom-up; all
other cases copy top-down. Reading the complete source row before writing it
also preserves horizontal-overlap semantics.

## Serialization

`Blt()` raises task priority to `TPL_NOTIFY` around framebuffer traffic and
restores the original TPL before freeing temporary storage and returning. This
serializes firmware-side BLT access in the current Boot Services environment.
The driver has no asynchronous completion, interrupt handler, DMA descriptor,
or separate lock.

## Pixel Representation

`EFI_GRAPHICS_OUTPUT_BLT_PIXEL` stores blue, green, red, and reserved bytes.
The resulting 32-bit word is compatible with the project's XRGB8888 storage:
the FPGA ignores the high X/reserved byte and sends the low 24 RGB bits through
pixel unpack and identity color conversion.

## Required Acceptance

The protocol test requires all of the following before printing
`GOP_TEST_PASS`:

- the correct child can be distinguished from the iGPU GOP;
- empty EDID protocols are installed;
- mode 0 reports 1920x1080, `PixelBltOnly`, and 1920 pixels per scan line;
- invalid mode queries and sets return their required errors;
- fill, buffer transfer, readback, and guard pixels match exactly;
- upward and downward overlapping copies match expected memory semantics; and
- invalid BLT shapes, coordinates, buffers, and layouts are rejected.
