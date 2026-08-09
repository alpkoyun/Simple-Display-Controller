/** @file
  Hardware access library for the Simple Display Shell application and GOP.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef SIMPLE_DISPLAY_HW_LIB_H_
#define SIMPLE_DISPLAY_HW_LIB_H_

#include <Uefi.h>
#include <Protocol/PciIo.h>
#include <SimpleDisplayHardware.h>

typedef struct {
  EFI_PCI_IO_PROTOCOL    *PciIo;
  UINTN                  BarIndex;
  EFI_PHYSICAL_ADDRESS   BarBase;
  UINT64                 BarLength;
  UINT32                 AbiVersion;
  UINT32                 AbiFeatures;
  UINT32                 RomApertureBytes;
} SIMPLE_DISPLAY_HW_CONTEXT;

typedef struct {
  UINT32  Original[4];
  UINT32  Readback[4];
  UINT32  Restored[4];
} SIMPLE_DISPLAY_SCRATCH_RESULT;

typedef struct {
  UINT32  GpioChannel1;
  UINT32  GpioChannel2;
  UINT32  ClockStatus;
  UINT32  ClockConfig0;
  UINT32  ClockConfig2;
  UINT32  VdmaMm2sControl;
  UINT32  VdmaMm2sStatus;
  UINT32  VdmaS2mmControl;
  UINT32  VdmaS2mmStatus;
  UINT32  VdmaParkPointer;
  UINT32  VtcControl;
  UINT32  VtcInterruptStatus;
  UINT32  VtcError;
  UINT32  VtcGeneratorStatus;
  UINT32  IicStatus;
  UINT32  PixelUnpackMode;
} SIMPLE_DISPLAY_STATUS;

EFI_STATUS
EFIAPI
SimpleDisplayHwMatchPci (
  IN  EFI_PCI_IO_PROTOCOL  *PciIo,
  OUT UINT8                *Revision OPTIONAL
  );

EFI_STATUS
EFIAPI
SimpleDisplayHwInitializeContext (
  IN  EFI_PCI_IO_PROTOCOL       *PciIo,
  OUT SIMPLE_DISPLAY_HW_CONTEXT *Context
  );

EFI_STATUS
EFIAPI
SimpleDisplayHwScratchTest (
  IN  SIMPLE_DISPLAY_HW_CONTEXT   *Context,
  OUT SIMPLE_DISPLAY_SCRATCH_RESULT *Result
  );

EFI_STATUS
EFIAPI
SimpleDisplayHwProgramMode (
  IN SIMPLE_DISPLAY_HW_CONTEXT  *Context,
  IN CONST SIMPLE_DISPLAY_MODE  *Mode
  );

EFI_STATUS
EFIAPI
SimpleDisplayHwFillColorBars (
  IN SIMPLE_DISPLAY_HW_CONTEXT  *Context,
  IN CONST SIMPLE_DISPLAY_MODE  *Mode
  );

EFI_STATUS
EFIAPI
SimpleDisplayHwReadStatus (
  IN  SIMPLE_DISPLAY_HW_CONTEXT  *Context,
  OUT SIMPLE_DISPLAY_STATUS      *Status
  );

UINTN
EFIAPI
SimpleDisplayHwFrameBytes (
  IN CONST SIMPLE_DISPLAY_MODE  *Mode
  );

EFI_STATUS
EFIAPI
SimpleDisplayHwValidateModeBounds (
  IN CONST SIMPLE_DISPLAY_HW_CONTEXT  *Context,
  IN CONST SIMPLE_DISPLAY_MODE        *Mode
  );

#endif
