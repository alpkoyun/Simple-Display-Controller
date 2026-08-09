/** @file
  Explicit, non-binding UEFI bring-up application. This application is allowed
  to change the visible output and deliberately leaves scanout running.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <IndustryStandard/Acpi.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/SimpleDisplayHwLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/PciIo.h>

STATIC
VOID
PrintBars (
  IN EFI_PCI_IO_PROTOCOL  *PciIo
  )
{
  EFI_STATUS                         Status;
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR  *Resource;
  UINTN                              Bar;

  for (Bar = 0; Bar < 6; Bar++) {
    Resource = NULL;
    Status   = PciIo->GetBarAttributes (PciIo, (UINT8)Bar, NULL, (VOID **)&Resource);
    if (EFI_ERROR (Status)) {
      Print (L"  BAR%u: %r\n", Bar, Status);
      continue;
    }

    if ((Resource != NULL) && (Resource->Desc == ACPI_ADDRESS_SPACE_DESCRIPTOR)) {
      Print (
        L"  BAR%u: type=%u base=0x%lx length=0x%lx\n",
        Bar,
        Resource->ResType,
        Resource->AddrRangeMin,
        Resource->AddrLen
        );
    } else {
      Print (L"  BAR%u: malformed descriptor\n", Bar);
    }

    if (Resource != NULL) {
      FreePool (Resource);
    }
  }
}

STATIC
VOID
PrintScratch (
  IN CONST SIMPLE_DISPLAY_SCRATCH_RESULT  *Scratch
  )
{
  Print (
    L"  scratch original: %08x %08x %08x %08x\n",
    Scratch->Original[0], Scratch->Original[1],
    Scratch->Original[2], Scratch->Original[3]
    );
  Print (
    L"  scratch pattern:  %08x %08x %08x %08x\n",
    Scratch->Readback[0], Scratch->Readback[1],
    Scratch->Readback[2], Scratch->Readback[3]
    );
  Print (
    L"  scratch restored: %08x %08x %08x %08x\n",
    Scratch->Restored[0], Scratch->Restored[1],
    Scratch->Restored[2], Scratch->Restored[3]
    );
}

STATIC
VOID
PrintHardwareStatus (
  IN CONST SIMPLE_DISPLAY_STATUS  *Status
  )
{
  Print (
    L"  GPIO ch1=%08x ch2=%08x IIC_SR=%08x unpack=%08x\n",
    Status->GpioChannel1,
    Status->GpioChannel2,
    Status->IicStatus,
    Status->PixelUnpackMode
    );
  Print (
    L"  CLOCK SR=%08x CFG0=%08x CFG2=%08x\n",
    Status->ClockStatus,
    Status->ClockConfig0,
    Status->ClockConfig2
    );
  Print (
    L"  VDMA MM2S CR=%08x SR=%08x S2MM CR=%08x SR=%08x PARK=%08x\n",
    Status->VdmaMm2sControl,
    Status->VdmaMm2sStatus,
    Status->VdmaS2mmControl,
    Status->VdmaS2mmStatus,
    Status->VdmaParkPointer
    );
  Print (
    L"  VTC CTL=%08x ISR=%08x ERR=%08x GTSTAT=%08x\n",
    Status->VtcControl,
    Status->VtcInterruptStatus,
    Status->VtcError,
    Status->VtcGeneratorStatus
    );
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                     Status;
  EFI_HANDLE                     *Handles;
  UINTN                          HandleCount;
  UINTN                          Index;
  EFI_PCI_IO_PROTOCOL            *PciIo;
  UINTN                          Segment;
  UINTN                          Bus;
  UINTN                          Device;
  UINTN                          Function;
  UINT8                          Revision;
  UINT16                         Command;
  UINT64                         OriginalAttributes;
  BOOLEAN                        Found;
  SIMPLE_DISPLAY_HW_CONTEXT      Context;
  SIMPLE_DISPLAY_SCRATCH_RESULT  Scratch;
  SIMPLE_DISPLAY_STATUS          HardwareStatus;
  CONST SIMPLE_DISPLAY_MODE      *Mode;

  (VOID)ImageHandle;
  (VOID)SystemTable;
  Handles     = NULL;
  HandleCount = 0;
  Found       = FALSE;
  Print (L"SimpleDisplayBringup: direct-BAR GOP prerequisite test\n");

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiPciIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    Print (L"PCI enumeration failed: %r\n", Status);
    return Status;
  }

  Status = EFI_NOT_FOUND;
  for (Index = 0; Index < HandleCount; Index++) {
    PciIo = NULL;
    Status = gBS->HandleProtocol (
                    Handles[Index],
                    &gEfiPciIoProtocolGuid,
                    (VOID **)&PciIo
                    );
    if (EFI_ERROR (Status)) {
      continue;
    }

    Status = SimpleDisplayHwMatchPci (PciIo, &Revision);
    if (Status == EFI_UNSUPPORTED) {
      continue;
    }

    if (EFI_ERROR (Status)) {
      Print (L"candidate PCI read failed: %r\n", Status);
      continue;
    }

    Found = TRUE;

    Status = PciIo->GetLocation (PciIo, &Segment, &Bus, &Device, &Function);
    if (EFI_ERROR (Status)) {
      Print (L"matched device location failed: %r\n", Status);
      break;
    }

    Command = 0;
    Status  = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, 0x04, 1, &Command);
    if (EFI_ERROR (Status)) {
      Print (L"matched device command read failed: %r\n", Status);
      break;
    }

    Print (
      L"PCI_MATCH %04x:%02x:%02x.%x 10ee:7024 rev=%02x command=%04x\n",
      Segment,
      Bus,
      Device,
      Function,
      Revision,
      Command
      );
    PrintBars (PciIo);

    OriginalAttributes = 0;
    Status = PciIo->Attributes (
                      PciIo,
                      EfiPciIoAttributeOperationGet,
                      0,
                      &OriginalAttributes
                      );
    if (EFI_ERROR (Status)) {
      Print (L"PCI attribute read failed: %r\n", Status);
      break;
    }

    Status = PciIo->Attributes (
                      PciIo,
                      EfiPciIoAttributeOperationEnable,
                      EFI_PCI_IO_ATTRIBUTE_MEMORY,
                      NULL
                      );
    if (EFI_ERROR (Status)) {
      Print (L"memory decode enable failed: %r\n", Status);
      break;
    }

    Status = SimpleDisplayHwInitializeContext (PciIo, &Context);
    if (EFI_ERROR (Status)) {
      Print (L"BAR2 validation failed: %r\n", Status);
      break;
    }

    Print (
      L"BAR2_OK base=0x%lx length=0x%lx framebuffer=0x%lx\n",
      Context.BarBase,
      Context.BarLength,
      Context.BarBase + SIMPLE_DISPLAY_FRAMEBUFFER_OFFSET
      );

    Status = SimpleDisplayHwScratchTest (&Context, &Scratch);
    PrintScratch (&Scratch);
    if (EFI_ERROR (Status)) {
      Print (L"scratch test/restore failed: %r\n", Status);
      break;
    }

    Print (L"SCRATCH_RESTORE_OK\n");
    Mode   = &gSimpleDisplayModes[0];
    Status = SimpleDisplayHwProgramMode (&Context, Mode);
    if (EFI_ERROR (Status)) {
      Print (L"pipeline programming failed for %a: %r\n", Mode->Name, Status);
      break;
    }

    Print (L"HDMI_I2C_OK\nCLOCK_LOCK_OK\nVDMA_MM2S_OK\nVTC_OK\n");
    Status = SimpleDisplayHwFillColorBars (&Context, Mode);
    if (EFI_ERROR (Status)) {
      Print (L"frame fill failed: %r\n", Status);
      break;
    }

    Print (L"FRAME_FILL_OK\n");
    Status = SimpleDisplayHwReadStatus (&Context, &HardwareStatus);
    if (EFI_ERROR (Status)) {
      Print (L"status readback failed: %r\n", Status);
      break;
    }

    PrintHardwareStatus (&HardwareStatus);
    Print (
      L"BRINGUP_PASS mode=%a frame_bytes=0x%lx\n",
      Mode->Name,
      SimpleDisplayHwFrameBytes (Mode)
      );
    Print (L"Scanout and PCI memory decoding are intentionally left active.\n");
    Status = EFI_SUCCESS;
    break;
  }

  if (Handles != NULL) {
    FreePool (Handles);
  }

  if (!Found) {
    Status = EFI_NOT_FOUND;
    Print (L"No matching 10ee:7024 display-class controller found.\n");
  } else if (EFI_ERROR (Status)) {
    Print (L"BRINGUP_FAIL %r\n", Status);
  }

  return Status;
}
