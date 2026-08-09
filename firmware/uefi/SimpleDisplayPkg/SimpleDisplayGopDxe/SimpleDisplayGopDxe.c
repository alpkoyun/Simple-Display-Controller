/** @file
  Fixed-mode UEFI GOP driver for the Simple Display Controller.

  Driver Binding Start only discovers and publishes protocols. Visible pipeline
  changes occur in SetMode, and the EDID protocols intentionally contain no
  data because this HDMI path has no usable EDID/DDC interface.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "SimpleDisplay.h"

#include <IndustryStandard/Pci.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

STATIC CONST ACPI_ADR_DEVICE_PATH  mHdmiDevicePathNode = {
  {
    ACPI_DEVICE_PATH,
    ACPI_ADR_DP,
    { sizeof (ACPI_ADR_DEVICE_PATH), 0 }
  },
  ACPI_DISPLAY_ADR (
    1,
    0,
    1,
    0,
    0,
    ACPI_ADR_DISPLAY_TYPE_EXTERNAL_DIGITAL,
    0,
    0
    )
};

STATIC
VOID
MakeModeInfo (
  IN  UINT32                                ModeNumber,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info
  )
{
  CONST SIMPLE_DISPLAY_MODE  *Mode;

  Mode = &gSimpleDisplayModes[ModeNumber];
  ZeroMem (Info, sizeof (*Info));
  Info->Version              = 0;
  Info->HorizontalResolution = Mode->HorizontalActive;
  Info->VerticalResolution   = Mode->VerticalActive;
  // BAR2 requires ordered 32-bit PCI I/O transactions. Do not advertise it
  // as an ordinary CPU-accessible linear framebuffer.
  Info->PixelFormat          = PixelBltOnly;
  Info->PixelsPerScanLine    = Mode->HorizontalActive;
}

STATIC
EFI_STATUS
EFIAPI
SimpleDisplayQueryMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
  IN  UINT32                                ModeNumber,
  OUT UINTN                                 *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  **Info
  )
{
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  ModeInfo;

  if ((This == NULL) || (SizeOfInfo == NULL) || (Info == NULL) ||
      (ModeNumber >= SIMPLE_DISPLAY_GOP_MODE_COUNT))
  {
    return EFI_INVALID_PARAMETER;
  }

  MakeModeInfo (ModeNumber, &ModeInfo);
  *Info = AllocateCopyPool (sizeof (ModeInfo), &ModeInfo);
  if (*Info == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  *SizeOfInfo = sizeof (ModeInfo);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
SimpleDisplaySetMode (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *This,
  IN UINT32                        ModeNumber
  )
{
  EFI_STATUS                            Status;
  SIMPLE_DISPLAY_PRIVATE                *Private;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  NewInfo;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL         Black;

  if ((This == NULL) || (ModeNumber >= SIMPLE_DISPLAY_GOP_MODE_COUNT)) {
    return EFI_UNSUPPORTED;
  }

  Private = SIMPLE_DISPLAY_PRIVATE_FROM_GOP (This);
  Status  = SimpleDisplayHwValidateModeBounds (
              &Private->Hardware,
              &gSimpleDisplayModes[ModeNumber]
              );
  if (EFI_ERROR (Status)) {
    return EFI_DEVICE_ERROR;
  }

  MakeModeInfo (ModeNumber, &NewInfo);
  Status = SimpleDisplayHwProgramMode (
             &Private->Hardware,
             &gSimpleDisplayModes[ModeNumber]
             );
  if (EFI_ERROR (Status)) {
    return EFI_DEVICE_ERROR;
  }

  CopyMem (&Private->GopInfo, &NewInfo, sizeof (NewInfo));
  Private->GopMode.Mode            = ModeNumber;
  Private->GopMode.Info            = &Private->GopInfo;
  Private->GopMode.SizeOfInfo      = sizeof (Private->GopInfo);
  Private->GopMode.FrameBufferBase = 0;
  Private->GopMode.FrameBufferSize = 0;

  ZeroMem (&Black, sizeof (Black));
  Status = This->Blt (
                   This,
                   &Black,
                   EfiBltVideoFill,
                   0,
                   0,
                   0,
                   0,
                   NewInfo.HorizontalResolution,
                   NewInfo.VerticalResolution,
                   0
                   );
  return EFI_ERROR (Status) ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
ReadFrameBufferRow (
  IN  SIMPLE_DISPLAY_PRIVATE            *Private,
  IN  UINTN                             X,
  IN  UINTN                             Y,
  IN  UINTN                             Width,
  OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL     *Buffer
  )
{
  UINT64  Offset;
  UINT64  RowBytes;

  if ((Private == NULL) || (Private->PciIo == NULL) || (Buffer == NULL) ||
      (Width == 0))
  {
    return EFI_INVALID_PARAMETER;
  }

  RowBytes = (UINT64)Width * sizeof (*Buffer);
  Offset   = SIMPLE_DISPLAY_FRAMEBUFFER_OFFSET +
             (((UINT64)Y * Private->GopInfo.PixelsPerScanLine) + X) *
             sizeof (*Buffer);
  if ((RowBytes > Private->Hardware.BarLength) ||
      (Offset > Private->Hardware.BarLength - RowBytes))
  {
    return EFI_INVALID_PARAMETER;
  }

  // Use PciIo so the access retains the ordering and 32-bit width semantics
  // required by this BAR. The GOP exposes this storage only through Blt().
  return Private->PciIo->Mem.Read (
                                Private->PciIo,
                                EfiPciIoWidthUint32,
                                (UINT8)Private->Hardware.BarIndex,
                                Offset,
                                Width,
                                Buffer
                                );
}

STATIC
EFI_STATUS
EFIAPI
WriteFrameBufferRow (
  IN SIMPLE_DISPLAY_PRIVATE                *Private,
  IN UINTN                                 X,
  IN UINTN                                 Y,
  IN UINTN                                 Width,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL         *Buffer
  )
{
  UINT64  Offset;
  UINT64  RowBytes;

  if ((Private == NULL) || (Private->PciIo == NULL) || (Buffer == NULL) ||
      (Width == 0))
  {
    return EFI_INVALID_PARAMETER;
  }

  RowBytes = (UINT64)Width * sizeof (*Buffer);
  Offset   = SIMPLE_DISPLAY_FRAMEBUFFER_OFFSET +
             (((UINT64)Y * Private->GopInfo.PixelsPerScanLine) + X) *
             sizeof (*Buffer);
  if ((RowBytes > Private->Hardware.BarLength) ||
      (Offset > Private->Hardware.BarLength - RowBytes))
  {
    return EFI_INVALID_PARAMETER;
  }

  return Private->PciIo->Mem.Write (
                                 Private->PciIo,
                                 EfiPciIoWidthUint32,
                                 (UINT8)Private->Hardware.BarIndex,
                                 Offset,
                                 Width,
                                 Buffer
                                 );
}

STATIC
EFI_STATUS
EFIAPI
SimpleDisplayBlt (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL       *This,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL      *BltBuffer OPTIONAL,
  IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION  BltOperation,
  IN UINTN                              SourceX,
  IN UINTN                              SourceY,
  IN UINTN                              DestinationX,
  IN UINTN                              DestinationY,
  IN UINTN                              Width,
  IN UINTN                              Height,
  IN UINTN                              Delta OPTIONAL
  )
{
  SIMPLE_DISPLAY_PRIVATE  *Private;
  EFI_STATUS              Status;
  EFI_TPL                 OriginalTpl;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *RowBuffer;
  UINTN                           Row;

  #define RECT_FITS(Start, Length, Limit) \
    (((Length) != 0) && ((Start) <= (Limit)) && ((Length) <= (Limit) - (Start)))

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Private = SIMPLE_DISPLAY_PRIVATE_FROM_GOP (This);
  if ((Private->GopMode.Mode == MAX_UINT32) ||
      (Private->GopMode.Mode >= Private->GopMode.MaxMode))
  {
    return EFI_NOT_READY;
  }

  if (BltOperation >= EfiGraphicsOutputBltOperationMax) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Width == 0) || (Height == 0)) {
    return EFI_INVALID_PARAMETER;
  }

  switch (BltOperation) {
    case EfiBltVideoFill:
      if ((BltBuffer == NULL) ||
          !RECT_FITS (DestinationX, Width, Private->GopInfo.HorizontalResolution) ||
          !RECT_FITS (DestinationY, Height, Private->GopInfo.VerticalResolution))
      {
        return EFI_INVALID_PARAMETER;
      }

      break;

    case EfiBltVideoToBltBuffer:
      if ((BltBuffer == NULL) ||
          !RECT_FITS (SourceX, Width, Private->GopInfo.HorizontalResolution) ||
          !RECT_FITS (SourceY, Height, Private->GopInfo.VerticalResolution))
      {
        return EFI_INVALID_PARAMETER;
      }

      break;

    case EfiBltBufferToVideo:
      if ((BltBuffer == NULL) ||
          !RECT_FITS (DestinationX, Width, Private->GopInfo.HorizontalResolution) ||
          !RECT_FITS (DestinationY, Height, Private->GopInfo.VerticalResolution))
      {
        return EFI_INVALID_PARAMETER;
      }

      break;

    case EfiBltVideoToVideo:
      if (!RECT_FITS (SourceX, Width, Private->GopInfo.HorizontalResolution) ||
          !RECT_FITS (SourceY, Height, Private->GopInfo.VerticalResolution) ||
          !RECT_FITS (DestinationX, Width, Private->GopInfo.HorizontalResolution) ||
          !RECT_FITS (DestinationY, Height, Private->GopInfo.VerticalResolution))
      {
        return EFI_INVALID_PARAMETER;
      }

      break;

    default:
      return EFI_INVALID_PARAMETER;
  }

  // Reject buffer layouts that could overflow pointer arithmetic before using
  // the PCI I/O row operations. Delta zero is safe only for a tightly packed
  // origin-based buffer.
  if ((BltOperation == EfiBltVideoToBltBuffer) ||
      (BltOperation == EfiBltBufferToVideo))
  {
    UINTN  BufferX;
    UINTN  BufferY;
    UINTN  RowPixels;
    UINTN  RowBytes;
    UINTN  LastRow;

    BufferX = (BltOperation == EfiBltVideoToBltBuffer) ? DestinationX : SourceX;
    BufferY = (BltOperation == EfiBltVideoToBltBuffer) ? DestinationY : SourceY;
    if ((BufferX > MAX_UINTN - Width) ||
        ((BufferX + Width) > MAX_UINTN / sizeof (*BltBuffer)))
    {
      return EFI_INVALID_PARAMETER;
    }

    RowPixels = BufferX + Width;
    RowBytes  = RowPixels * sizeof (*BltBuffer);
    if (Delta == 0) {
      if ((BufferX != 0) || (BufferY != 0)) {
        return EFI_INVALID_PARAMETER;
      }

      Delta = RowBytes;
    } else if (Delta < RowBytes) {
      return EFI_INVALID_PARAMETER;
    }

    if ((BufferY > MAX_UINTN - (Height - 1)) ||
        ((BufferY + Height - 1) > MAX_UINTN / Delta))
    {
      return EFI_INVALID_PARAMETER;
    }

    LastRow = (BufferY + Height - 1) * Delta;
    if (LastRow > MAX_UINTN - RowBytes) {
      return EFI_INVALID_PARAMETER;
    }
  }

  RowBuffer = NULL;
  if ((BltOperation == EfiBltVideoFill) ||
      (BltOperation == EfiBltVideoToVideo))
  {
    if (Width > MAX_UINTN / sizeof (*RowBuffer)) {
      return EFI_INVALID_PARAMETER;
    }

    RowBuffer = AllocatePool (Width * sizeof (*RowBuffer));
    if (RowBuffer == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
  }

  OriginalTpl = gBS->RaiseTPL (TPL_NOTIFY);
  if (BltOperation == EfiBltVideoFill) {
    UINTN  Pixel;

    for (Pixel = 0; Pixel < Width; Pixel++) {
      RowBuffer[Pixel] = *BltBuffer;
    }

    Status = EFI_SUCCESS;
    for (Row = 0; Row < Height; Row++) {
      Status = WriteFrameBufferRow (
                 Private,
                 DestinationX,
                 DestinationY + Row,
                 Width,
                 RowBuffer
                 );
      if (EFI_ERROR (Status)) {
        break;
      }
    }
  } else if (BltOperation == EfiBltVideoToVideo) {
    Status = EFI_SUCCESS;
    // A one-row PCI-I/O bounce buffer preserves horizontal and vertical
    // overlap semantics without ordinary CPU reads from the PCI BAR.
    for (Row = 0; Row < Height; Row++) {
      UINTN  CopyRow;

      CopyRow = (DestinationY > SourceY) ? (Height - Row - 1) : Row;
      Status  = ReadFrameBufferRow (
                  Private,
                  SourceX,
                  SourceY + CopyRow,
                  Width,
                  RowBuffer
                  );
      if (EFI_ERROR (Status)) {
        break;
      }

      Status = WriteFrameBufferRow (
                 Private,
                 DestinationX,
                 DestinationY + CopyRow,
                 Width,
                 RowBuffer
                 );
      if (EFI_ERROR (Status)) {
        break;
      }
    }
  } else if (BltOperation == EfiBltVideoToBltBuffer) {
    Status = EFI_SUCCESS;
    for (Row = 0; Row < Height; Row++) {
      EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *DestinationRow;

      DestinationRow = (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)(
                         (UINT8 *)BltBuffer +
                         ((DestinationY + Row) * Delta) +
                         (DestinationX * sizeof (*BltBuffer))
                         );
      Status = ReadFrameBufferRow (
                 Private,
                 SourceX,
                 SourceY + Row,
                 Width,
                 DestinationRow
                 );
      if (EFI_ERROR (Status)) {
        break;
      }
    }
  } else {
    Status = EFI_SUCCESS;
    for (Row = 0; Row < Height; Row++) {
      EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *SourceRow;

      SourceRow = (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)(
                    (UINT8 *)BltBuffer +
                    ((SourceY + Row) * Delta) +
                    (SourceX * sizeof (*BltBuffer))
                    );
      Status = WriteFrameBufferRow (
                 Private,
                 DestinationX,
                 DestinationY + Row,
                 Width,
                 SourceRow
                 );
      if (EFI_ERROR (Status)) {
        break;
      }
    }
  }

  gBS->RestoreTPL (OriginalTpl);
  if (RowBuffer != NULL) {
    FreePool (RowBuffer);
  }

  #undef RECT_FITS

  return EFI_ERROR (Status) ? Status : EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
SimpleDisplayDriverSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath OPTIONAL
  )
{
  EFI_STATUS              Status;
  EFI_PCI_IO_PROTOCOL     *PciIo;
  SIMPLE_DISPLAY_PRIVATE  *Private;

  if ((RemainingDevicePath != NULL) &&
      !IsDevicePathEnd (RemainingDevicePath) &&
      ((DevicePathNodeLength (RemainingDevicePath) != sizeof (mHdmiDevicePathNode)) ||
       (CompareMem (RemainingDevicePath, &mHdmiDevicePathNode, sizeof (mHdmiDevicePathNode)) != 0)))
  {
    return EFI_UNSUPPORTED;
  }

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiPciIoProtocolGuid,
                  (VOID **)&PciIo,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (Status == EFI_ALREADY_STARTED) {
    Status = gBS->OpenProtocol (
                    ControllerHandle,
                    &gEfiCallerIdGuid,
                    (VOID **)&Private,
                    This->DriverBindingHandle,
                    ControllerHandle,
                    EFI_OPEN_PROTOCOL_GET_PROTOCOL
                    );
    if (EFI_ERROR (Status) ||
        (Private == NULL) ||
        (Private->Signature != SIMPLE_DISPLAY_PRIVATE_SIGNATURE) ||
        (Private->ControllerHandle != ControllerHandle))
    {
      return EFI_ALREADY_STARTED;
    }

    if ((Private->ChildHandle == NULL) &&
        ((RemainingDevicePath == NULL) || !IsDevicePathEnd (RemainingDevicePath)))
    {
      return EFI_SUCCESS;
    }

    return EFI_ALREADY_STARTED;
  }

  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = SimpleDisplayHwMatchPci (PciIo, NULL);
  gBS->CloseProtocol (
         ControllerHandle,
         &gEfiPciIoProtocolGuid,
         This->DriverBindingHandle,
         ControllerHandle
         );
  return Status;
}

STATIC
VOID
FreeChildResources (
  IN SIMPLE_DISPLAY_PRIVATE  *Private
  )
{
  if (Private == NULL) {
    return;
  }

  if (Private->DevicePath != NULL) {
    FreePool (Private->DevicePath);
    Private->DevicePath = NULL;
  }

  Private->ChildHandle      = NULL;
  Private->ChildPciIoOpened = FALSE;
  ZeroMem (&Private->Hardware, sizeof (Private->Hardware));
}

STATIC
VOID
FreePrivate (
  IN SIMPLE_DISPLAY_PRIVATE  *Private
  )
{
  if (Private == NULL) {
    return;
  }

  FreeChildResources (Private);
  FreePool (Private);
}

STATIC
EFI_STATUS
RestorePciAttributes (
  IN SIMPLE_DISPLAY_PRIVATE  *Private
  )
{
  EFI_STATUS  Status;
  UINT16      Command;

  if ((Private == NULL) ||
      (!Private->PciAttributesSaved && !Private->PciCommandSaved))
  {
    return EFI_SUCCESS;
  }

  if (Private->PciAttributesSaved) {
    Status = Private->PciIo->Attributes (
                              Private->PciIo,
                              EfiPciIoAttributeOperationSet,
                              Private->OriginalPciAttributes,
                              NULL
                              );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  // Some firmware keeps an EFI_PCI_IO_PROTOCOL attribute cache that can
  // disagree with the raw Command register after Option-ROM dispatch. Restore
  // the exact entry Command word after the abstract attribute state.
  if (Private->PciCommandSaved) {
    Command = Private->OriginalPciCommand;
    Status  = Private->PciIo->Pci.Write (
                                    Private->PciIo,
                                    EfiPciIoWidthUint16,
                                    0x04,
                                    1,
                                    &Command
                                    );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Command = MAX_UINT16;
    Status  = Private->PciIo->Pci.Read (
                                    Private->PciIo,
                                    EfiPciIoWidthUint16,
                                    0x04,
                                    1,
                                    &Command
                                    );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if (Command != Private->OriginalPciCommand) {
      return EFI_DEVICE_ERROR;
    }
  }

  Private->PciAttributesSaved = FALSE;
  Private->PciCommandSaved    = FALSE;
  Private->PciMemoryEnabled   = FALSE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EnablePciMemoryDecode (
  IN EFI_PCI_IO_PROTOCOL  *PciIo
  )
{
  EFI_STATUS  Status;
  UINT16      Command;

  Status = PciIo->Attributes (
                    PciIo,
                    EfiPciIoAttributeOperationEnable,
                    EFI_PCI_IO_ATTRIBUTE_MEMORY,
                    NULL
                    );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Command = MAX_UINT16;
  Status  = PciIo->Pci.Read (
                         PciIo,
                         EfiPciIoWidthUint16,
                         0x04,
                         1,
                         &Command
                         );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // AMI's abstract attribute state can survive an exact raw Command restore.
  // Make the hardware decode state authoritative before any BAR2 MMIO.
  if ((Command & EFI_PCI_COMMAND_MEMORY_SPACE) == 0) {
    Command |= EFI_PCI_COMMAND_MEMORY_SPACE;
    Status   = PciIo->Pci.Write (
                            PciIo,
                            EfiPciIoWidthUint16,
                            0x04,
                            1,
                            &Command
                            );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Command = MAX_UINT16;
    Status  = PciIo->Pci.Read (
                           PciIo,
                           EfiPciIoWidthUint16,
                           0x04,
                           1,
                           &Command
                           );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  return ((Command & EFI_PCI_COMMAND_MEMORY_SPACE) != 0) ?
         EFI_SUCCESS : EFI_DEVICE_ERROR;
}

STATIC
EFI_STATUS
EFIAPI
SimpleDisplayDriverStart (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath OPTIONAL
  )
{
  EFI_STATUS                            Status;
  EFI_STATUS                            CleanupStatus;
  EFI_PCI_IO_PROTOCOL                   *PciIo;
  EFI_DEVICE_PATH_PROTOCOL              *ParentDevicePath;
  SIMPLE_DISPLAY_PRIVATE                *Private;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  InitialInfo;
  BOOLEAN                               BoundJustNow;
  UINT16                                OriginalPciCommand;

  PciIo           = NULL;
  ParentDevicePath = NULL;
  Private         = NULL;
  BoundJustNow    = FALSE;
  CleanupStatus   = EFI_SUCCESS;

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiPciIoProtocolGuid,
                  (VOID **)&PciIo,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (Status == EFI_ALREADY_STARTED) {
    Status = gBS->OpenProtocol (
                    ControllerHandle,
                    &gEfiCallerIdGuid,
                    (VOID **)&Private,
                    This->DriverBindingHandle,
                    ControllerHandle,
                    EFI_OPEN_PROTOCOL_GET_PROTOCOL
                    );
    if (EFI_ERROR (Status) ||
        (Private == NULL) ||
        (Private->Signature != SIMPLE_DISPLAY_PRIVATE_SIGNATURE) ||
        (Private->ControllerHandle != ControllerHandle))
    {
      return EFI_ALREADY_STARTED;
    }

    PciIo           = Private->PciIo;
    ParentDevicePath = Private->ParentDevicePath;
  } else if (EFI_ERROR (Status)) {
    return Status;
  } else {
    BoundJustNow = TRUE;
  }

  if (!BoundJustNow) {
    if ((RemainingDevicePath != NULL) && IsDevicePathEnd (RemainingDevicePath)) {
      return EFI_ALREADY_STARTED;
    }

    if (Private->ChildHandle != NULL) {
      return EFI_ALREADY_STARTED;
    }
  }

  if (BoundJustNow) {
    Status = gBS->OpenProtocol (
                    ControllerHandle,
                    &gEfiDevicePathProtocolGuid,
                    (VOID **)&ParentDevicePath,
                    This->DriverBindingHandle,
                    ControllerHandle,
                    EFI_OPEN_PROTOCOL_BY_DRIVER
                    );
    if (EFI_ERROR (Status)) {
      goto ClosePciIo;
    }

    Status = SimpleDisplayHwMatchPci (PciIo, NULL);
    if (EFI_ERROR (Status)) {
      goto CloseDevicePath;
    }

    Private = AllocateZeroPool (sizeof (*Private));
    if (Private == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
      goto CloseDevicePath;
    }

    Private->Signature        = SIMPLE_DISPLAY_PRIVATE_SIGNATURE;
    Private->ControllerHandle = ControllerHandle;
    Private->PciIo            = PciIo;
    Private->ParentDevicePath = ParentDevicePath;

    Status = gBS->InstallProtocolInterface (
                    &ControllerHandle,
                    &gEfiCallerIdGuid,
                    EFI_NATIVE_INTERFACE,
                    Private
                    );
    if (EFI_ERROR (Status)) {
      goto FreeAllocatedPrivate;
    }
  }

  // An end node asks this bus driver to manage only the parent. Retain the
  // private parent marker so a later Start() can create the HDMI GOP child.
  if ((RemainingDevicePath != NULL) && IsDevicePathEnd (RemainingDevicePath)) {
    return EFI_SUCCESS;
  }

  Status = PciIo->Pci.Read (
                        PciIo,
                        EfiPciIoWidthUint16,
                        0x04,
                        1,
                        &OriginalPciCommand
                        );
  if (EFI_ERROR (Status)) {
    goto ChildStartFailed;
  }

  Status = PciIo->Attributes (
                    PciIo,
                    EfiPciIoAttributeOperationGet,
                    0,
                    &Private->OriginalPciAttributes
                    );
  if (EFI_ERROR (Status)) {
    goto ChildStartFailed;
  }

  Private->OriginalPciCommand = OriginalPciCommand;
  Private->OriginalPciAttributes &= ~(EFI_PCI_IO_ATTRIBUTE_IO |
                                      EFI_PCI_IO_ATTRIBUTE_MEMORY |
                                      EFI_PCI_IO_ATTRIBUTE_BUS_MASTER);
  if ((OriginalPciCommand & EFI_PCI_COMMAND_IO_SPACE) != 0) {
    Private->OriginalPciAttributes |= EFI_PCI_IO_ATTRIBUTE_IO;
  }

  if ((OriginalPciCommand & EFI_PCI_COMMAND_MEMORY_SPACE) != 0) {
    Private->OriginalPciAttributes |= EFI_PCI_IO_ATTRIBUTE_MEMORY;
  }

  if ((OriginalPciCommand & EFI_PCI_COMMAND_BUS_MASTER) != 0) {
    Private->OriginalPciAttributes |= EFI_PCI_IO_ATTRIBUTE_BUS_MASTER;
  }

  Private->PciAttributesSaved = TRUE;
  Private->PciCommandSaved    = TRUE;
  Status = EnablePciMemoryDecode (PciIo);
  if (EFI_ERROR (Status)) {
    goto ChildStartFailed;
  }

  Private->PciMemoryEnabled = TRUE;
  Status = SimpleDisplayHwInitializeContext (PciIo, &Private->Hardware);
  if (EFI_ERROR (Status)) {
    goto ChildStartFailed;
  }

  Private->DevicePath = AppendDevicePathNode (
                          ParentDevicePath,
                          (EFI_DEVICE_PATH_PROTOCOL *)&mHdmiDevicePathNode
                          );
  if (Private->DevicePath == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ChildStartFailed;
  }

  Private->Gop.QueryMode = SimpleDisplayQueryMode;
  Private->Gop.SetMode   = SimpleDisplaySetMode;
  Private->Gop.Blt       = SimpleDisplayBlt;
  Private->Gop.Mode      = &Private->GopMode;
  MakeModeInfo (0, &InitialInfo);
  CopyMem (&Private->GopInfo, &InitialInfo, sizeof (InitialInfo));
  Private->GopMode.MaxMode         = SIMPLE_DISPLAY_GOP_MODE_COUNT;
  // No hardware mode is selected in Driver Binding Start. A consumer must
  // call SetMode before Blt can touch the visible framebuffer.
  Private->GopMode.Mode            = MAX_UINT32;
  Private->GopMode.Info            = &Private->GopInfo;
  Private->GopMode.SizeOfInfo      = sizeof (Private->GopInfo);
  Private->GopMode.FrameBufferBase = 0;
  Private->GopMode.FrameBufferSize = 0;

  Private->EdidDiscovered.SizeOfEdid = 0;
  Private->EdidDiscovered.Edid       = NULL;
  Private->EdidActive.SizeOfEdid     = 0;
  Private->EdidActive.Edid           = NULL;

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Private->ChildHandle,
                  &gEfiDevicePathProtocolGuid,
                  Private->DevicePath,
                  &gEfiGraphicsOutputProtocolGuid,
                  &Private->Gop,
                  &gEfiEdidDiscoveredProtocolGuid,
                  &Private->EdidDiscovered,
                  &gEfiEdidActiveProtocolGuid,
                  &Private->EdidActive,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    Private->ChildHandle = NULL;
    goto ChildStartFailed;
  }

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiPciIoProtocolGuid,
                  (VOID **)&Private->PciIo,
                  This->DriverBindingHandle,
                  Private->ChildHandle,
                  EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
                  );
  if (EFI_ERROR (Status)) {
    CleanupStatus = gBS->UninstallMultipleProtocolInterfaces (
                           Private->ChildHandle,
                           &gEfiDevicePathProtocolGuid,
                           Private->DevicePath,
                           &gEfiGraphicsOutputProtocolGuid,
                           &Private->Gop,
                           &gEfiEdidDiscoveredProtocolGuid,
                           &Private->EdidDiscovered,
                           &gEfiEdidActiveProtocolGuid,
                           &Private->EdidActive,
                           NULL
                           );
    if (EFI_ERROR (CleanupStatus)) {
      // Keep the child and parent state intact so Stop() can retry cleanup.
      return CleanupStatus;
    }

    Private->ChildHandle = NULL;
    goto ChildStartFailed;
  }

  Private->ChildPciIoOpened = TRUE;
  return EFI_SUCCESS;

ChildStartFailed:
  CleanupStatus = RestorePciAttributes (Private);
  FreeChildResources (Private);
  if (EFI_ERROR (CleanupStatus)) {
    // Keep the parent-private marker and BY_DRIVER opens intact so Stop() can
    // retry restoring the exact entry attributes.
    return CleanupStatus;
  }

  CleanupStatus = Status;
  if (!BoundJustNow) {
    return CleanupStatus;
  }

  Status = gBS->UninstallProtocolInterface (
                  ControllerHandle,
                  &gEfiCallerIdGuid,
                  Private
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

FreeAllocatedPrivate:
  FreePrivate (Private);
CloseDevicePath:
  gBS->CloseProtocol (
         ControllerHandle,
         &gEfiDevicePathProtocolGuid,
         This->DriverBindingHandle,
         ControllerHandle
         );
ClosePciIo:
  gBS->CloseProtocol (
         ControllerHandle,
         &gEfiPciIoProtocolGuid,
         This->DriverBindingHandle,
         ControllerHandle
         );
  return EFI_ERROR (CleanupStatus) ? CleanupStatus : Status;
}

STATIC
EFI_STATUS
EFIAPI
SimpleDisplayDriverStop (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN UINTN                        NumberOfChildren,
  IN EFI_HANDLE                   *ChildHandleBuffer OPTIONAL
  )
{
  EFI_STATUS                    Status;
  EFI_STATUS                    CleanupStatus;
  EFI_STATUS                    DevicePathStatus;
  EFI_STATUS                    PciIoStatus;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  SIMPLE_DISPLAY_PRIVATE        *Private;

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiCallerIdGuid,
                  (VOID **)&Private,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status) ||
      (Private == NULL) ||
      (Private->Signature != SIMPLE_DISPLAY_PRIVATE_SIGNATURE) ||
      (Private->ControllerHandle != ControllerHandle))
  {
    return EFI_DEVICE_ERROR;
  }

  if (NumberOfChildren == 0) {
    if (Private->ChildHandle != NULL) {
      return EFI_DEVICE_ERROR;
    }

    Status = RestorePciAttributes (Private);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Status = gBS->UninstallProtocolInterface (
                    ControllerHandle,
                    &gEfiCallerIdGuid,
                    Private
                    );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    DevicePathStatus = gBS->CloseProtocol (
                               ControllerHandle,
                               &gEfiDevicePathProtocolGuid,
                               This->DriverBindingHandle,
                               ControllerHandle
                               );
    PciIoStatus = gBS->CloseProtocol (
                         ControllerHandle,
                         &gEfiPciIoProtocolGuid,
                         This->DriverBindingHandle,
                         ControllerHandle
                         );
    FreePrivate (Private);
    return EFI_ERROR (DevicePathStatus) ? DevicePathStatus : PciIoStatus;
  }

  if ((NumberOfChildren != 1) || (ChildHandleBuffer == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Status = gBS->OpenProtocol (
                  ChildHandleBuffer[0],
                  &gEfiGraphicsOutputProtocolGuid,
                  (VOID **)&Gop,
                  This->DriverBindingHandle,
                  ChildHandleBuffer[0],
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Private = SIMPLE_DISPLAY_PRIVATE_FROM_GOP (Gop);
  if ((Private->ControllerHandle != ControllerHandle) ||
      (Private->ChildHandle != ChildHandleBuffer[0]))
  {
    return EFI_INVALID_PARAMETER;
  }

  if (Private->ChildPciIoOpened) {
    Status = gBS->CloseProtocol (
                    ControllerHandle,
                    &gEfiPciIoProtocolGuid,
                    This->DriverBindingHandle,
                    Private->ChildHandle
                    );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Private->ChildPciIoOpened = FALSE;
  }

  Status = gBS->UninstallMultipleProtocolInterfaces (
                  Private->ChildHandle,
                  &gEfiDevicePathProtocolGuid,
                  Private->DevicePath,
                  &gEfiGraphicsOutputProtocolGuid,
                  &Private->Gop,
                  &gEfiEdidDiscoveredProtocolGuid,
                  &Private->EdidDiscovered,
                  &gEfiEdidActiveProtocolGuid,
                  &Private->EdidActive,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    CleanupStatus = gBS->OpenProtocol (
                           ControllerHandle,
                           &gEfiPciIoProtocolGuid,
                           (VOID **)&Private->PciIo,
                           This->DriverBindingHandle,
                           Private->ChildHandle,
                           EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
                           );
    Private->ChildPciIoOpened = !EFI_ERROR (CleanupStatus);
    return Status;
  }

  // Explicit disconnect policy: leave the pipeline programmed, but restore the
  // controller's original PCI attributes before releasing the parent later.
  FreeChildResources (Private);
  return RestorePciAttributes (Private);
}

STATIC EFI_DRIVER_BINDING_PROTOCOL  mSimpleDisplayDriverBinding = {
  SimpleDisplayDriverSupported,
  SimpleDisplayDriverStart,
  SimpleDisplayDriverStop,
  0x10,
  NULL,
  NULL
};

EFI_STATUS
EFIAPI
SimpleDisplayGopEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  return EfiLibInstallDriverBindingComponentName2 (
           ImageHandle,
           SystemTable,
           &mSimpleDisplayDriverBinding,
           ImageHandle,
           NULL,
           NULL
           );
}
