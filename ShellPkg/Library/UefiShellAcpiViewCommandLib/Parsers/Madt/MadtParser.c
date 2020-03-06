/** @file
  MADT table parser

  Copyright (c) 2016 - 2020, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent

  @par Reference(s):
    - ACPI 6.3 Specification - January 2019
    - Arm Generic Interrupt Controller Architecture Specification,
      GIC architecture version 3 and version 4, issue E
    - Arm Server Base System Architecture 5.0
**/

#include <IndustryStandard/Acpi.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include "AcpiCrossValidator.h"
#include "AcpiParser.h"
#include "AcpiTableParser.h"
#include "AcpiViewConfig.h"
#include "MadtParser.h"
#include "AcpiViewLog.h"

// Local Variables
STATIC CONST UINT8* MadtInterruptControllerType;
STATIC CONST UINT8* MadtInterruptControllerLength;
STATIC ACPI_DESCRIPTION_HEADER_INFO AcpiHdrInfo;

/**
  This function validates the System Vector Base in the GICD.

  @param [in] Ptr     Pointer to the start of the field data.
  @param [in] Context Pointer to context specific information e.g. this
                      could be a pointer to the ACPI table header.
**/
STATIC
VOID
EFIAPI
ValidateGICDSystemVectorBase (
  IN UINT8* Ptr,
  IN VOID*  Context
)
{
  UINT32 GicdSystemVectorBase;

  GicdSystemVectorBase = *(UINT32 *) Ptr;
  AssertConstraint (L"ACPI", GicdSystemVectorBase == 0);
}

/**
  This function validates the SPE Overflow Interrupt in the GICC.

  @param [in] Ptr     Pointer to the start of the field data.
  @param [in] Context Pointer to context specific information e.g. this
                      could be a pointer to the ACPI table header.
**/
STATIC
VOID
EFIAPI
ValidateSpeOverflowInterrupt (
  IN UINT8* Ptr,
  IN VOID*  Context
  )
{
  UINT16 SpeOverflowInterrupt;

  SpeOverflowInterrupt = *(UINT16 *) Ptr;

  // SPE not supported by this processor
  if (SpeOverflowInterrupt == 0) {
    return;
  }

  AssertConstraint (L"ACPI", SpeOverflowInterrupt >= ARM_PPI_ID_MIN);
  AssertConstraint (
    L"ACPI",
    (SpeOverflowInterrupt <= ARM_PPI_ID_MAX) ||
      (SpeOverflowInterrupt >= ARM_PPI_ID_EXTENDED_MIN));
  AssertConstraint (L"ACPI", SpeOverflowInterrupt <= ARM_PPI_ID_EXTENDED_MAX);
  WarnConstraint (L"SBSA", SpeOverflowInterrupt == ARM_PPI_ID_PMBIRQ);
}

/**
  An ACPI_PARSER array describing the GICC Interrupt Controller Structure.
**/
STATIC CONST ACPI_PARSER GicCParser[] = {
  {L"Type", 1, 0, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Length", 1, 1, L"%d", NULL, NULL, NULL, NULL},
  {L"Reserved", 2, 2, L"0x%x", NULL, NULL, NULL, NULL},

  {L"CPU Interface Number", 4, 4, L"0x%x", NULL, NULL, NULL, NULL},
  {L"ACPI Processor UID", 4, 8, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Flags", 4, 12, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Parking Protocol Version", 4, 16, L"0x%x", NULL, NULL, NULL, NULL},

  {L"Performance Interrupt GSIV", 4, 20, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Parked Address", 8, 24, L"0x%lx", NULL, NULL, NULL, NULL},
  {L"Physical Base Address", 8, 32, L"0x%lx", NULL, NULL, NULL, NULL},
  {L"GICV", 8, 40, L"0x%lx", NULL, NULL, NULL, NULL},
  {L"GICH", 8, 48, L"0x%lx", NULL, NULL, NULL, NULL},
  {L"VGIC Maintenance interrupt", 4, 56, L"0x%x", NULL, NULL, NULL, NULL},
  {L"GICR Base Address", 8, 60, L"0x%lx", NULL, NULL, NULL, NULL},
  {L"MPIDR", 8, 68, L"0x%lx", NULL, NULL, NULL, NULL},
  {L"Processor Power Efficiency Class", 1, 76, L"0x%x", NULL, NULL, NULL,
   NULL},
  {L"Reserved", 1, 77, L"0x%x", NULL, NULL, NULL, NULL},
  {L"SPE overflow Interrupt", 2, 78, L"0x%x", NULL, NULL,
    ValidateSpeOverflowInterrupt, NULL}
};

/**
  An ACPI_PARSER array describing the GICD Interrupt Controller Structure.
**/
STATIC CONST ACPI_PARSER GicDParser[] = {
  {L"Type", 1, 0, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Length", 1, 1, L"%d", NULL, NULL, NULL, NULL},
  {L"Reserved", 2, 2, L"0x%x", NULL, NULL, NULL, NULL},

  {L"GIC ID", 4, 4, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Physical Base Address", 8, 8, L"0x%lx", NULL, NULL, NULL, NULL},
  {L"System Vector Base", 4, 16, L"0x%x", NULL, NULL,
    ValidateGICDSystemVectorBase, NULL},
  {L"GIC Version", 1, 20, L"%d", NULL, NULL, NULL, NULL},
  {L"Reserved", 3, 21, L"%x %x %x", Dump3Chars, NULL, NULL, NULL}
};

/**
  An ACPI_PARSER array describing the MSI Frame Interrupt Controller Structure.
**/
STATIC CONST ACPI_PARSER GicMSIFrameParser[] = {
  {L"Type", 1, 0, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Length", 1, 1, L"%d", NULL, NULL, NULL, NULL},
  {L"Reserved", 2, 2, L"0x%x", NULL, NULL, NULL, NULL},

  {L"MSI Frame ID", 4, 4, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Physical Base Address", 8, 8, L"0x%lx", NULL, NULL, NULL, NULL},
  {L"Flags", 4, 16, L"0x%x", NULL, NULL, NULL, NULL},

  {L"SPI Count", 2, 20, L"%d", NULL, NULL, NULL, NULL},
  {L"SPI Base", 2, 22, L"0x%x", NULL, NULL, NULL, NULL}
};

/**
  An ACPI_PARSER array describing the GICR Interrupt Controller Structure.
**/
STATIC CONST ACPI_PARSER GicRParser[] = {
  {L"Type", 1, 0, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Length", 1, 1, L"%d", NULL, NULL, NULL, NULL},
  {L"Reserved", 2, 2, L"0x%x", NULL, NULL, NULL, NULL},

  {L"Discovery Range Base Address", 8, 4, L"0x%lx", NULL, NULL, NULL,
   NULL},
  {L"Discovery Range Length", 4, 12, L"0x%x", NULL, NULL, NULL, NULL}
};

/**
  An ACPI_PARSER array describing the GIC ITS Interrupt Controller Structure.
**/
STATIC CONST ACPI_PARSER GicITSParser[] = {
  {L"Type", 1, 0, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Length", 1, 1, L"%d", NULL, NULL, NULL, NULL},
  {L"Reserved", 2, 2, L"0x%x", NULL, NULL, NULL, NULL},

  {L"GIC ITS ID", 4, 4, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Physical Base Address", 8, 8, L"0x%lx", NULL, NULL, NULL, NULL},
  {L"Reserved", 4, 16, L"0x%x", NULL, NULL, NULL, NULL}
};

/**
  An ACPI_PARSER array describing the ACPI MADT Table.
**/
STATIC CONST ACPI_PARSER MadtParser[] = {
  PARSE_ACPI_HEADER (&AcpiHdrInfo),
  {L"Local Interrupt Controller Address", 4, 36, L"0x%x", NULL, NULL, NULL,
   NULL},
  {L"Flags", 4, 40, L"0x%x", NULL, NULL, NULL, NULL}
};

/**
  An ACPI_PARSER array describing the MADT Interrupt Controller Structure Header Structure.
**/
STATIC CONST ACPI_PARSER MadtInterruptControllerHeaderParser[] = {
  {NULL, 1, 0, NULL, NULL, (VOID**)&MadtInterruptControllerType, NULL, NULL},
  {L"Length", 1, 1, NULL, NULL, (VOID**)&MadtInterruptControllerLength, NULL,
   NULL},
  {L"Reserved", 2, 2, NULL, NULL, NULL, NULL, NULL}
};

/**
  Information about each Interrupt Controller Structure type.
**/
STATIC ACPI_STRUCT_INFO MadtStructs[] = {
  ACPI_STRUCT_INFO_PARSER_NOT_IMPLEMENTED (
    "Processor Local APIC",
    EFI_ACPI_6_3_PROCESSOR_LOCAL_APIC,
    ARCH_COMPAT_IA32 | ARCH_COMPAT_X64
    ),
  ACPI_STRUCT_INFO_PARSER_NOT_IMPLEMENTED (
    "I/O APIC",
    EFI_ACPI_6_3_IO_APIC,
    ARCH_COMPAT_IA32 | ARCH_COMPAT_X64
    ),
  ACPI_STRUCT_INFO_PARSER_NOT_IMPLEMENTED (
    "Interrupt Source Override",
    EFI_ACPI_6_3_INTERRUPT_SOURCE_OVERRIDE,
    ARCH_COMPAT_IA32 | ARCH_COMPAT_X64
    ),
  ACPI_STRUCT_INFO_PARSER_NOT_IMPLEMENTED (
    "NMI Source",
    EFI_ACPI_6_3_NON_MASKABLE_INTERRUPT_SOURCE,
    ARCH_COMPAT_IA32 | ARCH_COMPAT_X64
    ),
  ACPI_STRUCT_INFO_PARSER_NOT_IMPLEMENTED (
    "Local APIC NMI",
    EFI_ACPI_6_3_LOCAL_APIC_NMI,
    ARCH_COMPAT_IA32 | ARCH_COMPAT_X64
    ),
  ACPI_STRUCT_INFO_PARSER_NOT_IMPLEMENTED (
    "Local APIC Address Override",
    EFI_ACPI_6_3_LOCAL_APIC_ADDRESS_OVERRIDE,
    ARCH_COMPAT_IA32 | ARCH_COMPAT_X64
    ),
  ACPI_STRUCT_INFO_PARSER_NOT_IMPLEMENTED (
    "I/O SAPIC",
    EFI_ACPI_6_3_IO_SAPIC,
    ARCH_COMPAT_IA32 | ARCH_COMPAT_X64
    ),
  ACPI_STRUCT_INFO_PARSER_NOT_IMPLEMENTED (
    "Local SAPIC",
    EFI_ACPI_6_3_LOCAL_SAPIC,
    ARCH_COMPAT_IA32 | ARCH_COMPAT_X64
    ),
  ACPI_STRUCT_INFO_PARSER_NOT_IMPLEMENTED (
    "Platform Interrupt Sources",
    EFI_ACPI_6_3_PLATFORM_INTERRUPT_SOURCES,
    ARCH_COMPAT_IA32 | ARCH_COMPAT_X64
    ),
  ACPI_STRUCT_INFO_PARSER_NOT_IMPLEMENTED (
    "Processor Local x2APIC",
    EFI_ACPI_6_3_PROCESSOR_LOCAL_X2APIC,
    ARCH_COMPAT_IA32 | ARCH_COMPAT_X64
    ),
  ACPI_STRUCT_INFO_PARSER_NOT_IMPLEMENTED (
    "Local x2APIC NMI",
    EFI_ACPI_6_3_LOCAL_X2APIC_NMI,
    ARCH_COMPAT_IA32 | ARCH_COMPAT_X64
    ),
  ADD_ACPI_STRUCT_INFO_ARRAY (
    "GICC",
    EFI_ACPI_6_3_GIC,
    ARCH_COMPAT_ARM | ARCH_COMPAT_AARCH64,
    GicCParser
    ),
  ADD_ACPI_STRUCT_INFO_ARRAY (
    "GICD",
    EFI_ACPI_6_3_GICD,
    ARCH_COMPAT_ARM | ARCH_COMPAT_AARCH64,
    GicDParser
    ),
  ADD_ACPI_STRUCT_INFO_ARRAY (
    "GIC MSI Frame",
    EFI_ACPI_6_3_GIC_MSI_FRAME,
    ARCH_COMPAT_ARM | ARCH_COMPAT_AARCH64,
    GicMSIFrameParser
    ),
  ADD_ACPI_STRUCT_INFO_ARRAY (
    "GICR",
    EFI_ACPI_6_3_GICR,
    ARCH_COMPAT_ARM | ARCH_COMPAT_AARCH64,
    GicRParser
    ),
  ADD_ACPI_STRUCT_INFO_ARRAY (
    "GIC ITS",
    EFI_ACPI_6_3_GIC_ITS,
    ARCH_COMPAT_ARM | ARCH_COMPAT_AARCH64,
    GicITSParser
    )
};

/**
  MADT structure database
**/
STATIC ACPI_STRUCT_DATABASE MadtDatabase = {
  "Interrupt Controller Structure",
  MadtStructs,
  ARRAY_SIZE (MadtStructs)
};

/**
  ACPI Processor UID and GIC ITS ID comparator.

  @param[in] Id1   The first ID.
  @param[in] Id2   The second ID.

  @retval 0     Id1 and Id2 are equal.
  @retval -1    Id1 and Id2 are different.
**/
INTN
EFIAPI
GicIdCompare (
  CONST VOID *Id1,
  CONST VOID *Id2
  )
{
  if (*(UINT32*)Id1 == *(UINT32*)Id2) {
    return 0;
  } else {
    return -1;
  }
}

/**
  Validate that all instances of a given field in a given controller
  type structure are unique across the entire MADT table.

  This method assumes that there has already been a successful pass through
  the table. Consequently, many security checks are skipped.

  @param [in] Ptr             Pointer to the MADT table
  @param [in] Length          Length of the MADT table in bytes.
  @param [in] StructOffset    Offset from the start of MADT to the list of
                              Interrupt Controller Structures.
  @param [in] FieldOffset     Offset of field in MADT Interrupt Controller struct
                              that is to be checked for uniqueness.
  @param [in] FieldSize       Size of field pointed to by FieldOffset.
  @param [in] StructMeta      Structure with metadata (type, name) concerning
                              the struct that contains the field pointed to by
                              FieldOffset.

  @retval EFI_SUCCESS             All instances of the field are unique
  @retval EFI_INVALID_PARAMETER   One or more duplicate values found.
  @retval EFI_OUT_OF_RESOURCES    Memory allocation failed.
**/
STATIC
EFI_STATUS
EFIAPI
ValidateFieldUnique (
  IN UINT8              *Ptr,
  IN UINT32             Length,
  IN UINT32             StructOffset,
  IN UINTN              FieldOffset,
  IN UINTN              FieldSize,
  IN CONST EFI_STRING   FieldName,
  IN ACPI_STRUCT_INFO   *StructMeta
  )
{
  BOOLEAN               AllUnique;
  LIST_ENTRY            UniqueList;

  InitializeListHead(&UniqueList);

  // Parse the MADT table body in search of GICC structures
  while (StructOffset < Length) {

    // Parse Interrupt Controller Structure to obtain its Length.
    ParseAcpi (
      FALSE,
      0,
      NULL,
      Ptr + StructOffset,
      Length - StructOffset,
      PARSER_PARAMS (MadtInterruptControllerHeaderParser)
      );

    // If the currently parsed structure is of correct type, and is big enough
    // to expose the required field, add the field value to the
    // unique list buffer.
    if ((*MadtInterruptControllerType == StructMeta->Type) &&
        (*MadtInterruptControllerLength > (FieldOffset + FieldSize))
        ) {
      AcpiCrossValidatorAdd (
        &UniqueList,
        Ptr + StructOffset + FieldOffset,
        FieldSize,
        StructMeta->Type,
        StructOffset + FieldOffset
        );
    }

    StructOffset += *MadtInterruptControllerLength;
  } // while

  AllUnique = AcpiCrossValidatorAllUnique (
    &UniqueList,
    GicIdCompare,
    StructMeta->Name,
    FieldName
    );

  AcpiCrossValidatorDelete (&UniqueList);
  return AllUnique ? EFI_SUCCESS : EFI_INVALID_PARAMETER;
}

/**
  This function parses the ACPI MADT table.
  When trace is enabled this function parses the MADT table and
  traces the ACPI table fields.

  This function currently parses the following Interrupt Controller
  Structures:
    - GICC
    - GICD
    - GIC MSI Frame
    - GICR
    - GIC ITS

  This function also performs validation of the ACPI table fields.

  @param [in] Trace              If TRUE, trace the ACPI fields.
  @param [in] Ptr                Pointer to the start of the buffer.
  @param [in] AcpiTableLength    Length of the ACPI table.
  @param [in] AcpiTableRevision  Revision of the ACPI table.
**/
VOID
EFIAPI
ParseAcpiMadt (
  IN BOOLEAN Trace,
  IN UINT8*  Ptr,
  IN UINT32  AcpiTableLength,
  IN UINT8   AcpiTableRevision
  )
{
  UINT32 Offset;
  UINT32 MadtBodyOffset;

  if (!Trace) {
    return;
  }

  ResetAcpiStructCounts (&MadtDatabase);

  Offset = ParseAcpi (
             TRUE,
             0,
             "MADT",
             Ptr,
             AcpiTableLength,
             PARSER_PARAMS (MadtParser)
             );

  MadtBodyOffset = Offset;

  while (Offset < AcpiTableLength) {
    // Parse Interrupt Controller Structure to obtain Length.
    ParseAcpi (
      FALSE,
      0,
      NULL,
      Ptr + Offset,
      AcpiTableLength - Offset,
      PARSER_PARAMS (MadtInterruptControllerHeaderParser)
      );

    // Check if the values used to control the parsing logic have been
    // successfully read.
    if ((MadtInterruptControllerType == NULL) ||
        (MadtInterruptControllerLength == NULL)) {
      AcpiError (
        ACPI_ERROR_PARSE,
        L"Failed to read the Interrupt Controller Structure header");
      return;
    }

    if (AssertMemberIntegrity (
          Offset, *MadtInterruptControllerLength, Ptr, AcpiTableLength)) {
      return;
    }

    // Parse the Interrupt Controller Structure
    ParseAcpiStruct (
      2,
      Ptr + Offset,
      &MadtDatabase,
      Offset,
      *MadtInterruptControllerType,
      *MadtInterruptControllerLength);

    Offset += *MadtInterruptControllerLength;
  } // while

  // Report and validate Interrupt Controller Structure counts
  if (mConfig.ConsistencyCheck) {
    ValidateAcpiStructCounts (&MadtDatabase);

    ValidateFieldUnique (
      Ptr,
      AcpiTableLength,
      MadtBodyOffset,
      FIELD_SIZE_OF (EFI_ACPI_6_3_GIC_STRUCTURE, AcpiProcessorUid),
      OFFSET_OF (EFI_ACPI_6_3_GIC_STRUCTURE, AcpiProcessorUid),
      L"ACPI Processor UID",
      &MadtStructs[EFI_ACPI_6_3_GIC]
    );

    ValidateFieldUnique (
      Ptr,
      AcpiTableLength,
      MadtBodyOffset,
      FIELD_SIZE_OF (EFI_ACPI_6_3_GIC_ITS_STRUCTURE, GicItsId),
      OFFSET_OF (EFI_ACPI_6_3_GIC_ITS_STRUCTURE, GicItsId),
      L"GIC ITS ID",
      &MadtStructs[EFI_ACPI_6_3_GIC_ITS]
    );

    ValidateFieldUnique (
      Ptr,
      AcpiTableLength,
      MadtBodyOffset,
      FIELD_SIZE_OF (EFI_ACPI_6_3_GIC_MSI_FRAME_STRUCTURE, GicMsiFrameId),
      OFFSET_OF (EFI_ACPI_6_3_GIC_MSI_FRAME_STRUCTURE, GicMsiFrameId),
      L"GIC MSI Frame ID",
      &MadtStructs[EFI_ACPI_6_3_GIC_MSI_FRAME]
    );

    if (MadtStructs[EFI_ACPI_6_3_GICD].Count > 1) {
      AcpiError (
        ACPI_ERROR_CROSS,
        L"Only one %a must be present",
        MadtStructs[EFI_ACPI_6_3_GICD].Name
        );
    }
  }
}
