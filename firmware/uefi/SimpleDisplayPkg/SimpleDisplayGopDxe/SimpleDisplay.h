/** @file
  Private definitions for SimpleDisplayGopDxe.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef SIMPLE_DISPLAY_GOP_DXE_H_
#define SIMPLE_DISPLAY_GOP_DXE_H_

#include <Uefi.h>

#include <Protocol/DevicePath.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/EdidActive.h>
#include <Protocol/EdidDiscovered.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/PciIo.h>

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/SimpleDisplayHwLib.h>

#define SIMPLE_DISPLAY_PRIVATE_SIGNATURE  SIGNATURE_32 ('S', 'D', 'G', 'P')

typedef struct {
  UINT32                                Signature;
  EFI_HANDLE                            ControllerHandle;
  EFI_HANDLE                            ChildHandle;
  EFI_PCI_IO_PROTOCOL                   *PciIo;
  EFI_DEVICE_PATH_PROTOCOL              *ParentDevicePath;
  UINT64                                OriginalPciAttributes;
  UINT16                                OriginalPciCommand;
  BOOLEAN                               PciAttributesSaved;
  BOOLEAN                               PciCommandSaved;
  BOOLEAN                               PciMemoryEnabled;
  BOOLEAN                               ChildPciIoOpened;
  SIMPLE_DISPLAY_HW_CONTEXT             Hardware;
  EFI_DEVICE_PATH_PROTOCOL              *DevicePath;
  EFI_GRAPHICS_OUTPUT_PROTOCOL          Gop;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE     GopMode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  GopInfo;
  EFI_EDID_DISCOVERED_PROTOCOL          EdidDiscovered;
  EFI_EDID_ACTIVE_PROTOCOL              EdidActive;
} SIMPLE_DISPLAY_PRIVATE;

#define SIMPLE_DISPLAY_PRIVATE_FROM_GOP(This) \
  CR (This, SIMPLE_DISPLAY_PRIVATE, Gop, SIMPLE_DISPLAY_PRIVATE_SIGNATURE)

#endif
