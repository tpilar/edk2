/** @file
  GTDT table parser

  Copyright (c) 2016 - 2020, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent

  @par Reference(s):
    - ACPI 6.3 Specification - January 2019
  **/

#include <IndustryStandard/Acpi.h>
#include "AcpiParser.h"
#include "AcpiCrossValidator.h"
#include "AcpiTableParser.h"
#include "AcpiViewConfig.h"
#include "AcpiViewLog.h"

// "The number of GT Block Timers must be less than or equal to 8"
#define GT_BLOCK_TIMER_COUNT_MAX 8

/**
  Handler for each Platform Timer Structure type
**/
STATIC ACPI_STRUCT_INFO GtdtStructs[];

// Local variables
STATIC CONST UINT32* GtdtPlatformTimerCount;
STATIC CONST UINT32* GtdtPlatformTimerOffset;
STATIC CONST UINT8*  PlatformTimerType;
STATIC CONST UINT16* PlatformTimerLength;
STATIC CONST UINT32* GtBlockTimerCount;
STATIC CONST UINT32* GtBlockTimerOffset;
STATIC ACPI_DESCRIPTION_HEADER_INFO AcpiHdrInfo;

/**
  This function validates the GT Block timer count.

  @param [in] Ptr     Pointer to the start of the field data.
  @param [in] Context Pointer to context specific information e.g. this
                      could be a pointer to the ACPI table header.
**/
STATIC
VOID
EFIAPI
ValidateGtBlockTimerCount (
  IN UINT8* Ptr,
  IN VOID*  Context
  )
{
  UINT32 BlockTimerCount;

  BlockTimerCount = *(UINT32*)Ptr;
  AssertConstraint (L"ACPI", BlockTimerCount <= GT_BLOCK_TIMER_COUNT_MAX);
}

/**
  This function validates the GT Frame Number.

  @param [in] Ptr     Pointer to the start of the field data.
  @param [in] Context Pointer to context specific information e.g. this
                      could be a pointer to the ACPI table header.
**/
STATIC
VOID
EFIAPI
ValidateGtFrameNumber (
  IN UINT8* Ptr,
  IN VOID*  Context
  )
{
  UINT8 GTFrameNumber;

  GTFrameNumber = *Ptr;
  AssertConstraint (L"ACPI", GTFrameNumber < GT_BLOCK_TIMER_COUNT_MAX);
}

/**
  An ACPI_PARSER array describing the ACPI GTDT Table.
**/
STATIC CONST ACPI_PARSER GtdtParser[] = {
  PARSE_ACPI_HEADER (&AcpiHdrInfo),
  {L"CntControlBase Physical Address", 8, 36, L"0x%lx", NULL, NULL,
   NULL, NULL},
  {L"Reserved", 4, 44, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Secure EL1 timer GSIV", 4, 48, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Secure EL1 timer FLAGS", 4, 52, L"0x%x", NULL, NULL, NULL, NULL},

  {L"Non-Secure EL1 timer GSIV", 4, 56, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Non-Secure EL1 timer FLAGS", 4, 60, L"0x%x", NULL, NULL, NULL, NULL},

  {L"Virtual timer GSIV", 4, 64, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Virtual timer FLAGS", 4, 68, L"0x%x", NULL, NULL, NULL, NULL},

  {L"Non-Secure EL2 timer GSIV", 4, 72, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Non-Secure EL2 timer FLAGS", 4, 76, L"0x%x", NULL, NULL, NULL, NULL},

  {L"CntReadBase Physical address", 8, 80, L"0x%lx", NULL, NULL, NULL, NULL},
  {L"Platform Timer Count", 4, 88, L"%d", NULL,
   (VOID**)&GtdtPlatformTimerCount, NULL, NULL},
  {L"Platform Timer Offset", 4, 92, L"0x%x", NULL,
   (VOID**)&GtdtPlatformTimerOffset, NULL, NULL},
  {L"Virtual EL2 Timer GSIV", 4, 96, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Virtual EL2 Timer Flags", 4, 100, L"0x%x", NULL, NULL, NULL, NULL}
};

/**
  An ACPI_PARSER array describing the Platform timer header.
**/
STATIC CONST ACPI_PARSER GtPlatformTimerHeaderParser[] = {
  {L"Type", 1, 0, NULL, NULL, (VOID**)&PlatformTimerType, NULL, NULL},
  {L"Length", 2, 1, NULL, NULL, (VOID**)&PlatformTimerLength, NULL, NULL},
  {L"Reserved", 1, 3, NULL, NULL, NULL, NULL, NULL}
};

/**
  An ACPI_PARSER array describing the Platform GT Block.
**/
STATIC CONST ACPI_PARSER GtBlockParser[] = {
  {L"Type", 1, 0, L"%d", NULL, NULL, NULL, NULL},
  {L"Length", 2, 1, L"%d", NULL, NULL, NULL, NULL},
  {L"Reserved", 1, 3, L"%x", NULL, NULL, NULL, NULL},
  {L"Physical address (CntCtlBase)", 8, 4, L"0x%lx", NULL, NULL, NULL, NULL},
  {L"Timer Count", 4, 12, L"%d", NULL, (VOID**)&GtBlockTimerCount,
   ValidateGtBlockTimerCount, NULL},
  {L"Timer Offset", 4, 16, L"%d", NULL, (VOID**)&GtBlockTimerOffset, NULL,
    NULL}
};

/**
  An ACPI_PARSER array describing the GT Block timer.
**/
STATIC CONST ACPI_PARSER GtBlockTimerParser[] = {
  {L"Frame Number", 1, 0, L"%d", NULL, NULL, ValidateGtFrameNumber, NULL},
  {L"Reserved", 3, 1, L"%x %x %x", Dump3Chars, NULL, NULL, NULL},
  {L"Physical address (CntBaseX)", 8, 4, L"0x%lx", NULL, NULL, NULL, NULL},
  {L"Physical address (CntEL0BaseX)", 8, 12, L"0x%lx", NULL, NULL, NULL,
    NULL},
  {L"Physical Timer GSIV", 4, 20, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Physical Timer Flags", 4, 24, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Virtual Timer GSIV", 4, 28, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Virtual Timer Flags", 4, 32, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Common Flags", 4, 36, L"0x%x", NULL, NULL, NULL, NULL}
};

/**
  An ACPI_PARSER array describing the Platform Watchdog.
**/
STATIC CONST ACPI_PARSER SBSAGenericWatchdogParser[] = {
  {L"Type", 1, 0, L"%d", NULL, NULL, NULL, NULL},
  {L"Length", 2, 1, L"%d", NULL, NULL, NULL, NULL},
  {L"Reserved", 1, 3, L"%x", NULL, NULL, NULL, NULL},
  {L"RefreshFrame Physical address", 8, 4, L"0x%lx", NULL, NULL, NULL, NULL},
  {L"ControlFrame Physical address", 8, 12, L"0x%lx", NULL, NULL, NULL, NULL},
  {L"Watchdog Timer GSIV", 4, 20, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Watchdog Timer Flags", 4, 24, L"0x%x", NULL, NULL, NULL, NULL}
};

/**
  GT Frame Number comparator.

  @param[in] Frame1   The first GT Frame Number.
  @param[in] Frame2   The second GT Frame Number.

  @retval 0     Frame1 and Frame2 are equal.
  @retval -1    Frame1 and Frame2 are different.
**/
INTN
EFIAPI
GtFrameNumberCompare (
  CONST VOID *Frame1,
  CONST VOID *Frame2
  )
{
  if (*(UINT8*)Frame1 == *(UINT8*)Frame2) {
    return 0;
  } else {
    return -1;
  }
}

/**
  Validate that all GT Frame Numbers found in GT Block Timer structures
  are unique across the entire GT Block.

  @param [in] Ptr                 Pointer to the GTDT table.
  @param [in] Length              Length in bytes to scan, starting from Offset into Ptr.
  @param [in] Offset              Offset from the start of GT Block to the list
                                  of GT Block Timers.
  @param [in] Count               Number of GT Block Timers in this block.

  @retval EFI_SUCCESS             All GT Frame Numbers are unique.
  @retval EFI_INVALID_PARAMETER   One or more duplicate GT Frame Numbers.
  @retval EFI_OUT_OF_RESOURCES    Memory allocation failed.
**/
STATIC
EFI_STATUS
EFIAPI
ValidateGtFrameNumbersUnique (
  IN UINT8*            Ptr,
  IN UINT16            Length,
  IN UINT16            Offset,
  IN UINT32            Count,
  IN UINTN             FieldOffset,
  IN UINTN             FieldSize
  )
{
  BOOLEAN               AllUnique;
  LIST_ENTRY            UniqueList;

  InitializeListHead(&UniqueList);

  while ((Count-- > 0) && (Offset < Length)) {
    AcpiCrossValidatorAdd (
      &UniqueList,
      Ptr + Offset + FieldOffset,
      FieldSize,
      EFI_ACPI_6_3_GTDT_GT_BLOCK,
      Offset + FieldOffset
      );

    Offset += sizeof (EFI_ACPI_6_3_GTDT_GT_BLOCK_TIMER_STRUCTURE);
  }

  AllUnique = AcpiCrossValidatorAllUnique (
                &UniqueList,
                GtFrameNumberCompare,
                "GT Block Timer",
                L"GT Frame Number"
                );

  AcpiCrossValidatorDelete (&UniqueList);
  return AllUnique ? EFI_SUCCESS : EFI_INVALID_PARAMETER;
}

/**
  Common signature for Platform Timer Structure parsers.

  @param [in] Ptr     Pointer to the start of the Platform Timer data.
  @param [in] Length  Length of the Platform Timer structure.
*/
typedef VOID (*PLATFORM_TIMER_PARSER) (
  IN UINT8* Ptr,
  IN UINT16 Length
  );

/**
  This function parses the Platform GT Block.

  @param [in] Ptr         Pointer to the start of structure's buffer.
  @param [in] Length      Length of the buffer.
**/
STATIC
VOID
DumpGTBlock (
  IN UINT8* Ptr,
  IN UINT32 Length
  )
{
  UINT32 Index;
  UINT32 Offset;

  ParseAcpi (
    TRUE,
    2,
    NULL,
    Ptr,
    Length,
    PARSER_PARAMS (GtBlockParser)
    );

  // Check if the values used to control the parsing logic have been
  // successfully read.
  if ((GtBlockTimerCount == NULL) ||
      (GtBlockTimerOffset == NULL)) {
    AcpiError (ACPI_ERROR_PARSE, L"Failed to parse GT Block Structure");
    return;
  }

  Offset = *GtBlockTimerOffset;
  Index = 0;

  // Parse the specified number of GT Block Timer Structures or the GT Block
  // Structure buffer length. Whichever is minimum.
  while ((Index < *GtBlockTimerCount) &&
         (Offset < Length)) {
    AcpiLog(ACPI_ITEM, L"  GT Block Timer[%d] (+0x%x)", Index, Offset);
    Offset += ParseAcpi (
                TRUE,
                4,
                NULL,
                Ptr + Offset,
                Length - Offset,
                PARSER_PARAMS (GtBlockTimerParser)
                );
    Index++;
  }

  if (mConfig.ConsistencyCheck)  {
    ValidateGtFrameNumbersUnique (
      Ptr,
      Length,
      *GtBlockTimerOffset,
      *GtBlockTimerCount,
      OFFSET_OF (EFI_ACPI_6_3_GTDT_GT_BLOCK_TIMER_STRUCTURE, GTFrameNumber),
      FIELD_SIZE_OF (
        EFI_ACPI_6_3_GTDT_GT_BLOCK_TIMER_STRUCTURE, GTFrameNumber)
      );
  }
}

/**
  Information about each Platform Timer Structure type.
**/
STATIC ACPI_STRUCT_INFO GtdtStructs[] = {
  ADD_ACPI_STRUCT_INFO_FUNC (
    "GT Block",
    EFI_ACPI_6_3_GTDT_GT_BLOCK,
    ARCH_COMPAT_ARM | ARCH_COMPAT_AARCH64,
    DumpGTBlock
    ),
  ADD_ACPI_STRUCT_INFO_ARRAY (
    "SBSA Generic Watchdog",
    EFI_ACPI_6_3_GTDT_SBSA_GENERIC_WATCHDOG,
    ARCH_COMPAT_ARM | ARCH_COMPAT_AARCH64,
    SBSAGenericWatchdogParser
    )
};

/**
  GTDT structure database
**/
STATIC ACPI_STRUCT_DATABASE GtdtDatabase = {
  "Platform Timer Structure",
  GtdtStructs,
  ARRAY_SIZE (GtdtStructs)
};

/**
  This function parses the ACPI GTDT table.
  When trace is enabled this function parses the GTDT table and
  traces the ACPI table fields.

  This function also parses the following platform timer structures:
    - GT Block timer
    - Watchdog timer

  This function also performs validation of the ACPI table fields.

  @param [in] Trace              If TRUE, trace the ACPI fields.
  @param [in] Ptr                Pointer to the start of the buffer.
  @param [in] AcpiTableLength    Length of the ACPI table.
  @param [in] AcpiTableRevision  Revision of the ACPI table.
**/
VOID
EFIAPI
ParseAcpiGtdt (
  IN BOOLEAN Trace,
  IN UINT8*  Ptr,
  IN UINT32  AcpiTableLength,
  IN UINT8   AcpiTableRevision
  )
{
  UINT32 Index;
  UINT32 Offset;

  if (!Trace) {
    return;
  }

  ResetAcpiStructCounts (&GtdtDatabase);

  ParseAcpi (
    TRUE,
    0,
    "GTDT",
    Ptr,
    AcpiTableLength,
    PARSER_PARAMS (GtdtParser)
    );

  // Check if the values used to control the parsing logic have been
  // successfully read.
  if ((GtdtPlatformTimerCount == NULL) || (GtdtPlatformTimerOffset == NULL)) {
    AcpiError (ACPI_ERROR_PARSE, L"Corrupt Platform Timer Table");
    return;
  }

  Offset = *GtdtPlatformTimerOffset;
  Index = 0;

  // Parse the specified number of Platform Timer Structures or the GTDT
  // buffer length. Whichever is minimum.
  while ((Index++ < *GtdtPlatformTimerCount) &&
         (Offset < AcpiTableLength)) {
    // Parse the Platform Timer Header to obtain Length and Type
    ParseAcpi (
      FALSE,
      0,
      NULL,
      Ptr + Offset,
      AcpiTableLength - Offset,
      PARSER_PARAMS (GtPlatformTimerHeaderParser)
      );

    // Check if the values used to control the parsing logic have been
    // successfully read.
    if ((PlatformTimerType == NULL) || (PlatformTimerLength == NULL)) {
      AcpiError (ACPI_ERROR_PARSE, L"Corrupt Platform Timer Structure");
      return;
    }

    // Validate Platform Timer Structure length
    if (AssertMemberIntegrity(Offset, *PlatformTimerLength, Ptr, AcpiTableLength)) {
      return;
    }

    // Parse the Platform Timer Structure
    ParseAcpiStruct (
      2,
      Ptr + Offset,
      &GtdtDatabase,
      Offset,
      *PlatformTimerType,
      *PlatformTimerLength
      );

    Offset += *PlatformTimerLength;
  } // while

  // Report and validate Platform Timer Type Structure counts
  if (mConfig.ConsistencyCheck) {
    ValidateAcpiStructCounts (&GtdtDatabase);
  }
}
