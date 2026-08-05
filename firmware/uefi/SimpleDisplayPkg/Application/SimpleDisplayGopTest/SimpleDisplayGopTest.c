/** @file
  Protocol-level GOP test. The test intentionally changes the visible output.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Protocol/EdidActive.h>
#include <Protocol/EdidDiscovered.h>
#include <Protocol/GraphicsOutput.h>

#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <SimpleDisplayHardware.h>

#define TEST_X       32U
#define TEST_Y       32U
#define TEST_WIDTH   12U
#define TEST_HEIGHT  8U

STATIC
EFI_STATUS
FindSimpleDisplayGop (
  OUT EFI_GRAPHICS_OUTPUT_PROTOCOL  **Gop,
  OUT EFI_EDID_DISCOVERED_PROTOCOL  **Discovered,
  OUT EFI_EDID_ACTIVE_PROTOCOL      **Active
  )
{
  EFI_STATUS                    Status;
  EFI_HANDLE                    *Handles;
  UINTN                         HandleCount;
  UINTN                         Index;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Candidate;
  EFI_EDID_DISCOVERED_PROTOCOL  *CandidateDiscovered;
  EFI_EDID_ACTIVE_PROTOCOL      *CandidateActive;

  Handles     = NULL;
  HandleCount = 0;
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiGraphicsOutputProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = EFI_NOT_FOUND;
  for (Index = 0; Index < HandleCount; Index++) {
    Candidate           = NULL;
    CandidateDiscovered = NULL;
    CandidateActive     = NULL;
    if (EFI_ERROR (gBS->HandleProtocol (
                          Handles[Index],
                          &gEfiGraphicsOutputProtocolGuid,
                          (VOID **)&Candidate
                          )))
    {
      continue;
    }

    if (EFI_ERROR (gBS->HandleProtocol (
                          Handles[Index],
                          &gEfiEdidDiscoveredProtocolGuid,
                          (VOID **)&CandidateDiscovered
                          )) ||
        EFI_ERROR (gBS->HandleProtocol (
                          Handles[Index],
                          &gEfiEdidActiveProtocolGuid,
                          (VOID **)&CandidateActive
                          )))
    {
      continue;
    }

    if ((Candidate->Mode == NULL) ||
        (Candidate->Mode->MaxMode != SIMPLE_DISPLAY_GOP_MODE_COUNT) ||
        (Candidate->Mode->Info == NULL) ||
        (Candidate->Mode->Info->PixelFormat != PixelBltOnly) ||
        (Candidate->Mode->FrameBufferBase != 0) ||
        (Candidate->Mode->FrameBufferSize != 0) ||
        (CandidateDiscovered->SizeOfEdid != 0) ||
        (CandidateDiscovered->Edid != NULL) ||
        (CandidateActive->SizeOfEdid != 0) ||
        (CandidateActive->Edid != NULL))
    {
      continue;
    }

    *Gop        = Candidate;
    *Discovered = CandidateDiscovered;
    *Active     = CandidateActive;
    Status      = EFI_SUCCESS;
    break;
  }

  FreePool (Handles);
  return Status;
}

STATIC
EFI_STATUS
TestModes (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop
  )
{
  EFI_STATUS                            Status;
  UINT32                                ModeNumber;
  UINTN                                 SizeOfInfo;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info;

  if (Gop->Mode->MaxMode != SIMPLE_DISPLAY_GOP_MODE_COUNT) {
    Print (
      L"MODE_FAIL MaxMode=%u expected=%u\n",
      Gop->Mode->MaxMode,
      SIMPLE_DISPLAY_GOP_MODE_COUNT
      );
    return EFI_COMPROMISED_DATA;
  }

  for (ModeNumber = 0; ModeNumber < Gop->Mode->MaxMode; ModeNumber++) {
    Info       = NULL;
    SizeOfInfo = 0;
    Status     = Gop->QueryMode (Gop, ModeNumber, &SizeOfInfo, &Info);
    if (EFI_ERROR (Status) || (Info == NULL) ||
        (SizeOfInfo != sizeof (*Info)) ||
        (Info->Version != 0) ||
        (Info->HorizontalResolution != gSimpleDisplayModes[ModeNumber].HorizontalActive) ||
        (Info->VerticalResolution != gSimpleDisplayModes[ModeNumber].VerticalActive) ||
        (Info->PixelFormat != PixelBltOnly) ||
        (Info->PixelsPerScanLine != gSimpleDisplayModes[ModeNumber].HorizontalActive))
    {
      if (Info != NULL) {
        FreePool (Info);
      }

      Print (L"MODE_FAIL query %u: %r\n", ModeNumber, Status);
      return EFI_COMPROMISED_DATA;
    }

    Print (
      L"MODE_INFO %u %ux%u format=%u ppsl=%u\n",
      ModeNumber,
      Info->HorizontalResolution,
      Info->VerticalResolution,
      Info->PixelFormat,
      Info->PixelsPerScanLine
      );
    FreePool (Info);

    Status = Gop->SetMode (Gop, ModeNumber);
    if (EFI_ERROR (Status)) {
      Print (L"MODE_FAIL SetMode(%u): %r\n", ModeNumber, Status);
      return Status;
    }

    if ((Gop->Mode->Mode != ModeNumber) ||
        (Gop->Mode->Info == NULL) ||
        (Gop->Mode->Info->PixelFormat != PixelBltOnly) ||
        (Gop->Mode->FrameBufferBase != 0) ||
        (Gop->Mode->FrameBufferSize != 0))
    {
      Print (
        L"MODE_FAIL SetMode(%u) exposed framebuffer base=0x%lx size=0x%lx\n",
        ModeNumber,
        Gop->Mode->FrameBufferBase,
        (UINT64)Gop->Mode->FrameBufferSize
        );
      return EFI_COMPROMISED_DATA;
    }
  }

  Info   = NULL;
  Status = Gop->QueryMode (Gop, Gop->Mode->MaxMode, &SizeOfInfo, &Info);
  if (Status != EFI_INVALID_PARAMETER) {
    Print (L"MODE_FAIL invalid QueryMode returned %r\n", Status);
    if (Info != NULL) {
      FreePool (Info);
    }

    return EFI_COMPROMISED_DATA;
  }

  Status = Gop->SetMode (Gop, Gop->Mode->MaxMode);
  if (Status != EFI_UNSUPPORTED) {
    Print (L"MODE_FAIL invalid SetMode returned %r\n", Status);
    return EFI_COMPROMISED_DATA;
  }

  Print (L"MODE_TEST_PASS\n");
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
TestBufferTransfers (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop
  )
{
  EFI_STATUS                         Status;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL      Guard;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL      Source[8 * 6];
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL      Readback[TEST_WIDTH * TEST_HEIGHT];
  UINTN                              X;
  UINTN                              Y;
  UINTN                              Index;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL      Expected;

  Guard.Blue     = 0xA5;
  Guard.Green    = 0x5A;
  Guard.Red      = 0xC3;
  Guard.Reserved = 0;
  Status = Gop->Blt (
                  Gop,
                  &Guard,
                  EfiBltVideoFill,
                  0,
                  0,
                  TEST_X,
                  TEST_Y,
                  TEST_WIDTH,
                  TEST_HEIGHT,
                  0
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (Index = 0; Index < sizeof (Source) / sizeof (Source[0]); Index++) {
    Source[Index].Blue     = (UINT8)(Index + 1);
    Source[Index].Green    = (UINT8)(0x80 + Index);
    Source[Index].Red      = (UINT8)(0xF0 - Index);
    Source[Index].Reserved = 0;
  }

  Status = Gop->Blt (
                  Gop,
                  Source,
                  EfiBltBufferToVideo,
                  2,
                  1,
                  TEST_X + 2,
                  TEST_Y + 2,
                  4,
                  3,
                  8 * sizeof (Source[0])
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  SetMem (Readback, sizeof (Readback), 0xCC);
  Status = Gop->Blt (
                  Gop,
                  Readback,
                  EfiBltVideoToBltBuffer,
                  TEST_X,
                  TEST_Y,
                  0,
                  0,
                  TEST_WIDTH,
                  TEST_HEIGHT,
                  TEST_WIDTH * sizeof (Readback[0])
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (Y = 0; Y < TEST_HEIGHT; Y++) {
    for (X = 0; X < TEST_WIDTH; X++) {
      if ((X >= 2) && (X < 6) && (Y >= 2) && (Y < 5)) {
        Expected = Source[(Y - 2 + 1) * 8 + (X - 2 + 2)];
      } else {
        Expected = Guard;
      }

      if (CompareMem (
            &Readback[Y * TEST_WIDTH + X],
            &Expected,
            sizeof (Expected)
            ) != 0)
      {
        UINT32  ActualValue;
        UINT32  ExpectedValue;

        CopyMem (&ActualValue, &Readback[Y * TEST_WIDTH + X], sizeof (ActualValue));
        CopyMem (&ExpectedValue, &Expected, sizeof (ExpectedValue));
        Print (
          L"BLT_FAIL transfer/guard mismatch x=%u y=%u actual=0x%08x expected=0x%08x\n",
          X,
          Y,
          ActualValue,
          ExpectedValue
          );
        return EFI_COMPROMISED_DATA;
      }
    }
  }

  Print (L"BLT_BUFFER_TRANSFER_PASS\n");
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
TestOverlappingVideoCopies (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop
  )
{
  EFI_STATUS                     Status;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Source[4 * 6];
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Readback[4 * 6];
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Expected[4 * 6];
  UINTN                          Index;

  for (Index = 0; Index < sizeof (Source) / sizeof (Source[0]); Index++) {
    Source[Index].Blue     = (UINT8)Index;
    Source[Index].Green    = (UINT8)(Index + 0x20);
    Source[Index].Red      = (UINT8)(Index + 0x40);
    Source[Index].Reserved = 0;
  }

  CopyMem (Expected, Source, sizeof (Expected));
  CopyMem (&Expected[4], &Expected[0], 4 * 4 * sizeof (Expected[0]));
  Status = Gop->Blt (
                  Gop,
                  Source,
                  EfiBltBufferToVideo,
                  0,
                  0,
                  TEST_X + 20,
                  TEST_Y,
                  4,
                  6,
                  4 * sizeof (Source[0])
                  );
  if (!EFI_ERROR (Status)) {
    Status = Gop->Blt (
                    Gop,
                    NULL,
                    EfiBltVideoToVideo,
                    TEST_X + 20,
                    TEST_Y,
                    TEST_X + 20,
                    TEST_Y + 1,
                    4,
                    4,
                    0
                    );
  }

  if (!EFI_ERROR (Status)) {
    Status = Gop->Blt (
                    Gop,
                    Readback,
                    EfiBltVideoToBltBuffer,
                    TEST_X + 20,
                    TEST_Y,
                    0,
                    0,
                    4,
                    6,
                    4 * sizeof (Readback[0])
                    );
  }

  if (EFI_ERROR (Status) || (CompareMem (Expected, Readback, sizeof (Expected)) != 0)) {
    Print (L"BLT_FAIL downward overlapping VideoToVideo: %r\n", Status);
    return EFI_COMPROMISED_DATA;
  }

  CopyMem (Expected, Source, sizeof (Expected));
  CopyMem (&Expected[0], &Expected[4], 4 * 4 * sizeof (Expected[0]));
  Status = Gop->Blt (
                  Gop,
                  Source,
                  EfiBltBufferToVideo,
                  0,
                  0,
                  TEST_X + 20,
                  TEST_Y,
                  4,
                  6,
                  4 * sizeof (Source[0])
                  );
  if (!EFI_ERROR (Status)) {
    Status = Gop->Blt (
                    Gop,
                    NULL,
                    EfiBltVideoToVideo,
                    TEST_X + 20,
                    TEST_Y + 1,
                    TEST_X + 20,
                    TEST_Y,
                    4,
                    4,
                    0
                    );
  }

  if (!EFI_ERROR (Status)) {
    Status = Gop->Blt (
                    Gop,
                    Readback,
                    EfiBltVideoToBltBuffer,
                    TEST_X + 20,
                    TEST_Y,
                    0,
                    0,
                    4,
                    6,
                    4 * sizeof (Readback[0])
                    );
  }

  if (EFI_ERROR (Status) || (CompareMem (Expected, Readback, sizeof (Expected)) != 0)) {
    Print (L"BLT_FAIL upward overlapping VideoToVideo: %r\n", Status);
    return EFI_COMPROMISED_DATA;
  }

  Print (L"BLT_OVERLAP_PASS\n");
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
TestInvalidBltRequests (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop
  )
{
  EFI_STATUS                     Status;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Pixel;

  ZeroMem (&Pixel, sizeof (Pixel));

#define EXPECT_INVALID(Call, Name) \
  do { \
    Status = (Call); \
    if (Status != EFI_INVALID_PARAMETER) { \
      Print (L"BLT_FAIL " Name L" returned %r\n", Status); \
      return EFI_COMPROMISED_DATA; \
    } \
  } while (FALSE)

  EXPECT_INVALID (
    Gop->Blt (Gop, &Pixel, EfiBltVideoFill, 0, 0, 0, 0, 0, 1, 0),
    L"zero width"
    );
  EXPECT_INVALID (
    Gop->Blt (
           Gop,
           &Pixel,
           EfiBltVideoFill,
           0,
           0,
           Gop->Mode->Info->HorizontalResolution,
           0,
           1,
           1,
           0
           ),
    L"out of bounds"
    );
  EXPECT_INVALID (
    Gop->Blt (Gop, &Pixel, EfiBltVideoFill, 0, 0, MAX_UINTN, 0, 2, 1, 0),
    L"coordinate overflow"
    );
  EXPECT_INVALID (
    Gop->Blt (Gop, NULL, EfiBltBufferToVideo, 0, 0, 0, 0, 1, 1, 0),
    L"null buffer"
    );
  EXPECT_INVALID (
    Gop->Blt (Gop, &Pixel, EfiBltBufferToVideo, 1, 1, 0, 0, 1, 1, 0),
    L"subrectangle with zero delta"
    );

#undef EXPECT_INVALID

  Print (L"BLT_INVALID_REQUEST_PASS\n");
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                    Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  EFI_EDID_DISCOVERED_PROTOCOL  *Discovered;
  EFI_EDID_ACTIVE_PROTOCOL      *Active;

  (VOID)ImageHandle;
  (VOID)SystemTable;
  Gop        = NULL;
  Discovered = NULL;
  Active     = NULL;

  Status = FindSimpleDisplayGop (&Gop, &Discovered, &Active);
  if (EFI_ERROR (Status)) {
    Print (L"GOP_TEST_FAIL: Simple Display GOP child not found: %r\n", Status);
    return Status;
  }

  Print (
    L"EDID_EMPTY_PASS discovered=%u/%p active=%u/%p\n",
    Discovered->SizeOfEdid,
    Discovered->Edid,
    Active->SizeOfEdid,
    Active->Edid
    );
  Status = TestModes (Gop);
  if (!EFI_ERROR (Status)) {
    Status = TestBufferTransfers (Gop);
  }

  if (!EFI_ERROR (Status)) {
    Status = TestOverlappingVideoCopies (Gop);
  }

  if (!EFI_ERROR (Status)) {
    Status = TestInvalidBltRequests (Gop);
  }

  if (EFI_ERROR (Status)) {
    Print (L"GOP_TEST_FAIL %r\n", Status);
    return Status;
  }

  Print (L"GOP_TEST_PASS\n");
  return EFI_SUCCESS;
}
