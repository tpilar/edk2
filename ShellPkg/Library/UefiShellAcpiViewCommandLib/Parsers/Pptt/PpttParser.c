/** @file
  PPTT table parser

  Copyright (c) 2019 - 2020, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent

  @par Reference(s):
    - ACPI 6.3 Specification - January 2019
    - ARM Architecture Reference Manual ARMv8 (D.a)
**/

#include <Library/PrintLib.h>
#include <Library/UefiLib.h>
#include "AcpiParser.h"
#include "AcpiView.h"
#include "AcpiViewConfig.h"
#include "AcpiViewLog.h"
#include "PpttParser.h"
#include "AcpiViewLog.h"
#include "AcpiCrossValidator.h"

// Local variables
STATIC CONST UINT8*  ProcessorTopologyStructureType;
STATIC CONST UINT8*  ProcessorTopologyStructureLength;
STATIC CONST UINT32* NumberOfPrivateResources;
STATIC ACPI_DESCRIPTION_HEADER_INFO AcpiHdrInfo;
STATIC LIST_ENTRY mRefList;

/**
  Handler for each Processor Topology Structure
**/
STATIC ACPI_STRUCT_INFO PpttStructs[];

/**
  This function validates the Cache Type Structure (Type 1) 'Number of sets'
  field.

  @param [in] Ptr       Pointer to the start of the field data.
  @param [in] Context   Pointer to context specific information e.g. this
                        could be a pointer to the ACPI table header.
**/
STATIC
VOID
EFIAPI
ValidateCacheNumberOfSets (
  IN UINT8* Ptr,
  IN VOID*  Context
  )
{
  UINT32 CacheNumberOfSets;

  CacheNumberOfSets = *(UINT32*) Ptr;
  AssertConstraint (L"ACPI", CacheNumberOfSets != 0);

#if defined(MDE_CPU_ARM) || defined (MDE_CPU_AARCH64)
  if (AssertConstraint (
        L"ARMv8.3-CCIDX",
        CacheNumberOfSets <= PPTT_ARM_CCIDX_CACHE_NUMBER_OF_SETS_MAX)) {
    return;
  }

  WarnConstraint (
    L"No-ARMv8.3-CCIDX", CacheNumberOfSets <= PPTT_ARM_CACHE_NUMBER_OF_SETS_MAX);
#endif
}

/**
  This function validates the Cache Type Structure (Type 1) 'Associativity'
  field.

  @param [in] Ptr       Pointer to the start of the field data.
  @param [in] Context   Pointer to context specific information e.g. this
                        could be a pointer to the ACPI table header.
**/
STATIC
VOID
EFIAPI
ValidateCacheAssociativity (
  IN UINT8* Ptr,
  IN VOID*  Context
  )
{
  UINT8 CacheAssociativity;

  CacheAssociativity = *Ptr;
  AssertConstraint (L"ACPI", CacheAssociativity != 0);
}

/**
  This function validates the Cache Type Structure (Type 1) Line size field.

  @param [in] Ptr     Pointer to the start of the field data.
  @param [in] Context Pointer to context specific information e.g. this
                      could be a pointer to the ACPI table header.
**/
STATIC
VOID
EFIAPI
ValidateCacheLineSize (
  IN UINT8* Ptr,
  IN VOID*  Context
  )
{
#if defined(MDE_CPU_ARM) || defined (MDE_CPU_AARCH64)
  // Reference: ARM Architecture Reference Manual ARMv8 (D.a)
  // Section D12.2.25: CCSIDR_EL1, Current Cache Size ID Register
  //   LineSize, bits [2:0]
  //     (Log2(Number of bytes in cache line)) - 4.

  UINT16 CacheLineSize;

  CacheLineSize = *(UINT16 *) Ptr;
  AssertConstraint (
    L"ARM",
    (CacheLineSize >= PPTT_ARM_CACHE_LINE_SIZE_MIN &&
     CacheLineSize <= PPTT_ARM_CACHE_LINE_SIZE_MAX));

  AssertConstraint (L"ARM", BitFieldCountOnes32 (CacheLineSize, 0, 15) == 1);
#endif
}

/**
  This function validates the Cache Type Structure (Type 1) Attributes field.

  @param [in] Ptr     Pointer to the start of the field data.
  @param [in] Context Pointer to context specific information e.g. this
                      could be a pointer to the ACPI table header.
**/
STATIC
VOID
EFIAPI
ValidateCacheAttributes (
  IN UINT8* Ptr,
  IN VOID*  Context
  )
{
  // Reference: Advanced Configuration and Power Interface (ACPI) Specification
  //            Version 6.2 Errata A, September 2017
  // Table 5-153: Cache Type Structure
  UINT8 Attributes;

  Attributes = *(UINT8 *) Ptr;
  AssertConstraint (L"ACPI", BitFieldCountOnes32 (Attributes, 5, 7) == 0);
}

/**
  This function validates the following PPTT table fields:
  - 'Parent' (Type 0)
  - 'Next Level of Cache' (Type 1)

  Check if the reference made is to a valid processor topology structure and
  that the link between the two types of PPTT structures is allowed by the
  ACPI specification.

  Also, check if by following the chain of references we enter an infinite loop.

  @param [in] Ptr       Pointer to the start of the field data.
  @param [in] Context   Pointer to context specific information e.g. this
                        could be a pointer to the ACPI table header.
**/
STATIC
VOID
EFIAPI
ValidateReference (
  IN UINT8* Ptr,
  IN VOID*  Context
  )
{
  UINT32                                  Reference;
  EFI_ACPI_6_3_PPTT_STRUCTURE_PROCESSOR * StructFound;
  UINTN                                   RefCount;
  UINT32                                  StructsVisited;
  LIST_ENTRY                              *Entry;
  ACPI_CROSS_ENTRY                        *Found;

  Reference = *(UINT32*)Ptr;

  if (Reference == 0) {
    return;
  }

  Entry = GetFirstNode(&mRefList);
  while (Entry != &mRefList) {
    Found = (ACPI_CROSS_ENTRY*)Entry;
    if (Found->Offset == Reference) {
      break;
    }
    Entry = GetNextNode(&mRefList, Entry);
  }

  if (Entry == &mRefList) {
    AcpiError (
      ACPI_ERROR_CROSS,
      L"Referenced offset 0x%x does contain a structure",
      Reference);
  }

  // Processor Hierarchy Node and Cache Structure share the same header.
  // This is why the following type cast is allowed.
  StructFound = Found->Buffer;

  if (StructFound->Type != *ProcessorTopologyStructureType) {
    AcpiError (
      ACPI_ERROR_CROSS,
      L"type %d structure can't reference type %d structure",
      *ProcessorTopologyStructureType,
      StructFound->Type
      );
    return;
  }

  // If a Type 0 structure being referenced is a 'leaf' node, referencing it is
  // not allowed.
  if ((StructFound->Type == EFI_ACPI_6_3_PPTT_TYPE_PROCESSOR) &&
      (StructFound->Flags.NodeIsALeaf == 1)) {
    AcpiError (
      ACPI_ERROR_CROSS,
      L"May not reference a 'leaf' Processor Hierarchy Node."
      );
    return;
  }

  RefCount = 0;
  Entry = GetFirstNode(&mRefList);
  while (Entry != &mRefList) {
    RefCount++;
    Entry = GetNextNode(&mRefList, Entry);
  }

  // Cycle detection works by following the 'Parent'/'Next Level of Cache'
  // reference until the we have reached a node which does not reference any
  // other. If we have made a number of jumps which is equal to the total number
  // of indexed PPTT structures, then we must be in a cycle.
  for (StructsVisited = 0; StructsVisited < RefCount; StructsVisited++) {
    Entry = GetFirstNode(&mRefList);
    while (Entry != &mRefList) {
      Found = (ACPI_CROSS_ENTRY *)Entry;

      // The following comparison works because 'Parent' and 'Next Level of Cache'
      // are both 4-byte fields at offset 8 in the respective PPTT structure types
      // they belong to.
      if (Found->Offset == StructFound->Parent) {
        break;
      }
      Entry = GetNextNode(&mRefList, Entry);
    }

    // The current item does not reference anything else - we are good
    if (Entry == &mRefList) {
      return;
    }

    StructFound = Found->Buffer;
  }

  AcpiError (ACPI_ERROR_CROSS, L"Reference loop detected");
}

/**
  An ACPI_PARSER array describing the ACPI PPTT Table.
**/
STATIC CONST ACPI_PARSER PpttParser[] = {
  PARSE_ACPI_HEADER (&AcpiHdrInfo)
};

/**
  An ACPI_PARSER array describing the processor topology structure header.
**/
STATIC CONST ACPI_PARSER ProcessorTopologyStructureHeaderParser[] = {
  {L"Type", 1, 0, NULL, NULL, (VOID**)&ProcessorTopologyStructureType,
   NULL, NULL},
  {L"Length", 1, 1, NULL, NULL, (VOID**)&ProcessorTopologyStructureLength,
   NULL, NULL},
  {L"Reserved", 2, 2, NULL, NULL, NULL, NULL, NULL}
};

/**
  An ACPI_PARSER array describing the Processor Hierarchy Node Structure - Type 0.
**/
STATIC CONST ACPI_PARSER ProcessorHierarchyNodeStructureParser[] = {
  {L"Type", 1, 0, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Length", 1, 1, L"%d", NULL, NULL, NULL, NULL},
  {L"Reserved", 2, 2, L"0x%x", NULL, NULL, NULL, NULL},

  {L"Flags", 4, 4, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Parent", 4, 8, L"0x%x", NULL, NULL, ValidateReference, NULL},
  {L"ACPI Processor ID", 4, 12, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Number of private resources", 4, 16, L"%d", NULL,
   (VOID**)&NumberOfPrivateResources, NULL, NULL}
};

/**
  An ACPI_PARSER array describing the Cache Type Structure - Type 1.
**/
STATIC CONST ACPI_PARSER CacheTypeStructureParser[] = {
  {L"Type", 1, 0, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Length", 1, 1, L"%d", NULL, NULL, NULL, NULL},
  {L"Reserved", 2, 2, L"0x%x", NULL, NULL, NULL, NULL},

  {L"Flags", 4, 4, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Next Level of Cache", 4, 8, L"0x%x", NULL, NULL, ValidateReference, NULL},
  {L"Size", 4, 12, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Number of sets", 4, 16, L"%d", NULL, NULL, ValidateCacheNumberOfSets, NULL},
  {L"Associativity", 1, 20, L"%d", NULL, NULL, ValidateCacheAssociativity, NULL},
  {L"Attributes", 1, 21, L"0x%x", NULL, NULL, ValidateCacheAttributes, NULL},
  {L"Line size", 2, 22, L"%d", NULL, NULL, ValidateCacheLineSize, NULL}
};

/**
  An ACPI_PARSER array describing the ID Type Structure - Type 2.
**/
STATIC CONST ACPI_PARSER IdStructureParser[] = {
  {L"Type", 1, 0, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Length", 1, 1, L"%d", NULL, NULL, NULL, NULL},
  {L"Reserved", 2, 2, L"0x%x", NULL, NULL, NULL, NULL},

  {L"VENDOR_ID", 4, 4, NULL, Dump4Chars, NULL, NULL, NULL},
  {L"LEVEL_1_ID", 8, 8, L"0x%x", NULL, NULL, NULL, NULL},
  {L"LEVEL_2_ID", 8, 16, L"0x%x", NULL, NULL, NULL, NULL},
  {L"MAJOR_REV", 2, 24, L"0x%x", NULL, NULL, NULL, NULL},
  {L"MINOR_REV", 2, 26, L"0x%x", NULL, NULL, NULL, NULL},
  {L"SPIN_REV", 2, 28, L"0x%x", NULL, NULL, NULL, NULL},
};

/**
  This function validates the Processor Hierarchy Node (Type 0) 'Private
  resources[N]' field.

  Check if the private resource belonging to the given Processor Hierarchy Node
  exists and is not of Type 0.

  @param [in] PrivResource    Offset of the private resource from the start of
                              the table.
**/
STATIC
VOID
EFIAPI
ValidatePrivateResource (
  IN UINT32     PrivResource
  )
{
  LIST_ENTRY       *Entry;
  ACPI_CROSS_ENTRY *Found;
  EFI_ACPI_6_3_PPTT_STRUCTURE_HEADER  *StructFound;

  Entry = GetFirstNode(&mRefList);
  while (Entry != &mRefList) {
    Found = (ACPI_CROSS_ENTRY *) Entry;
    if (Found->Offset == PrivResource) {
      break;
    }
    Entry = GetNextNode(&mRefList, Entry);
  }

  if (Entry == &mRefList) {
    AcpiError (
      ACPI_ERROR_CROSS,
      L"PPTT structure (offset=0x%x) does not exist.",
      PrivResource);
      return ;
  }

  StructFound = Found->Buffer;

  if ((StructFound->Type != EFI_ACPI_6_3_PPTT_TYPE_CACHE) &&
      (StructFound->Type != EFI_ACPI_6_3_PPTT_TYPE_ID)) {
    AcpiError (
      ACPI_ERROR_CROSS,
      L"Private resource (offset=0x%x) has bad type=%d (expected %d or %d)",
      PrivResource,
      StructFound->Type,
      EFI_ACPI_6_3_PPTT_TYPE_CACHE,
      EFI_ACPI_6_3_PPTT_TYPE_ID
      );
  }
}

/**
  This function parses the Processor Hierarchy Node Structure (Type 0).

  @param [in] Ptr     Pointer to the start of the Processor Hierarchy Node
                      Structure data.
  @param [in] Length  Length of the Processor Hierarchy Node Structure.
**/
STATIC
VOID
DumpProcessorHierarchyNodeStructure (
  IN       UINT8* Ptr,
  IN       UINT32 Length
  )
{
  UINT32 Offset;
  UINT32 Index;
  UINT32 *PrivResource;

  Offset = ParseAcpi (
    TRUE,
    2,
    NULL,
    Ptr,
    Length,
    PARSER_PARAMS (ProcessorHierarchyNodeStructureParser));

  // Check if the values used to control the parsing logic have been
  // successfully read.
  if(NumberOfPrivateResources == NULL) {
    AcpiError (ACPI_ERROR_PARSE, L"Failed to parse processor hierarchy");
    return;
  }

  // Parse the specified number of private resource references or the Processor
  // Hierarchy Node length. Whichever is minimum.
  for (Index = 0; Index < *NumberOfPrivateResources; Index++) {
    if (AssertMemberIntegrity (Offset, sizeof (UINT32), Ptr, Length)) {
      return;
    }
    PrivResource = (UINT32 *) (Ptr + Offset);

    PrintFieldName (4, L"Private resources [%d]", Index);
    AcpiInfo (L"0x%x", *PrivResource);

    if (mConfig.ConsistencyCheck) {
      ValidatePrivateResource(*PrivResource);
    }

    Offset += sizeof (UINT32);
  }
}

/**
  Information about each Processor Topology Structure type.
**/
STATIC ACPI_STRUCT_INFO PpttStructs[] = {
  ADD_ACPI_STRUCT_INFO_FUNC (
    "Processor",
    EFI_ACPI_6_3_PPTT_TYPE_PROCESSOR,
    ARCH_COMPAT_IA32 | ARCH_COMPAT_X64 | ARCH_COMPAT_ARM | ARCH_COMPAT_AARCH64,
    DumpProcessorHierarchyNodeStructure
    ),
  ADD_ACPI_STRUCT_INFO_ARRAY (
    "Cache",
    EFI_ACPI_6_3_PPTT_TYPE_CACHE,
    ARCH_COMPAT_IA32 | ARCH_COMPAT_X64 | ARCH_COMPAT_ARM | ARCH_COMPAT_AARCH64,
    CacheTypeStructureParser
    ),
  ADD_ACPI_STRUCT_INFO_ARRAY (
    "ID",
    EFI_ACPI_6_3_PPTT_TYPE_ID,
    ARCH_COMPAT_IA32 | ARCH_COMPAT_X64 | ARCH_COMPAT_ARM | ARCH_COMPAT_AARCH64,
    IdStructureParser
    )
};

/**
  PPTT structure database
**/
STATIC ACPI_STRUCT_DATABASE PpttDatabase = {
  "Processor Topology Structure",
  PpttStructs,
  ARRAY_SIZE (PpttStructs)
};

/**
  This function parses the ACPI PPTT table.
  When trace is enabled this function parses the PPTT table and
  traces the ACPI table fields.

  This function parses the following processor topology structures:
    - Processor hierarchy node structure (Type 0)
    - Cache Type Structure (Type 1)
    - ID structure (Type 2)

  This function also performs validation of the ACPI table fields.

  @param [in] Trace              If TRUE, trace the ACPI fields.
  @param [in] Ptr                Pointer to the start of the buffer.
  @param [in] AcpiTableLength    Length of the ACPI table.
  @param [in] AcpiTableRevision  Revision of the ACPI table.
**/
VOID
EFIAPI
ParseAcpiPptt (
  IN BOOLEAN Trace,
  IN UINT8*  Ptr,
  IN UINT32  AcpiTableLength,
  IN UINT8   AcpiTableRevision
  )
{
  UINT32 Offset;

  if (!Trace) {
    return;
  }

  ResetAcpiStructCounts (&PpttDatabase);
  InitializeListHead(&mRefList);

  // Run a first loop and do fatal checks and populate RefList
  Offset = ParseAcpi (
    FALSE, 0, "PPTT", Ptr, AcpiTableLength, PARSER_PARAMS (PpttParser));

  while (Offset < AcpiTableLength) {
    // Parse Processor Hierarchy Node Structure to obtain Type and Length.
    ParseAcpi (
      FALSE,
      0,
      NULL,
      Ptr + Offset,
      AcpiTableLength - Offset,
      PARSER_PARAMS (ProcessorTopologyStructureHeaderParser)
      );

    // Check if the values used to control the parsing logic have been
    // successfully read.
    if ((ProcessorTopologyStructureType == NULL) ||
        (ProcessorTopologyStructureLength == NULL)) {
      AcpiError (ACPI_ERROR_PARSE, L"Failed to parse processor topology");
      goto EXIT;
    }

    // Validate Processor Topology Structure length
    if (AssertMemberIntegrity (
          Offset, *ProcessorTopologyStructureLength, Ptr, AcpiTableLength)) {
      goto EXIT;
    }

    AcpiCrossValidatorAdd (
      &mRefList,
      Ptr + Offset,
      *ProcessorTopologyStructureLength,
      *ProcessorTopologyStructureType,
      Offset
      );

    Offset += *ProcessorTopologyStructureLength;
  } // while

   // Do second loop and validate the rest
  Offset = ParseAcpi (
    TRUE, 0, "PPTT", Ptr, AcpiTableLength, PARSER_PARAMS (PpttParser));

  while (Offset < AcpiTableLength) {
    // Parse Processor Hierarchy Node Structure to obtain Type and Length.
    ParseAcpi (
      FALSE,
      0,
      NULL,
      Ptr + Offset,
      AcpiTableLength - Offset,
      PARSER_PARAMS (ProcessorTopologyStructureHeaderParser)
      );

    // Parse the Processor Topology Structure
    ParseAcpiStruct (
      2,
      Ptr + Offset,
      &PpttDatabase,
      Offset,
      *ProcessorTopologyStructureType,
      *ProcessorTopologyStructureLength
      );

    Offset += *ProcessorTopologyStructureLength;
  } // while

  // Report and validate processor topology structure counts
  if (mConfig.ConsistencyCheck) {
    ValidateAcpiStructCounts (&PpttDatabase);
  }

EXIT:
  AcpiCrossValidatorDelete(&mRefList);
}
