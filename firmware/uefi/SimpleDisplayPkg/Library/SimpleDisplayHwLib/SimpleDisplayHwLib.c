/** @file
  Simple Display Controller register programming shared by UEFI applications
  and the GOP DXE driver.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <IndustryStandard/Acpi.h>
#include <IndustryStandard/Pci.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/SimpleDisplayHwLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define BIT(Value)  (1U << (Value))

#define PIXEL_UNPACK_MODE             0x010

#define COLOR_C1_C1                   0x010
#define COLOR_C1_C2                   0x018
#define COLOR_C1_C3                   0x020
#define COLOR_C2_C1                   0x028
#define COLOR_C2_C2                   0x030
#define COLOR_C2_C3                   0x038
#define COLOR_C3_C1                   0x040
#define COLOR_C3_C2                   0x048
#define COLOR_C3_C3                   0x050
#define COLOR_BIAS_C1                 0x058
#define COLOR_BIAS_C2                 0x060
#define COLOR_BIAS_C3                 0x068

#define VDMA_MM2S_CHANNEL             0x000
#define VDMA_S2MM_CHANNEL             0x030
#define VDMA_PARK_POINTER             0x028
#define VDMA_CONTROL                  0x000
#define VDMA_STATUS                   0x004
#define VDMA_FRAME_STORE              0x018
#define VDMA_MM2S_ADDRESS             0x050
#define VDMA_VERTICAL_SIZE            0x000
#define VDMA_HORIZONTAL_SIZE          0x004
#define VDMA_STRIDE                   0x008
#define VDMA_START_ADDRESS            0x00C
#define VDMA_CONTROL_RUN              BIT (0)
#define VDMA_CONTROL_TAIL             BIT (1)
#define VDMA_CONTROL_RESET            BIT (2)
#define VDMA_CONTROL_SYNC             BIT (3)
#define VDMA_CONTROL_INTERNAL_GENLOCK BIT (7)
#define VDMA_STATUS_ERRORS            0x00000FF0U
#define VDMA_STATUS_INTERRUPTS        0x00007000U

#define VTC_CONTROL                   0x000
#define VTC_INTERRUPT_STATUS          0x004
#define VTC_ERROR                     0x008
#define VTC_ACTIVE_SIZE               0x060
#define VTC_GENERATOR_STATUS          0x064
#define VTC_FRAME_ENCODING            0x068
#define VTC_POLARITY                  0x06C
#define VTC_HORIZONTAL_SIZE           0x070
#define VTC_VERTICAL_SIZE             0x074
#define VTC_HORIZONTAL_SYNC           0x078
#define VTC_VERTICAL_BLANK_OFFSET     0x07C
#define VTC_VERTICAL_SYNC             0x080
#define VTC_VERTICAL_SYNC_OFFSET      0x084
#define VTC_VERTICAL_BLANK_OFFSET_F1  0x088
#define VTC_VERTICAL_SYNC_F1          0x08C
#define VTC_VERTICAL_SYNC_OFFSET_F1   0x090
#define VTC_ACTIVE_SIZE_F1            0x094
#define VTC_CONTROL_ALL_SOURCE_SELECT 0x03FDEF00U
#define VTC_CONTROL_GENERATOR_ENABLE  BIT (2)
#define VTC_CONTROL_REGISTER_UPDATE   BIT (1)
#define VTC_CONTROL_SOFTWARE_ENABLE   BIT (0)
#define VTC_POLARITY_VBLANK           BIT (0)
#define VTC_POLARITY_HBLANK           BIT (1)
#define VTC_POLARITY_VSYNC            BIT (2)
#define VTC_POLARITY_HSYNC            BIT (3)
#define VTC_POLARITY_ACTIVE_VIDEO     BIT (4)
#define VTC_POLARITY_ACTIVE_CHROMA    BIT (5)
#define VTC_POLARITY_FIELD_ID         BIT (6)

#define GPIO_DATA_CHANNEL1            0x000
#define GPIO_TRI_CHANNEL1             0x004
#define GPIO_DATA_CHANNEL2            0x008
#define GPIO_TRI_CHANNEL2             0x00C

#define CLOCK_STATUS                  0x004
#define CLOCK_CONFIG0                 0x200
#define CLOCK_CONFIG1                 0x204
#define CLOCK_CONFIG2                 0x208
#define CLOCK_CONFIG3                 0x20C
#define CLOCK_CONFIG4                 0x210
#define CLOCK_CONFIG23                0x25C
#define CLOCK_STATUS_LOCKED           BIT (0)
#define CLOCK_CONFIG23_LOAD           BIT (0)
#define CLOCK_CONFIG23_SADDR          BIT (1)
#define CLOCK_DUTY_50_PERCENT         50000U

#define IIC_RESET_REGISTER            0x040
#define IIC_CONTROL                   0x100
#define IIC_STATUS                    0x104
#define IIC_TX_FIFO                   0x108
#define IIC_RESET_VALUE               0x0000000AU
#define IIC_CONTROL_ENABLE            BIT (0)
#define IIC_CONTROL_TX_FIFO_RESET     BIT (1)
#define IIC_STATUS_BUS_BUSY           BIT (2)
#define IIC_STATUS_TX_FIFO_FULL       BIT (4)
#define IIC_STATUS_TX_FIFO_EMPTY      BIT (7)
#define IIC_DYNAMIC_START             0x00000100U
#define IIC_DYNAMIC_STOP              0x00000200U

CONST SIMPLE_DISPLAY_MODE  gSimpleDisplayModes[SIMPLE_DISPLAY_MODE_COUNT] = {
  {
    "1920x1080@60", 148500,
    1920, 2008, 2052, 2200,
    1080, 1084, 1089, 1125,
    TRUE, TRUE,
    (10U | (37U << 8) | (125U << 16)), 5U
  },
  {
    "800x600@60", 40000,
    800, 840, 968, 1056,
    600, 601, 605, 628,
    TRUE, TRUE,
    (1U | (3U << 8)), 15U
  },
  {
    "640x480@60", 25175,
    640, 656, 752, 800,
    480, 490, 492, 525,
    FALSE, FALSE,
    (11U | (36U << 8)), 26U
  },
  {
    "1024x768@60", 65000,
    1024, 1048, 1184, 1344,
    768, 771, 777, 806,
    FALSE, FALSE,
    (4U | (13U << 8)), 10U
  },
  {
    "1280x1024@60", 108000,
    1280, 1328, 1440, 1688,
    1024, 1025, 1028, 1066,
    TRUE, TRUE,
    (5U | (27U << 8)), 10U
  },
  {
    "1280x720@60", 74250,
    1280, 1390, 1430, 1650,
    720, 725, 730, 750,
    TRUE, TRUE,
    (10U | (37U << 8) | (125U << 16)), 10U
  }
};

STATIC
EFI_STATUS
Read32 (
  IN  SIMPLE_DISPLAY_HW_CONTEXT  *Context,
  IN  UINT64                     Offset,
  OUT UINT32                     *Value
  )
{
  if ((Context == NULL) || (Context->PciIo == NULL) || (Value == NULL) ||
      (Offset > Context->BarLength - sizeof (*Value)))
  {
    return EFI_INVALID_PARAMETER;
  }

  return Context->PciIo->Mem.Read (
                               Context->PciIo,
                               EfiPciIoWidthUint32,
                               (UINT8)Context->BarIndex,
                               Offset,
                               1,
                               Value
                               );
}

STATIC
EFI_STATUS
Write32 (
  IN SIMPLE_DISPLAY_HW_CONTEXT  *Context,
  IN UINT64                     Offset,
  IN UINT32                     Value
  )
{
  if ((Context == NULL) || (Context->PciIo == NULL) ||
      (Offset > Context->BarLength - sizeof (Value)))
  {
    return EFI_INVALID_PARAMETER;
  }

  return Context->PciIo->Mem.Write (
                               Context->PciIo,
                               EfiPciIoWidthUint32,
                               (UINT8)Context->BarIndex,
                               Offset,
                               1,
                               &Value
                               );
}

STATIC
EFI_STATUS
WaitForBits (
  IN SIMPLE_DISPLAY_HW_CONTEXT  *Context,
  IN UINT64                     Offset,
  IN UINT32                     ClearMask,
  IN UINT32                     SetMask,
  IN UINTN                      TimeoutMicroseconds,
  IN UINTN                      PollMicroseconds
  )
{
  EFI_STATUS  Status;
  UINT32      Value;
  UINTN       Waited;

  for (Waited = 0; Waited < TimeoutMicroseconds; Waited += PollMicroseconds) {
    Status = Read32 (Context, Offset, &Value);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if (((Value & ClearMask) == 0) && ((Value & SetMask) == SetMask)) {
      return EFI_SUCCESS;
    }

    gBS->Stall (PollMicroseconds);
  }

  return EFI_TIMEOUT;
}

EFI_STATUS
EFIAPI
SimpleDisplayHwMatchPci (
  IN  EFI_PCI_IO_PROTOCOL  *PciIo,
  OUT UINT8                *Revision OPTIONAL
  )
{
  EFI_STATUS  Status;
  PCI_TYPE00  Pci;

  if (PciIo == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (&Pci, sizeof (Pci));
  Status = PciIo->Pci.Read (
                        PciIo,
                        EfiPciIoWidthUint8,
                        0,
                        sizeof (Pci),
                        &Pci
                        );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((Pci.Hdr.VendorId != SIMPLE_DISPLAY_VENDOR_ID) ||
      (Pci.Hdr.DeviceId != SIMPLE_DISPLAY_DEVICE_ID) ||
      (Pci.Hdr.ClassCode[2] != SIMPLE_DISPLAY_BASE_CLASS))
  {
    return EFI_UNSUPPORTED;
  }

  if (Revision != NULL) {
    *Revision = Pci.Hdr.RevisionID;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SimpleDisplayHwInitializeContext (
  IN  EFI_PCI_IO_PROTOCOL        *PciIo,
  OUT SIMPLE_DISPLAY_HW_CONTEXT  *Context
  )
{
  EFI_STATUS                         Status;
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR  *Resource;
  UINT32                             Magic;

  if ((PciIo == NULL) || (Context == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Context, sizeof (*Context));
  Resource = NULL;
  Status   = PciIo->GetBarAttributes (
                      PciIo,
                      SIMPLE_DISPLAY_BAR_INDEX,
                      NULL,
                      (VOID **)&Resource
                      );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((Resource == NULL) ||
      (Resource->Desc != ACPI_ADDRESS_SPACE_DESCRIPTOR) ||
      (Resource->ResType != ACPI_ADDRESS_SPACE_TYPE_MEM) ||
      (Resource->AddrLen < SIMPLE_DISPLAY_BAR_MIN_SIZE))
  {
    Status = EFI_UNSUPPORTED;
  } else {
    Context->PciIo     = PciIo;
    Context->BarIndex  = SIMPLE_DISPLAY_BAR_INDEX;
    Context->BarBase   = Resource->AddrRangeMin;
    Context->BarLength = Resource->AddrLen;
    Status = Read32 (
               Context,
               SIMPLE_DISPLAY_IDENTITY_OFFSET + SIMPLE_DISPLAY_ID_MAGIC_OFFSET,
               &Magic
               );
    if (!EFI_ERROR (Status)) {
      Status = Read32 (
                 Context,
                 SIMPLE_DISPLAY_IDENTITY_OFFSET + SIMPLE_DISPLAY_ID_VERSION_OFFSET,
                 &Context->AbiVersion
                 );
    }

    if (!EFI_ERROR (Status)) {
      Status = Read32 (
                 Context,
                 SIMPLE_DISPLAY_IDENTITY_OFFSET + SIMPLE_DISPLAY_ID_FEATURES_OFFSET,
                 &Context->AbiFeatures
                 );
    }

    if (!EFI_ERROR (Status)) {
      Status = Read32 (
                 Context,
                 SIMPLE_DISPLAY_IDENTITY_OFFSET + SIMPLE_DISPLAY_ID_ROM_SIZE_OFFSET,
                 &Context->RomApertureBytes
                 );
    }

    if (!EFI_ERROR (Status) &&
        ((Magic != SIMPLE_DISPLAY_ABI_MAGIC) ||
         (Context->AbiVersion != SIMPLE_DISPLAY_ABI_VERSION) ||
         ((Context->AbiFeatures & SIMPLE_DISPLAY_ABI_REQUIRED_FEATURES) !=
          SIMPLE_DISPLAY_ABI_REQUIRED_FEATURES) ||
         ((Context->RomApertureBytes != 4096U) &&
          (Context->RomApertureBytes != 32768U))))
    {
      Status = EFI_UNSUPPORTED;
    }
  }

  if (Resource != NULL) {
    FreePool (Resource);
  }

  if (EFI_ERROR (Status)) {
    ZeroMem (Context, sizeof (*Context));
  }

  return Status;
}

UINTN
EFIAPI
SimpleDisplayHwFrameBytes (
  IN CONST SIMPLE_DISPLAY_MODE  *Mode
  )
{
  if (Mode == NULL) {
    return 0;
  }

  return (UINTN)Mode->HorizontalActive *
         (UINTN)Mode->VerticalActive *
         SIMPLE_DISPLAY_BYTES_PER_PIXEL;
}

EFI_STATUS
EFIAPI
SimpleDisplayHwValidateModeBounds (
  IN CONST SIMPLE_DISPLAY_HW_CONTEXT  *Context,
  IN CONST SIMPLE_DISPLAY_MODE        *Mode
  )
{
  UINTN  FrameBytes;

  if ((Context == NULL) || (Mode == NULL) ||
      (Mode->HorizontalActive == 0) || (Mode->VerticalActive == 0) ||
      (Mode->HorizontalActive > SIMPLE_DISPLAY_MAX_WIDTH) ||
      (Mode->VerticalActive > SIMPLE_DISPLAY_MAX_HEIGHT))
  {
    return EFI_INVALID_PARAMETER;
  }

  FrameBytes = SimpleDisplayHwFrameBytes (Mode);
  if ((FrameBytes == 0) || (FrameBytes > SIMPLE_DISPLAY_MAX_FRAME_BYTES) ||
      (SIMPLE_DISPLAY_FRAMEBUFFER_OFFSET > Context->BarLength) ||
      (FrameBytes > Context->BarLength - SIMPLE_DISPLAY_FRAMEBUFFER_OFFSET))
  {
    return EFI_BAD_BUFFER_SIZE;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SimpleDisplayHwScratchTest (
  IN  SIMPLE_DISPLAY_HW_CONTEXT    *Context,
  OUT SIMPLE_DISPLAY_SCRATCH_RESULT *Result
  )
{
  STATIC CONST UINT32  Patterns[4] = {
    0x55AA00FFU, 0xA55AC33CU, 0x01234567U, 0x89ABCDEFU
  };
  EFI_STATUS  Status;
  EFI_STATUS  RestoreStatus;
  UINTN       Index;

  if ((Context == NULL) || (Result == NULL) ||
      (SIMPLE_DISPLAY_SCRATCH_OFFSET > Context->BarLength - sizeof (Patterns)))
  {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Result, sizeof (*Result));
  for (Index = 0; Index < 4; Index++) {
    Status = Read32 (
               Context,
               SIMPLE_DISPLAY_SCRATCH_OFFSET + Index * sizeof (UINT32),
               &Result->Original[Index]
               );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  Status = EFI_SUCCESS;
  for (Index = 0; Index < 4; Index++) {
    Status = Write32 (
               Context,
               SIMPLE_DISPLAY_SCRATCH_OFFSET + Index * sizeof (UINT32),
               Patterns[Index]
               );
    if (EFI_ERROR (Status)) {
      break;
    }
  }

  if (!EFI_ERROR (Status)) {
    for (Index = 0; Index < 4; Index++) {
      Status = Read32 (
                 Context,
                 SIMPLE_DISPLAY_SCRATCH_OFFSET + Index * sizeof (UINT32),
                 &Result->Readback[Index]
                 );
      if (EFI_ERROR (Status) || (Result->Readback[Index] != Patterns[Index])) {
        if (!EFI_ERROR (Status)) {
          Status = EFI_COMPROMISED_DATA;
        }

        break;
      }
    }
  }

  RestoreStatus = EFI_SUCCESS;
  for (Index = 0; Index < 4; Index++) {
    RestoreStatus = Write32 (
                      Context,
                      SIMPLE_DISPLAY_SCRATCH_OFFSET + Index * sizeof (UINT32),
                      Result->Original[Index]
                      );
    if (EFI_ERROR (RestoreStatus)) {
      break;
    }
  }

  if (!EFI_ERROR (RestoreStatus)) {
    for (Index = 0; Index < 4; Index++) {
      RestoreStatus = Read32 (
                        Context,
                        SIMPLE_DISPLAY_SCRATCH_OFFSET + Index * sizeof (UINT32),
                        &Result->Restored[Index]
                        );
      if (EFI_ERROR (RestoreStatus) ||
          (Result->Restored[Index] != Result->Original[Index]))
      {
        if (!EFI_ERROR (RestoreStatus)) {
          RestoreStatus = EFI_COMPROMISED_DATA;
        }

        break;
      }
    }
  }

  return EFI_ERROR (RestoreStatus) ? RestoreStatus : Status;
}

STATIC
EFI_STATUS
ConfigureGpio (
  IN SIMPLE_DISPLAY_HW_CONTEXT  *Context
  )
{
  EFI_STATUS  Status;
  UINT32      Value;

  Status = Write32 (Context, SIMPLE_DISPLAY_GPIO_OFFSET + GPIO_TRI_CHANNEL1, 0x0FU);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Write32 (Context, SIMPLE_DISPLAY_GPIO_OFFSET + GPIO_TRI_CHANNEL2, MAX_UINT32);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Read32 (Context, SIMPLE_DISPLAY_GPIO_OFFSET + GPIO_DATA_CHANNEL1, &Value);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return Read32 (Context, SIMPLE_DISPLAY_GPIO_OFFSET + GPIO_DATA_CHANNEL2, &Value);
}

STATIC
EFI_STATUS
ConfigurePixelUnpack (
  IN SIMPLE_DISPLAY_HW_CONTEXT  *Context
  )
{
  EFI_STATUS  Status;
  UINT32      Value;

  Status = Write32 (Context, SIMPLE_DISPLAY_PIXEL_UNPACK_OFFSET + PIXEL_UNPACK_MODE, 1U);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Read32 (Context, SIMPLE_DISPLAY_PIXEL_UNPACK_OFFSET + PIXEL_UNPACK_MODE, &Value);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return (Value == 1U) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

STATIC
EFI_STATUS
ConfigureColor (
  IN SIMPLE_DISPLAY_HW_CONTEXT  *Context
  )
{
  STATIC CONST struct {
    UINT32  Offset;
    UINT32  Value;
  } Coefficients[] = {
    { COLOR_C1_C1, 256 }, { COLOR_C1_C2, 0 }, { COLOR_C1_C3, 0 },
    { COLOR_C2_C1, 0 }, { COLOR_C2_C2, 256 }, { COLOR_C2_C3, 0 },
    { COLOR_C3_C1, 0 }, { COLOR_C3_C2, 0 }, { COLOR_C3_C3, 256 },
    { COLOR_BIAS_C1, 0 }, { COLOR_BIAS_C2, 0 }, { COLOR_BIAS_C3, 0 }
  };
  EFI_STATUS  Status;
  UINT32      Value;
  UINTN       Index;

  for (Index = 0; Index < sizeof (Coefficients) / sizeof (Coefficients[0]); Index++) {
    Status = Write32 (
               Context,
               SIMPLE_DISPLAY_COLOR_OFFSET + Coefficients[Index].Offset,
               Coefficients[Index].Value
               );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  Status = Read32 (Context, SIMPLE_DISPLAY_COLOR_OFFSET + COLOR_C1_C1, &Value);
  if (EFI_ERROR (Status) || (Value != 256U)) {
    return EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
  }

  Status = Read32 (Context, SIMPLE_DISPLAY_COLOR_OFFSET + COLOR_C2_C2, &Value);
  if (EFI_ERROR (Status) || (Value != 256U)) {
    return EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
  }

  Status = Read32 (Context, SIMPLE_DISPLAY_COLOR_OFFSET + COLOR_C3_C3, &Value);
  if (EFI_ERROR (Status) || (Value != 256U)) {
    return EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
IicWriteFifo (
  IN SIMPLE_DISPLAY_HW_CONTEXT  *Context,
  IN UINT32                     Value
  )
{
  EFI_STATUS  Status;

  Status = WaitForBits (
             Context,
             SIMPLE_DISPLAY_IIC_OFFSET + IIC_STATUS,
             IIC_STATUS_TX_FIFO_FULL,
             0,
             10000,
             10
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return Write32 (Context, SIMPLE_DISPLAY_IIC_OFFSET + IIC_TX_FIFO, Value);
}

STATIC
EFI_STATUS
IicWriteRegister (
  IN SIMPLE_DISPLAY_HW_CONTEXT  *Context,
  IN UINT8                      DeviceAddress8,
  IN UINT8                      Register,
  IN UINT8                      Data
  )
{
  EFI_STATUS  Status;

  Status = WaitForBits (
             Context,
             SIMPLE_DISPLAY_IIC_OFFSET + IIC_STATUS,
             IIC_STATUS_BUS_BUSY,
             0,
             100000,
             10
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = IicWriteFifo (Context, IIC_DYNAMIC_START | DeviceAddress8);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = IicWriteFifo (Context, Register);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = IicWriteFifo (Context, IIC_DYNAMIC_STOP | Data);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return WaitForBits (
           Context,
           SIMPLE_DISPLAY_IIC_OFFSET + IIC_STATUS,
           IIC_STATUS_BUS_BUSY,
           IIC_STATUS_TX_FIFO_EMPTY,
           100000,
           10
           );
}

STATIC
EFI_STATUS
ConfigureHdmi (
  IN SIMPLE_DISPLAY_HW_CONTEXT  *Context
  )
{
  STATIC CONST struct {
    UINT8  Device;
    UINT8  Register;
    UINT8  Data;
  } Settings[] = {
    { 0x72, 0x08, 0x35 },
    { 0x7A, 0x2F, 0x00 }
  };
  EFI_STATUS  Status;
  UINTN       Index;

  Status = Write32 (Context, SIMPLE_DISPLAY_IIC_OFFSET + IIC_RESET_REGISTER, IIC_RESET_VALUE);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  gBS->Stall (10);
  Status = Write32 (
             Context,
             SIMPLE_DISPLAY_IIC_OFFSET + IIC_CONTROL,
             IIC_CONTROL_ENABLE | IIC_CONTROL_TX_FIFO_RESET
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Write32 (Context, SIMPLE_DISPLAY_IIC_OFFSET + IIC_CONTROL, IIC_CONTROL_ENABLE);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (Index = 0; Index < sizeof (Settings) / sizeof (Settings[0]); Index++) {
    Status = IicWriteRegister (
               Context,
               Settings[Index].Device,
               Settings[Index].Register,
               Settings[Index].Data
               );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    gBS->Stall (1000);
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ConfigureClock (
  IN SIMPLE_DISPLAY_HW_CONTEXT  *Context,
  IN CONST SIMPLE_DISPLAY_MODE  *Mode
  )
{
  EFI_STATUS  Status;

  Status = Write32 (Context, SIMPLE_DISPLAY_CLOCK_OFFSET + CLOCK_CONFIG0, Mode->ClockConfig0);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Write32 (Context, SIMPLE_DISPLAY_CLOCK_OFFSET + CLOCK_CONFIG1, 0);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Write32 (Context, SIMPLE_DISPLAY_CLOCK_OFFSET + CLOCK_CONFIG2, Mode->ClockConfig2);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Write32 (Context, SIMPLE_DISPLAY_CLOCK_OFFSET + CLOCK_CONFIG3, 0);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Write32 (Context, SIMPLE_DISPLAY_CLOCK_OFFSET + CLOCK_CONFIG4, CLOCK_DUTY_50_PERCENT);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Write32 (
             Context,
             SIMPLE_DISPLAY_CLOCK_OFFSET + CLOCK_CONFIG23,
             CLOCK_CONFIG23_LOAD | CLOCK_CONFIG23_SADDR
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return WaitForBits (
           Context,
           SIMPLE_DISPLAY_CLOCK_OFFSET + CLOCK_CONFIG23,
           CLOCK_CONFIG23_LOAD,
           0,
           1000000,
           1000
           );
}

STATIC
EFI_STATUS
ResetVdmaChannel (
  IN SIMPLE_DISPLAY_HW_CONTEXT  *Context,
  IN UINT64                     Channel
  )
{
  EFI_STATUS  Status;

  Status = Write32 (
             Context,
             SIMPLE_DISPLAY_VDMA_OFFSET + Channel + VDMA_CONTROL,
             VDMA_CONTROL_RESET
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return WaitForBits (
           Context,
           SIMPLE_DISPLAY_VDMA_OFFSET + Channel + VDMA_CONTROL,
           VDMA_CONTROL_RESET,
           0,
           10000,
           10
           );
}

STATIC
EFI_STATUS
ConfigureVdma (
  IN SIMPLE_DISPLAY_HW_CONTEXT  *Context,
  IN CONST SIMPLE_DISPLAY_MODE  *Mode
  )
{
  EFI_STATUS  Status;
  UINT32      Control;
  UINT32      LineBytes;
  UINT32      VdmaStatus;

  LineBytes = Mode->HorizontalActive * SIMPLE_DISPLAY_BYTES_PER_PIXEL;

  Status = ResetVdmaChannel (Context, VDMA_S2MM_CHANNEL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = ResetVdmaChannel (Context, VDMA_MM2S_CHANNEL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Write32 (
             Context,
             SIMPLE_DISPLAY_VDMA_OFFSET + VDMA_MM2S_CHANNEL + VDMA_STATUS,
             VDMA_STATUS_ERRORS | VDMA_STATUS_INTERRUPTS
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Write32 (
             Context,
             SIMPLE_DISPLAY_VDMA_OFFSET + VDMA_MM2S_CHANNEL + VDMA_FRAME_STORE,
             1
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Control = VDMA_CONTROL_TAIL | VDMA_CONTROL_SYNC | VDMA_CONTROL_INTERNAL_GENLOCK;
  Status  = Write32 (
              Context,
              SIMPLE_DISPLAY_VDMA_OFFSET + VDMA_MM2S_CHANNEL + VDMA_CONTROL,
              Control
              );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Write32 (
             Context,
             SIMPLE_DISPLAY_VDMA_OFFSET + VDMA_MM2S_ADDRESS + VDMA_HORIZONTAL_SIZE,
             LineBytes
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Write32 (
             Context,
             SIMPLE_DISPLAY_VDMA_OFFSET + VDMA_MM2S_ADDRESS + VDMA_STRIDE,
             LineBytes
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Write32 (
             Context,
             SIMPLE_DISPLAY_VDMA_OFFSET + VDMA_MM2S_ADDRESS + VDMA_START_ADDRESS,
             SIMPLE_DISPLAY_DDR_FRAME_ADDRESS
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Write32 (
             Context,
             SIMPLE_DISPLAY_VDMA_OFFSET + VDMA_MM2S_CHANNEL + VDMA_CONTROL,
             Control | VDMA_CONTROL_RUN
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Write32 (
             Context,
             SIMPLE_DISPLAY_VDMA_OFFSET + VDMA_MM2S_ADDRESS + VDMA_VERTICAL_SIZE,
             Mode->VerticalActive
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Read32 (
             Context,
             SIMPLE_DISPLAY_VDMA_OFFSET + VDMA_MM2S_CHANNEL + VDMA_STATUS,
             &VdmaStatus
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return ((VdmaStatus & VDMA_STATUS_ERRORS) == 0) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

STATIC
UINT32
VtcPack (
  IN UINT32  Start,
  IN UINT32  End
  )
{
  return (Start & 0x3FFFU) | ((End & 0x3FFFU) << 16);
}

STATIC
EFI_STATUS
ConfigureVtc (
  IN SIMPLE_DISPLAY_HW_CONTEXT  *Context,
  IN CONST SIMPLE_DISPLAY_MODE  *Mode
  )
{
  EFI_STATUS  Status;
  UINT32      Control;
  UINT32      Polarity;
  UINT32      VSyncStart;
  UINT32      VSyncEnd;

  VSyncStart = Mode->VerticalSyncStart - 1;
  VSyncEnd   = Mode->VerticalSyncEnd - 1;
  Polarity   = VTC_POLARITY_VBLANK | VTC_POLARITY_HBLANK |
               VTC_POLARITY_ACTIVE_VIDEO | VTC_POLARITY_ACTIVE_CHROMA |
               VTC_POLARITY_FIELD_ID;
  if (Mode->PositiveHSync) {
    Polarity |= VTC_POLARITY_HSYNC;
  }

  if (Mode->PositiveVSync) {
    Polarity |= VTC_POLARITY_VSYNC;
  }

#define VTC_WRITE(Register, Value) \
  do { \
    Status = Write32 (Context, SIMPLE_DISPLAY_VTC_OFFSET + (Register), (Value)); \
    if (EFI_ERROR (Status)) { \
      return Status; \
    } \
  } while (FALSE)

  VTC_WRITE (VTC_HORIZONTAL_SIZE, Mode->HorizontalTotal);
  VTC_WRITE (VTC_VERTICAL_SIZE, Mode->VerticalTotal | (Mode->VerticalTotal << 16));
  VTC_WRITE (VTC_ACTIVE_SIZE, Mode->HorizontalActive | (Mode->VerticalActive << 16));
  VTC_WRITE (VTC_ACTIVE_SIZE_F1, Mode->VerticalActive << 16);
  VTC_WRITE (VTC_HORIZONTAL_SYNC, VtcPack (Mode->HorizontalSyncStart, Mode->HorizontalSyncEnd));
  VTC_WRITE (VTC_VERTICAL_SYNC, VtcPack (VSyncStart, VSyncEnd));
  VTC_WRITE (VTC_VERTICAL_SYNC_F1, VtcPack (VSyncStart, VSyncEnd));
  VTC_WRITE (VTC_VERTICAL_BLANK_OFFSET, VtcPack (Mode->HorizontalActive, Mode->HorizontalActive));
  VTC_WRITE (VTC_VERTICAL_BLANK_OFFSET_F1, VtcPack (Mode->HorizontalActive, Mode->HorizontalActive));
  VTC_WRITE (VTC_VERTICAL_SYNC_OFFSET, VtcPack (Mode->HorizontalSyncStart, Mode->HorizontalSyncStart));
  VTC_WRITE (VTC_VERTICAL_SYNC_OFFSET_F1, VtcPack (Mode->HorizontalSyncStart, Mode->HorizontalSyncStart));
  VTC_WRITE (VTC_FRAME_ENCODING, 0);
  VTC_WRITE (VTC_POLARITY, Polarity);

#undef VTC_WRITE

  Status = Read32 (Context, SIMPLE_DISPLAY_VTC_OFFSET + VTC_CONTROL, &Control);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Control &= ~VTC_CONTROL_ALL_SOURCE_SELECT;
  Control |= VTC_CONTROL_SOFTWARE_ENABLE |
             VTC_CONTROL_GENERATOR_ENABLE |
             VTC_CONTROL_REGISTER_UPDATE;
  return Write32 (Context, SIMPLE_DISPLAY_VTC_OFFSET + VTC_CONTROL, Control);
}

EFI_STATUS
EFIAPI
SimpleDisplayHwProgramMode (
  IN SIMPLE_DISPLAY_HW_CONTEXT  *Context,
  IN CONST SIMPLE_DISPLAY_MODE  *Mode
  )
{
  EFI_STATUS  Status;
  UINT32      Value;

  Status = SimpleDisplayHwValidateModeBounds (Context, Mode);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = ConfigureGpio (Context);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = ConfigurePixelUnpack (Context);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = ConfigureColor (Context);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = ConfigureHdmi (Context);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = ConfigureClock (Context, Mode);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = WaitForBits (
             Context,
             SIMPLE_DISPLAY_CLOCK_OFFSET + CLOCK_STATUS,
             0,
             CLOCK_STATUS_LOCKED,
             1000000,
             1000
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = ConfigureVdma (Context, Mode);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = ConfigureVtc (Context, Mode);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Read32 (Context, SIMPLE_DISPLAY_VTC_OFFSET + VTC_ERROR, &Value);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return (Value == 0) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS
EFIAPI
SimpleDisplayHwFillColorBars (
  IN SIMPLE_DISPLAY_HW_CONTEXT  *Context,
  IN CONST SIMPLE_DISPLAY_MODE  *Mode
  )
{
  STATIC CONST UINT32  Colors[8] = {
    0x00FF0000U, 0x0000FF00U, 0x000000FFU, 0x00FFFFFFU,
    0x0000FFFFU, 0x00FF00FFU, 0x00FFFF00U, 0x00000000U
  };
  EFI_STATUS  Status;
  UINT32      Pixels[256];
  UINT32      Band;
  UINT32      Chunk;
  UINT32      Count;
  UINT32      X;
  UINT32      Y;
  UINT64      Offset;

  Status = SimpleDisplayHwValidateModeBounds (Context, Mode);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (Y = 0; Y < Mode->VerticalActive; Y++) {
    Band = (Y * 8U) / Mode->VerticalActive;
    for (X = 0; X < sizeof (Pixels) / sizeof (Pixels[0]); X++) {
      Pixels[X] = Colors[Band];
    }

    for (X = 0; X < Mode->HorizontalActive; X += Count) {
      Count = Mode->HorizontalActive - X;
      if (Count > sizeof (Pixels) / sizeof (Pixels[0])) {
        Count = sizeof (Pixels) / sizeof (Pixels[0]);
      }

      Offset = SIMPLE_DISPLAY_FRAMEBUFFER_OFFSET +
               ((UINT64)Y * Mode->HorizontalActive + X) * sizeof (UINT32);
      Chunk  = Count;
      Status = Context->PciIo->Mem.Write (
                                     Context->PciIo,
                                     EfiPciIoWidthUint32,
                                     (UINT8)Context->BarIndex,
                                     Offset,
                                     Chunk,
                                     Pixels
                                     );
      if (EFI_ERROR (Status)) {
        return Status;
      }
    }
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SimpleDisplayHwReadStatus (
  IN  SIMPLE_DISPLAY_HW_CONTEXT  *Context,
  OUT SIMPLE_DISPLAY_STATUS      *Status
  )
{
  EFI_STATUS  Result;

  if ((Context == NULL) || (Status == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Status, sizeof (*Status));

#define READ_STATUS(Member, Offset) \
  do { \
    Result = Read32 (Context, (Offset), &Status->Member); \
    if (EFI_ERROR (Result)) { \
      return Result; \
    } \
  } while (FALSE)

  READ_STATUS (GpioChannel1, SIMPLE_DISPLAY_GPIO_OFFSET + GPIO_DATA_CHANNEL1);
  READ_STATUS (GpioChannel2, SIMPLE_DISPLAY_GPIO_OFFSET + GPIO_DATA_CHANNEL2);
  READ_STATUS (ClockStatus, SIMPLE_DISPLAY_CLOCK_OFFSET + CLOCK_STATUS);
  READ_STATUS (ClockConfig0, SIMPLE_DISPLAY_CLOCK_OFFSET + CLOCK_CONFIG0);
  READ_STATUS (ClockConfig2, SIMPLE_DISPLAY_CLOCK_OFFSET + CLOCK_CONFIG2);
  READ_STATUS (VdmaMm2sControl, SIMPLE_DISPLAY_VDMA_OFFSET + VDMA_MM2S_CHANNEL + VDMA_CONTROL);
  READ_STATUS (VdmaMm2sStatus, SIMPLE_DISPLAY_VDMA_OFFSET + VDMA_MM2S_CHANNEL + VDMA_STATUS);
  READ_STATUS (VdmaS2mmControl, SIMPLE_DISPLAY_VDMA_OFFSET + VDMA_S2MM_CHANNEL + VDMA_CONTROL);
  READ_STATUS (VdmaS2mmStatus, SIMPLE_DISPLAY_VDMA_OFFSET + VDMA_S2MM_CHANNEL + VDMA_STATUS);
  READ_STATUS (VdmaParkPointer, SIMPLE_DISPLAY_VDMA_OFFSET + VDMA_PARK_POINTER);
  READ_STATUS (VtcControl, SIMPLE_DISPLAY_VTC_OFFSET + VTC_CONTROL);
  READ_STATUS (VtcInterruptStatus, SIMPLE_DISPLAY_VTC_OFFSET + VTC_INTERRUPT_STATUS);
  READ_STATUS (VtcError, SIMPLE_DISPLAY_VTC_OFFSET + VTC_ERROR);
  READ_STATUS (VtcGeneratorStatus, SIMPLE_DISPLAY_VTC_OFFSET + VTC_GENERATOR_STATUS);
  READ_STATUS (IicStatus, SIMPLE_DISPLAY_IIC_OFFSET + IIC_STATUS);
  READ_STATUS (PixelUnpackMode, SIMPLE_DISPLAY_PIXEL_UNPACK_OFFSET + PIXEL_UNPACK_MODE);

#undef READ_STATUS

  return EFI_SUCCESS;
}
