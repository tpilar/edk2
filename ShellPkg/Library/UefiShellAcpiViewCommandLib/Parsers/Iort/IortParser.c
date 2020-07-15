/** @file
  IORT table parser

  Copyright (c) 2016 - 2020, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent

  @par Reference(s):
    - IO Remapping Table, Platform Design Document, Revision D, March 2018
**/

#include <IndustryStandard/IoRemappingTable.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include "AcpiParser.h"
#include "AcpiTableParser.h"
#include "AcpiViewConfig.h"
#include "AcpiViewLog.h"

// Local variables
STATIC ACPI_DESCRIPTION_HEADER_INFO AcpiHdrInfo;

STATIC CONST UINT32* IortNodeCount;
STATIC CONST UINT32* IortNodeOffset;

STATIC CONST UINT8*  IortNodeType;
STATIC CONST UINT16* IortNodeLength;
STATIC CONST UINT32* IortIdMappingCount;
STATIC CONST UINT32* IortIdMappingOffset;

STATIC CONST UINT32* InterruptContextCount;
STATIC CONST UINT32* InterruptContextOffset;
STATIC CONST UINT32* PmuInterruptCount;
STATIC CONST UINT32* PmuInterruptOffset;

STATIC CONST UINT32* ItsCount;

/**
  Handler for each IORT Node type
**/
STATIC ACPI_STRUCT_INFO IortStructs[];

/**
  This function validates the ID Mapping array count for the ITS node.

  @param [in] Ptr     Pointer to the start of the field data.
  @param [in] Context Pointer to context specific information e.g. this
                      could be a pointer to the ACPI table header.
**/
STATIC
VOID
EFIAPI
ValidateItsIdMappingCount (
  IN UINT8* Ptr,
  IN VOID*  Context
  )
{
  UINT32 ItsNodeIdMapping;

  ItsNodeIdMapping = *(UINT32 *) Ptr;
  AssertConstraint (L"ACPI", ItsNodeIdMapping == 0);
}

/**
  This function validates the ID Mapping array count for the Performance
  Monitoring Counter Group (PMCG) node.

  @param [in] Ptr     Pointer to the start of the field data.
  @param [in] Context Pointer to context specific information e.g. this
                      could be a pointer to the ACPI table header.
**/
STATIC
VOID
EFIAPI
ValidatePmcgIdMappingCount (
  IN UINT8* Ptr,
  IN VOID*  Context
  )
{
  UINT32 PmcgNodeIdMapping;

  PmcgNodeIdMapping = *(UINT32 *) Ptr;
  AssertConstraint (L"ACPI", PmcgNodeIdMapping <= 1);
}

/**
  This function validates the ID Mapping array offset for the ITS node.

  @param [in] Ptr     Pointer to the start of the field data.
  @param [in] Context Pointer to context specific information e.g. this
                      could be a pointer to the ACPI table header.
**/
STATIC
VOID
EFIAPI
ValidateItsIdArrayReference (
  IN UINT8* Ptr,
  IN VOID*  Context
  )
{
  UINT32 ItsNodeMappingArrayOffset;

  ItsNodeMappingArrayOffset = *(UINT32 *) Ptr;
  AssertConstraint (L"ACPI", ItsNodeMappingArrayOffset == 0);
}

/**
  Helper Macro for populating the IORT Node header in the ACPI_PARSER array.

  @param [out] ValidateIdMappingCount    Optional pointer to a function for
                                         validating the ID Mapping count.
  @param [out] ValidateIdArrayReference  Optional pointer to a function for
                                         validating the ID Array reference.
**/
#define PARSE_IORT_NODE_HEADER(ValidateIdMappingCount,                   \
                               ValidateIdArrayReference)                 \
  { L"Type", 1, 0, L"%d", NULL, (VOID**)&IortNodeType, NULL, NULL },     \
  { L"Length", 2, 1, L"%d", NULL, (VOID**)&IortNodeLength, NULL, NULL }, \
  { L"Revision", 1, 3, L"%d", NULL, NULL, NULL, NULL },                  \
  { L"Reserved", 4, 4, L"0x%x", NULL, NULL, NULL, NULL },                \
  { L"Number of ID mappings", 4, 8, L"%d", NULL,                         \
    (VOID**)&IortIdMappingCount, ValidateIdMappingCount, NULL },         \
  { L"Reference to ID Array", 4, 12, L"0x%x", NULL,                      \
    (VOID**)&IortIdMappingOffset, ValidateIdArrayReference, NULL }

/**
  An ACPI_PARSER array describing the ACPI IORT Table
**/
STATIC CONST ACPI_PARSER IortParser[] = {
  PARSE_ACPI_HEADER (&AcpiHdrInfo),
  {L"Number of IORT Nodes", 4, 36, L"%d", NULL,
   (VOID**)&IortNodeCount, NULL, NULL},
  {L"Offset to Array of IORT Nodes", 4, 40, L"0x%x", NULL,
   (VOID**)&IortNodeOffset, NULL, NULL},
  {L"Reserved", 4, 44, L"0x%x", NULL, NULL, NULL, NULL}
};

/**
  An ACPI_PARSER array describing the IORT node header structure.
**/
STATIC CONST ACPI_PARSER IortNodeHeaderParser[] = {
  PARSE_IORT_NODE_HEADER (NULL, NULL)
};

/**
  An ACPI_PARSER array describing the IORT SMMUv1/2 node.
**/
STATIC CONST ACPI_PARSER IortNodeSmmuV1V2Parser[] = {
  PARSE_IORT_NODE_HEADER (NULL, NULL),
  {L"Base Address", 8, 16, L"0x%lx", NULL, NULL, NULL, NULL},
  {L"Span", 8, 24, L"0x%lx", NULL, NULL, NULL, NULL},
  {L"Model", 4, 32, L"%d", NULL, NULL, NULL, NULL},
  {L"Flags", 4, 36, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Reference to Global Interrupt Array", 4, 40, L"0x%x", NULL, NULL, NULL,
   NULL},
  {L"Number of context interrupts", 4, 44, L"%d", NULL,
   (VOID**)&InterruptContextCount, NULL, NULL},
  {L"Reference to Context Interrupt Array", 4, 48, L"0x%x", NULL,
   (VOID**)&InterruptContextOffset, NULL, NULL},
  {L"Number of PMU Interrupts", 4, 52, L"%d", NULL,
   (VOID**)&PmuInterruptCount, NULL, NULL},
  {L"Reference to PMU Interrupt Array", 4, 56, L"0x%x", NULL,
   (VOID**)&PmuInterruptOffset, NULL, NULL},

  // Interrupt Array
  {L"SMMU_NSgIrpt", 4, 60, L"0x%x", NULL, NULL, NULL, NULL},
  {L"SMMU_NSgIrpt interrupt flags", 4, 64, L"0x%x", NULL, NULL, NULL, NULL},
  {L"SMMU_NSgCfgIrpt", 4, 68, L"0x%x", NULL, NULL, NULL, NULL},
  {L"SMMU_NSgCfgIrpt interrupt flags", 4, 72, L"0x%x", NULL, NULL, NULL, NULL}
};

/**
  An ACPI_PARSER array describing the SMMUv1/2 Node Interrupt Array.
**/
STATIC CONST ACPI_PARSER InterruptArrayParser[] = {
  {L"Interrupt GSIV", 4, 0, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Flags", 4, 4, L"0x%x", NULL, NULL, NULL, NULL}
};

/**
  An ACPI_PARSER array describing the IORT ID Mapping.
**/
STATIC CONST ACPI_PARSER IortNodeIdMappingParser[] = {
  {L"Input base", 4, 0, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Number of IDs", 4, 4, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Output base", 4, 8, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Output reference", 4, 12, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Flags", 4, 16, L"0x%x", NULL, NULL, NULL, NULL}
};

/**
  An ACPI_PARSER array describing the IORT SMMUv3 node.
**/
STATIC CONST ACPI_PARSER IortNodeSmmuV3Parser[] = {
  PARSE_IORT_NODE_HEADER (NULL, NULL),
  {L"Base Address", 8, 16, L"0x%lx", NULL, NULL, NULL, NULL},
  {L"Flags", 4, 24, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Reserved", 4, 28, L"0x%x", NULL, NULL, NULL, NULL},
  {L"VATOS Address", 8, 32, L"0x%lx", NULL, NULL, NULL, NULL},
  {L"Model", 4, 40, L"%d", NULL, NULL, NULL, NULL},
  {L"Event", 4, 44, L"0x%x", NULL, NULL, NULL, NULL},
  {L"PRI", 4, 48, L"0x%x", NULL, NULL, NULL, NULL},
  {L"GERR", 4, 52, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Sync", 4, 56, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Proximity domain", 4, 60, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Device ID mapping index", 4, 64, L"%d", NULL, NULL, NULL, NULL}
};

/**
  An ACPI_PARSER array describing the IORT ITS node.
**/
STATIC CONST ACPI_PARSER IortNodeItsParser[] = {
  PARSE_IORT_NODE_HEADER (
    ValidateItsIdMappingCount,
    ValidateItsIdArrayReference
    ),
  {L"Number of ITSs", 4, 16, L"%d", NULL, (VOID**)&ItsCount, NULL}
};

/**
  An ACPI_PARSER array describing the ITS ID.
**/
STATIC CONST ACPI_PARSER ItsIdParser[] = {
  { L"GIC ITS Identifier", 4, 0, L"%d", NULL, NULL, NULL }
};

/**
  An ACPI_PARSER array describing the IORT Names Component node.
**/
STATIC CONST ACPI_PARSER IortNodeNamedComponentParser[] = {
  PARSE_IORT_NODE_HEADER (NULL, NULL),
  {L"Node Flags", 4, 16, L"%d", NULL, NULL, NULL, NULL},
  {L"Memory access properties", 8, 20, L"0x%lx", NULL, NULL, NULL, NULL},
  {L"Device memory address size limit", 1, 28, L"%d", NULL, NULL, NULL, NULL}
};

/**
  An ACPI_PARSER array describing the IORT Root Complex node.
**/
STATIC CONST ACPI_PARSER IortNodeRootComplexParser[] = {
  PARSE_IORT_NODE_HEADER (NULL, NULL),
  {L"Memory access properties", 8, 16, L"0x%lx", NULL, NULL, NULL, NULL},
  {L"ATS Attribute", 4, 24, L"0x%x", NULL, NULL, NULL, NULL},
  {L"PCI Segment number", 4, 28, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Memory access size limit", 1, 32, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Reserved", 3, 33, L"%x %x %x", Dump3Chars, NULL, NULL, NULL}
};

/**
  An ACPI_PARSER array describing the IORT PMCG node.
**/
STATIC CONST ACPI_PARSER IortNodePmcgParser[] = {
  PARSE_IORT_NODE_HEADER (ValidatePmcgIdMappingCount, NULL),
  {L"Page 0 Base Address", 8, 16, L"0x%lx", NULL, NULL, NULL, NULL},
  {L"Overflow interrupt GSIV", 4, 24, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Node reference", 4, 28, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Page 1 Base Address", 8, 32, L"0x%lx", NULL, NULL, NULL, NULL}
};

/**
  This function parses the IORT Node Id Mapping array.

  @param [in] Ptr            Pointer to the start of the ID mapping array.
  @param [in] Length         Length of the buffer.
  @param [in] MappingCount   The ID Mapping count.
**/
STATIC
VOID
DumpIortNodeIdMappings (
  IN UINT8* Ptr,
  IN UINT32 Length,
  IN UINT32 MappingCount
  )
{
  UINT32 Index;
  UINT32 Offset;

  Offset = 0;
  for (Index = 0; Index < MappingCount; Index++) {
    if (AssertMemberIntegrity(Offset, 1, Ptr, Length)) {
      return;
    }

    AcpiLog (ACPI_ITEM, L"    ID Mapping[%d] (+0x%x)", Index, Offset);
    Offset += ParseAcpi (
                TRUE,
                4,
                NULL,
                Ptr + Offset,
                Length - Offset,
                PARSER_PARAMS (IortNodeIdMappingParser));
  }
}

/**
  This function parses the IORT SMMUv1/2 node.

  @param [in] Ptr            Pointer to the start of the buffer.
  @param [in] Length         Length of the buffer.
**/
STATIC
VOID
DumpIortNodeSmmuV1V2 (
  IN       UINT8* Ptr,
  IN       UINT32 Length
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
    PARSER_PARAMS (IortNodeSmmuV1V2Parser)
    );

  // Check if the values used to control the parsing logic have been
  // successfully read.
  if ((InterruptContextCount == NULL)   ||
      (InterruptContextOffset == NULL)  ||
      (PmuInterruptCount == NULL)       ||
      (PmuInterruptOffset == NULL)) {
    AcpiError (ACPI_ERROR_PARSE, L"Failed to parse the SMMUv1/2 node");
    return;
  }

  Offset = *InterruptContextOffset;
  for (Index = 0; Index < *InterruptContextCount; Index++) {
    if (AssertMemberIntegrity(Offset, 1, Ptr, Length)) {
      break;
    }

    AcpiLog (
      ACPI_ITEM, L"    Context Interrupts Array[%d] (+0x%x)", Index, Offset);
    Offset += ParseAcpi (
                TRUE,
                4,
                NULL,
                Ptr + Offset,
                Length - Offset,
                PARSER_PARAMS (InterruptArrayParser));
  }

  Offset = *PmuInterruptOffset;
  for(Index = 0; Index < *PmuInterruptCount; Index++) {
    if (AssertMemberIntegrity(Offset, 1, Ptr, Length)){
      break;
    }

    AcpiLog (ACPI_ITEM, L"    PMU Interrupts Array[%d] (+0x%x)", Index, Offset);
    Offset += ParseAcpi (
      TRUE,
      4,
      NULL,
      Ptr + Offset,
      Length - Offset,
      PARSER_PARAMS (InterruptArrayParser));
  }

  DumpIortNodeIdMappings (
    Ptr + *IortIdMappingOffset,
    Length - *IortIdMappingOffset,
    *IortIdMappingCount
    );
}

/**
  This function parses the IORT SMMUv3 node.

  @param [in] Ptr            Pointer to the start of the buffer.
  @param [in] Length         Length of the buffer.
**/
STATIC
VOID
DumpIortNodeSmmuV3 (
  IN       UINT8* Ptr,
  IN       UINT32 Length
  )
{
  ParseAcpi (
    TRUE,
    2,
    NULL,
    Ptr,
    Length,
    PARSER_PARAMS (IortNodeSmmuV3Parser)
    );

  DumpIortNodeIdMappings (
    Ptr + *IortIdMappingOffset,
    Length - *IortIdMappingOffset,
    *IortIdMappingCount
    );
}

/**
  This function parses the IORT ITS node.

  ITS nodes have no ID mappings.

  @param [in] Ptr            Pointer to the start of the buffer.
  @param [in] Length         Length of the buffer.
**/
STATIC
VOID
DumpIortNodeIts (
  IN       UINT8* Ptr,
  IN       UINT32 Length
  )
{
  UINT32 Offset;
  UINT32 Index;

  Offset = ParseAcpi (
            TRUE,
            2,
            NULL,
            Ptr,
            Length,
            PARSER_PARAMS (IortNodeItsParser)
            );

  // Check if the values used to control the parsing logic have been
  // successfully read.
  if (ItsCount == NULL) {
    AcpiError (ACPI_ERROR_PARSE, L"Failed to parse ITS node");
    return;
  }

  for (Index = 0; Index < *ItsCount; Index++) {
    if (AssertMemberIntegrity(Offset, 1, Ptr, Length)) {
      return;
    }

    AcpiLog (
      ACPI_ITEM, L"    GIC ITS Identifier Array[%d] (+0x%x)", Index, Offset);
    Offset += ParseAcpi (
      TRUE,
      4,
      NULL,
      Ptr + Offset,
      Length - Offset,
      PARSER_PARAMS (ItsIdParser));
  }

  // Note: ITS does not have the ID Mappings Array

}

/**
  This function parses the IORT Named Component node.

  @param [in] Ptr            Pointer to the start of the buffer.
  @param [in] Length         Length of the buffer.
**/
STATIC
VOID
DumpIortNodeNamedComponent (
  IN       UINT8* Ptr,
  IN       UINT32 Length
  )
{
  UINT32 Offset;

  Offset = ParseAcpi (
             TRUE,
             2,
             NULL,
             Ptr,
             Length,
             PARSER_PARAMS (IortNodeNamedComponentParser)
             );

  // Estimate the Device Name length
  PrintFieldName (2, L"Device Object Name");
  AcpiInfo (
    L"%.*a",
    AsciiStrnLenS ((CONST CHAR8 *)Ptr + Offset, Length - Offset),
    Ptr + Offset);

  DumpIortNodeIdMappings (
    Ptr + *IortIdMappingOffset,
    Length - *IortIdMappingOffset,
    *IortIdMappingCount
    );
}

/**
  This function parses the IORT Root Complex node.

  @param [in] Ptr            Pointer to the start of the buffer.
  @param [in] Length         Length of the buffer.
**/
STATIC
VOID
DumpIortNodeRootComplex (
  IN       UINT8* Ptr,
  IN       UINT32 Length
  )
{
  ParseAcpi (
    TRUE,
    2,
    NULL,
    Ptr,
    Length,
    PARSER_PARAMS (IortNodeRootComplexParser)
    );

  DumpIortNodeIdMappings (
    Ptr + *IortIdMappingOffset,
    Length - *IortIdMappingOffset,
    *IortIdMappingCount
    );
}

/**
  This function parses the IORT PMCG node.

  @param [in] Ptr            Pointer to the start of the buffer.
  @param [in] Length         Length of the buffer.
**/
STATIC
VOID
DumpIortNodePmcg (
  IN       UINT8* Ptr,
  IN       UINT32 Length
  )
{
  ParseAcpi (
    TRUE,
    2,
    NULL,
    Ptr,
    Length,
    PARSER_PARAMS (IortNodePmcgParser)
    );

  DumpIortNodeIdMappings (
    Ptr + *IortIdMappingOffset,
    Length - *IortIdMappingOffset,
    *IortIdMappingCount
    );
}

/**
  Information about each IORT Node type
**/
STATIC ACPI_STRUCT_INFO IortStructs[] = {
  ADD_ACPI_STRUCT_INFO_FUNC (
    "ITS Group",
    EFI_ACPI_IORT_TYPE_ITS_GROUP,
    ARCH_COMPAT_ARM | ARCH_COMPAT_AARCH64,
    DumpIortNodeIts
    ),
  ADD_ACPI_STRUCT_INFO_FUNC (
    "Named Component",
    EFI_ACPI_IORT_TYPE_NAMED_COMP,
    ARCH_COMPAT_ARM | ARCH_COMPAT_AARCH64,
    DumpIortNodeNamedComponent
    ),
  ADD_ACPI_STRUCT_INFO_FUNC (
    "Root Complex",
    EFI_ACPI_IORT_TYPE_ROOT_COMPLEX,
    ARCH_COMPAT_ARM | ARCH_COMPAT_AARCH64,
    DumpIortNodeRootComplex
    ),
  ADD_ACPI_STRUCT_INFO_FUNC (
    "SMMUv1 or SMMUv2",
    EFI_ACPI_IORT_TYPE_SMMUv1v2,
    ARCH_COMPAT_ARM | ARCH_COMPAT_AARCH64,
    DumpIortNodeSmmuV1V2
    ),
  ADD_ACPI_STRUCT_INFO_FUNC (
    "SMMUv3",
    EFI_ACPI_IORT_TYPE_SMMUv3,
    ARCH_COMPAT_ARM | ARCH_COMPAT_AARCH64,
    DumpIortNodeSmmuV3
    ),
  ADD_ACPI_STRUCT_INFO_FUNC (
    "PMCG",
    EFI_ACPI_IORT_TYPE_PMCG,
    ARCH_COMPAT_ARM | ARCH_COMPAT_AARCH64,
    DumpIortNodePmcg
    )
};

/**
  IORT structure database
**/
STATIC ACPI_STRUCT_DATABASE IortDatabase = {
  "IORT Node",
  IortStructs,
  ARRAY_SIZE (IortStructs)
};

/**
  This function parses the ACPI IORT table.
  When trace is enabled this function parses the IORT table and traces the ACPI fields.

  This function also parses the following nodes:
    - ITS Group
    - Named Component
    - Root Complex
    - SMMUv1/2
    - SMMUv3
    - PMCG

  This function also performs validation of the ACPI table fields.

  @param [in] Trace              If TRUE, trace the ACPI fields.
  @param [in] Ptr                Pointer to the start of the buffer.
  @param [in] AcpiTableLength    Length of the ACPI table.
  @param [in] AcpiTableRevision  Revision of the ACPI table.
**/
VOID
EFIAPI
ParseAcpiIort (
  IN BOOLEAN Trace,
  IN UINT8*  Ptr,
  IN UINT32  AcpiTableLength,
  IN UINT8   AcpiTableRevision
  )
{
  UINT32 Offset;
  UINT32 Index;

  if (!Trace) {
    return;
  }

  ResetAcpiStructCounts (&IortDatabase);

  ParseAcpi (
    TRUE,
    0,
    "IORT",
    Ptr,
    AcpiTableLength,
    PARSER_PARAMS (IortParser)
    );

  // Check if the values used to control the parsing logic have been
  // successfully read.
  if ((IortNodeCount == NULL) ||
      (IortNodeOffset == NULL)) {
    AcpiError (ACPI_ERROR_PARSE, L"Failed to parse IORT Node.");
    return;
  }

  Offset = *IortNodeOffset;
  Index = 0;

  // Parse the specified number of IORT nodes or the IORT table buffer length.
  // Whichever is minimum.
  while ((Index++ < *IortNodeCount) &&
         (Offset < AcpiTableLength)) {
    // Parse the IORT Node Header
    ParseAcpi (
      FALSE,
      0,
      "IORT Node Header",
      Ptr + Offset,
      AcpiTableLength - Offset,
      PARSER_PARAMS (IortNodeHeaderParser)
      );

    // Check if the values used to control the parsing logic have been
    // successfully read.
    if ((IortNodeType == NULL)        ||
        (IortNodeLength == NULL)      ||
        (IortIdMappingCount == NULL)  ||
        (IortIdMappingOffset == NULL)) {
      AcpiError (ACPI_ERROR_PARSE, L"Failed to parse the IORT node header");
      return;
    }

    // Protect against buffer overrun
    if (AssertMemberIntegrity (Offset, *IortNodeLength, Ptr, AcpiTableLength)) {
      return;
    }

    // Parse the IORT Node
    ParseAcpiStruct (
      2, Ptr + Offset, &IortDatabase, Offset, *IortNodeType, *IortNodeLength);

    Offset += *IortNodeLength;
  } // while

  // Report and validate IORT Node counts
  if (mConfig.ConsistencyCheck) {
    ValidateAcpiStructCounts (&IortDatabase);
  }
}
