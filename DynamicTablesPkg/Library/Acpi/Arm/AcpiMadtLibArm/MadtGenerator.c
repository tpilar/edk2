/** @file
  MADT Table Generator

  Copyright (c) 2017 - 2019, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent

  @par Reference(s):
  - ACPI 6.3 Specification - January 2019

**/

#include <Library/AcpiLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Protocol/AcpiTable.h>

// Module specific include files.
#include <AcpiTableGenerator.h>
#include <ConfigurationManagerObject.h>
#include <Library/TableHelperLib.h>
#include <Protocol/ConfigurationManagerProtocol.h>

/** ARM standard MADT Generator

Requirements:
  The following Configuration Manager Object(s) are required by
  this Generator:
  - EArmObjGicCInfo
  - EArmObjGicDInfo
  - EArmObjGicMsiFrameInfo (OPTIONAL)
  - EArmObjGicRedistributorInfo (OPTIONAL)
  - EArmObjGicItsInfo (OPTIONAL)
*/

/** This function updates the GIC CPU Interface Information in the
    EFI_ACPI_6_3_GIC_STRUCTURE structure.

  @param [in]  Gicc       Pointer to GIC CPU Interface structure.
  @param [in]  GicCInfo   Pointer to the GIC CPU Interface Information.
  @param [in]  MadtRev    MADT table revision.
**/
STATIC
VOID
AddGICC (
  IN        EFI_ACPI_6_3_GIC_STRUCTURE  * CONST Gicc,
  IN  CONST CM_ARM_GICC_INFO            * CONST GicCInfo,
  IN  CONST UINT8                               MadtRev
  )
{
  ASSERT (Gicc != NULL);
  ASSERT (GicCInfo != NULL);

  // UINT8 Type
  Gicc->Type = EFI_ACPI_6_3_GIC;
  // UINT8 Length
  Gicc->Length = sizeof (EFI_ACPI_6_3_GIC_STRUCTURE);
  // UINT16 Reserved
  Gicc->Reserved = EFI_ACPI_RESERVED_WORD;

  // UINT32 CPUInterfaceNumber
  Gicc->CPUInterfaceNumber = GicCInfo->CPUInterfaceNumber;
  // UINT32 AcpiProcessorUid
  Gicc->AcpiProcessorUid = GicCInfo->AcpiProcessorUid;
  // UINT32 Flags
  Gicc->Flags = GicCInfo->Flags;
  // UINT32 ParkingProtocolVersion
  Gicc->ParkingProtocolVersion = GicCInfo->ParkingProtocolVersion;
  // UINT32 PerformanceInterruptGsiv
  Gicc->PerformanceInterruptGsiv = GicCInfo->PerformanceInterruptGsiv;
  // UINT64 ParkedAddress
  Gicc->ParkedAddress = GicCInfo->ParkedAddress;

  // UINT64 PhysicalBaseAddress
  Gicc->PhysicalBaseAddress = GicCInfo->PhysicalBaseAddress;
  // UINT64 GICV
  Gicc->GICV = GicCInfo->GICV;
  // UINT64 GICH
  Gicc->GICH = GicCInfo->GICH;

  // UINT32 VGICMaintenanceInterrupt
  Gicc->VGICMaintenanceInterrupt = GicCInfo->VGICMaintenanceInterrupt;
  // UINT64 GICRBaseAddress
  Gicc->GICRBaseAddress = GicCInfo->GICRBaseAddress;

  // UINT64 MPIDR
  Gicc->MPIDR = GicCInfo->MPIDR;
  // UINT8 ProcessorPowerEfficiencyClass
  Gicc->ProcessorPowerEfficiencyClass =
    GicCInfo->ProcessorPowerEfficiencyClass;
  // UINT8 Reserved2
  Gicc->Reserved2 = EFI_ACPI_RESERVED_BYTE;

  // UINT16  SpeOverflowInterrupt
  if (MadtRev > EFI_ACPI_6_2_MULTIPLE_APIC_DESCRIPTION_TABLE_REVISION) {
    Gicc->SpeOverflowInterrupt = GicCInfo->SpeOverflowInterrupt;
  } else {
    // Setting SpeOverflowInterrupt to 0 ensures backward compatibility with
    // ACPI 6.2 by also clearing the Reserved2[1] and Reserved2[2] fields
    // in EFI_ACPI_6_2_GIC_STRUCTURE.
    Gicc->SpeOverflowInterrupt = 0;
  }
}

/**
  Function to test if two GIC CPU Interface information structures have the
  same ACPI Processor UID.

  @param [in]  GicCInfo1          Pointer to the first GICC info structure.
  @param [in]  GicCInfo2          Pointer to the second GICC info structure.
  @param [in]  Index1             Index of GicCInfo1 in the shared list of GIC
                                  CPU Interface Info structures.
  @param [in]  Index2             Index of GicCInfo2 in the shared list of GIC
                                  CPU Interface Info structures.

  @retval TRUE                    GicCInfo1 and GicCInfo2 have the same UID.
  @retval FALSE                   GicCInfo1 and GicCInfo2 have different UIDs.
**/
BOOLEAN
EFIAPI
IsAcpiUidEqual (
  IN  CONST VOID          * GicCInfo1,
  IN  CONST VOID          * GicCInfo2,
  IN        UINTN           Index1,
  IN        UINTN           Index2
  )
{
  UINT32      Uid1;
  UINT32      Uid2;

  ASSERT ((GicCInfo1 != NULL) && (GicCInfo2 != NULL));

  Uid1 = ((CM_ARM_GICC_INFO*)GicCInfo1)->AcpiProcessorUid;
  Uid2 = ((CM_ARM_GICC_INFO*)GicCInfo2)->AcpiProcessorUid;

  if (Uid1 == Uid2) {
    DEBUG ((
      DEBUG_ERROR,
      "ERROR: MADT: GICC Info Structures %d and %d have the same ACPI " \
      "Processor UID: 0x%x.\n",
      Index1,
      Index2,
      Uid1
      ));
    return TRUE;
  }

  return FALSE;
}

/** Add the GIC CPU Interface Information to the MADT Table.

  This function also checks for duplicate ACPI Processor UIDs.

  @param [in]  Gicc                 Pointer to GIC CPU Interface structure list.
  @param [in]  GicCInfo             Pointer to the GIC CPU Information list.
  @param [in]  GicCCount            Count of GIC CPU Interfaces.
  @param [in]  MadtRev              MADT table revision.

  @retval EFI_SUCCESS               GIC CPU Interface Information was added
                                    successfully.
  @retval EFI_INVALID_PARAMETER     One or more invalid GIC CPU Info values were
                                    provided and the generator failed to add the
                                    information to the table.
**/
STATIC
EFI_STATUS
AddGICCList (
  IN  EFI_ACPI_6_3_GIC_STRUCTURE  * Gicc,
  IN  CONST UINT8                   MadtRev
  )
{
  BOOLEAN   IsAcpiProcUidDuplicated;
  CM_ARM_GICC_INFO *Cursor;
  VOID             *GicCInfo;
  UINT32 GicCCount;

  ASSERT (Gicc != NULL);

  EFI_STATUS Status =
    CfgMgrGetObjects (EArmObjGicCInfo, CM_NULL_TOKEN, &GicCInfo, &GicCCount);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  IsAcpiProcUidDuplicated = FindDuplicateValue (
                              GicCInfo,
                              GicCCount,
                              sizeof (CM_ARM_GICC_INFO),
                              IsAcpiUidEqual
                              );
  // Duplicate ACPI Processor UID was found so the GICC info provided
  // is invalid
  if (IsAcpiProcUidDuplicated) {
    FreePool (GicCInfo);
    return EFI_INVALID_PARAMETER;
  }

  Cursor = GicCInfo;
  while (GicCCount-- != 0) {
    AddGICC (Gicc++, Cursor++, MadtRev);
  }

  FreePool (GicCInfo);
  return EFI_SUCCESS;
}

/** Update the GIC Distributor Information in the MADT Table.

  @param [in]  Gicd      Pointer to GIC Distributor structure.
**/
STATIC
VOID
AddGICD (
  EFI_ACPI_6_3_GIC_DISTRIBUTOR_STRUCTURE  * CONST Gicd
  )
{
  EFI_STATUS Status;
  CM_ARM_GICD_INFO *GicDInfo;

  ASSERT (Gicd != NULL);
  ASSERT (GicDInfo != NULL);

  Status = CfgMgrGetSimpleObject (EArmObjGicDInfo, (VOID **)&GicDInfo);
  if (EFI_ERROR (Status)) {
    return;
  }

  // UINT8 Type
  Gicd->Type = EFI_ACPI_6_3_GICD;
  // UINT8 Length
  Gicd->Length = sizeof (EFI_ACPI_6_3_GIC_DISTRIBUTOR_STRUCTURE);
  // UINT16 Reserved
  Gicd->Reserved1 = EFI_ACPI_RESERVED_WORD;
  // UINT32 Identifier
  // One, and only one, GIC distributor structure must be present
  // in the MADT for an ARM based system
  Gicd->GicId = 0;
  // UINT64 PhysicalBaseAddress
  Gicd->PhysicalBaseAddress = GicDInfo->PhysicalBaseAddress;
  // UINT32 VectorBase
  Gicd->SystemVectorBase = EFI_ACPI_RESERVED_DWORD;
  // UINT8  GicVersion
  Gicd->GicVersion = GicDInfo->GicVersion;
  // UINT8  Reserved2[3]
  Gicd->Reserved2[0] = EFI_ACPI_RESERVED_BYTE;
  Gicd->Reserved2[1] = EFI_ACPI_RESERVED_BYTE;
  Gicd->Reserved2[2] = EFI_ACPI_RESERVED_BYTE;

  FreePool (GicDInfo);
}

/** Update the GIC MSI Frame Information.

  @param [in]  GicMsiFrame      Pointer to GIC MSI Frame structure.
  @param [in]  GicMsiFrameInfo  Pointer to the GIC MSI Frame Information.
**/
STATIC
VOID
AddGICMsiFrame (
  IN  EFI_ACPI_6_3_GIC_MSI_FRAME_STRUCTURE  * CONST GicMsiFrame,
  IN  CONST CM_ARM_GIC_MSI_FRAME_INFO       * CONST GicMsiFrameInfo
)
{
  ASSERT (GicMsiFrame != NULL);
  ASSERT (GicMsiFrameInfo != NULL);

  GicMsiFrame->Type = EFI_ACPI_6_3_GIC_MSI_FRAME;
  GicMsiFrame->Length = sizeof (EFI_ACPI_6_3_GIC_MSI_FRAME_STRUCTURE);
  GicMsiFrame->Reserved1 = EFI_ACPI_RESERVED_WORD;
  GicMsiFrame->GicMsiFrameId = GicMsiFrameInfo->GicMsiFrameId;
  GicMsiFrame->PhysicalBaseAddress = GicMsiFrameInfo->PhysicalBaseAddress;

  GicMsiFrame->Flags = GicMsiFrameInfo->Flags;
  GicMsiFrame->SPICount = GicMsiFrameInfo->SPICount;
  GicMsiFrame->SPIBase = GicMsiFrameInfo->SPIBase;
}

/** Add the GIC MSI Frame Information to the MADT Table.

  @param [in]  GicMsiFrame      Pointer to GIC MSI Frame structure list.
**/
STATIC
VOID
AddGICMsiFrameInfoList (
  IN  EFI_ACPI_6_3_GIC_MSI_FRAME_STRUCTURE  * GicMsiFrame
  )
{
  EFI_STATUS Status;
  VOID                      *GicMsiInfo;
  CM_ARM_GIC_MSI_FRAME_INFO *Cursor;
  UINT32 GicMsiCount;

  ASSERT (GicMsiFrame != NULL);

  Status = CfgMgrGetObjects (
    EArmObjGicMsiFrameInfo, CM_NULL_TOKEN, &GicMsiInfo, &GicMsiCount);
  if (EFI_ERROR(Status)) {
    return;
  }

  Cursor = GicMsiInfo;
  while (GicMsiCount-- != 0) {
    AddGICMsiFrame (GicMsiFrame++, Cursor++);
  }

  FreePool (GicMsiInfo);
}

/** Update the GIC Redistributor Information.

  @param [in]  Gicr                 Pointer to GIC Redistributor structure.
  @param [in]  GicRedisributorInfo  Pointer to the GIC Redistributor Info.
**/
STATIC
VOID
AddGICRedistributor (
  IN  EFI_ACPI_6_3_GICR_STRUCTURE   * CONST Gicr,
  IN  CONST CM_ARM_GIC_REDIST_INFO  * CONST GicRedisributorInfo
  )
{
  ASSERT (Gicr != NULL);
  ASSERT (GicRedisributorInfo != NULL);

  Gicr->Type = EFI_ACPI_6_3_GICR;
  Gicr->Length = sizeof (EFI_ACPI_6_3_GICR_STRUCTURE);
  Gicr->Reserved = EFI_ACPI_RESERVED_WORD;
  Gicr->DiscoveryRangeBaseAddress =
    GicRedisributorInfo->DiscoveryRangeBaseAddress;
  Gicr->DiscoveryRangeLength = GicRedisributorInfo->DiscoveryRangeLength;
}

/** Add the GIC Redistributor Information to the MADT Table.

  @param [in]  Gicr      Pointer to GIC Redistributor structure list.
**/
STATIC
VOID
AddGICRedistributorList (
  IN  EFI_ACPI_6_3_GICR_STRUCTURE   * Gicr
)
{
  CM_ARM_GIC_REDIST_INFO *Cursor;
  VOID                   *GicRInfo;
  UINT32 GicRCount;
  EFI_STATUS Status;

  ASSERT (Gicr != NULL);

  Status = CfgMgrGetObjects (
    EArmObjGicRedistributorInfo, CM_NULL_TOKEN, &GicRInfo, &GicRCount);
  if (EFI_ERROR (Status)) {
    return;
  }

  Cursor = GicRInfo;
  while (GicRCount-- != 0) {
    AddGICRedistributor (Gicr++, Cursor++);
  }

  FreePool (GicRInfo);
}

/** Update the GIC Interrupt Translation Service Information

  @param [in]  GicIts      Pointer to GIC ITS structure.
  @param [in]  GicItsInfo  Pointer to the GIC ITS Information.
**/
STATIC
VOID
AddGICInterruptTranslationService (
  IN  EFI_ACPI_6_3_GIC_ITS_STRUCTURE  * CONST GicIts,
  IN  CONST CM_ARM_GIC_ITS_INFO       * CONST GicItsInfo
)
{
  ASSERT (GicIts != NULL);
  ASSERT (GicItsInfo != NULL);

  GicIts->Type = EFI_ACPI_6_3_GIC_ITS;
  GicIts->Length = sizeof (EFI_ACPI_6_3_GIC_ITS_STRUCTURE);
  GicIts->Reserved = EFI_ACPI_RESERVED_WORD;
  GicIts->GicItsId = GicItsInfo->GicItsId;
  GicIts->PhysicalBaseAddress = GicItsInfo->PhysicalBaseAddress;
  GicIts->Reserved2 = EFI_ACPI_RESERVED_DWORD;
}

/** Add the GIC Interrupt Translation Service Information
    to the MADT Table.

  @param [in]  GicIts       Pointer to GIC ITS structure list.
**/
STATIC
VOID
AddGICItsList (
  IN  EFI_ACPI_6_3_GIC_ITS_STRUCTURE  * GicIts
)
{
  CM_ARM_GIC_ITS_INFO *Cursor;
  VOID                *GicItsInfo;
  UINT32 GicItsCount;
  EFI_STATUS Status;

  ASSERT (GicIts != NULL);

  Status = CfgMgrGetObjects (
    EArmObjGicItsInfo, CM_NULL_TOKEN, &GicItsInfo, &GicItsCount);
  if (EFI_ERROR(Status)) {
    return;
  }

  Cursor = GicItsInfo;
  while (GicItsCount-- != 0) {
    AddGICInterruptTranslationService (GicIts++, Cursor++);
  }

  FreePool (GicItsInfo);
}

/** Construct the MADT ACPI table.

  This function invokes the Configuration Manager protocol interface
  to get the required hardware information for generating the ACPI
  table.

  If this function allocates any resources then they must be freed
  in the FreeXXXXTableResources function.

  @param [in]  This           Pointer to the table generator.
  @param [in]  AcpiTableInfo  Pointer to the ACPI Table Info.
  @param [in]  CfgMgrProtocol Pointer to the Configuration Manager
                              Protocol Interface.
  @param [out] Table          Pointer to the constructed ACPI Table.

  @retval EFI_SUCCESS           Table generated successfully.
  @retval EFI_INVALID_PARAMETER A parameter is invalid.
  @retval EFI_NOT_FOUND         The required object was not found.
  @retval EFI_BAD_BUFFER_SIZE   The size returned by the Configuration
                                Manager is less than the Object size for the
                                requested object.
**/
STATIC
EFI_STATUS
EFIAPI
BuildMadtTable (
  IN  CONST ACPI_TABLE_GENERATOR                  * CONST This,
  IN  CONST CM_STD_OBJ_ACPI_TABLE_INFO            * CONST AcpiTableInfo,
  IN  CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST CfgMgrProtocol,
  OUT       EFI_ACPI_DESCRIPTION_HEADER          ** CONST Table
  )
{
  EFI_STATUS                   Status;
  UINT32                       TableSize;

  UINT32                       GicCCount;
  UINT32                       GicDCount;
  UINT32                       GicMSICount;
  UINT32                       GicRedistCount;
  UINT32                       GicItsCount;

  UINT32                       GicCOffset;
  UINT32                       GicDOffset;
  UINT32                       GicMSIOffset;
  UINT32                       GicRedistOffset;
  UINT32                       GicItsOffset;

  EFI_ACPI_6_3_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER  * Madt;

  ASSERT (This != NULL);
  ASSERT (AcpiTableInfo != NULL);
  ASSERT (Table != NULL);
  ASSERT (AcpiTableInfo->TableGeneratorId == This->GeneratorID);
  ASSERT (AcpiTableInfo->AcpiTableSignature == This->AcpiTableSignature);

  if ((AcpiTableInfo->AcpiTableRevision < This->MinAcpiTableRevision) ||
      (AcpiTableInfo->AcpiTableRevision > This->AcpiTableRevision)) {
    DEBUG ((
      DEBUG_ERROR,
      "ERROR: MADT: Requested table revision = %d, is not supported."
      "Supported table revision: Minimum = %d, Maximum = %d\n",
      AcpiTableInfo->AcpiTableRevision,
      This->MinAcpiTableRevision,
      This->AcpiTableRevision
      ));
    return EFI_INVALID_PARAMETER;
  }

  // Allocated memory pointers
  *Table = NULL;

  Status = CfgMgrCountObjects (EArmObjGicCInfo, &GicCCount);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  if (GicCCount == 0) {
    DEBUG ((
      DEBUG_ERROR,
      "ERROR: MADT: GIC CPU Interface information not provided.\n"
      ));
    ASSERT (GicCCount != 0);
    return EFI_INVALID_PARAMETER;
  }

  Status = CfgMgrCountObjects (EArmObjGicDInfo, &GicDCount);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (GicDCount == 0) {
    DEBUG ((
      DEBUG_ERROR,
      "ERROR: MADT: GIC Distributor information not provided.\n"
      ));
    ASSERT (GicDCount != 0);
    return EFI_INVALID_PARAMETER;
  }

  if (GicDCount > 1) {
    DEBUG ((
      DEBUG_ERROR,
      "ERROR: MADT: One, and only one, GIC distributor must be present."
      "GicDCount = %d\n",
      GicDCount
      ));
    ASSERT (GicDCount <= 1);
    return EFI_INVALID_PARAMETER;
  }

  Status = CfgMgrCountObjects (EArmObjGicMsiFrameInfo, &GicMSICount);
  if (EFI_ERROR (Status) && (Status != EFI_NOT_FOUND)) {
    return Status;
  }

  Status = CfgMgrCountObjects (EArmObjGicRedistributorInfo, &GicRedistCount);
  if (EFI_ERROR (Status) && (Status != EFI_NOT_FOUND)) {
    return Status;
  }

  Status = CfgMgrCountObjects (EArmObjGicItsInfo, &GicItsCount);
  if (EFI_ERROR (Status) && (Status != EFI_NOT_FOUND)) {
    return Status;
  }

  TableSize = sizeof (EFI_ACPI_6_3_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER);

  GicCOffset = TableSize;
  TableSize += (sizeof (EFI_ACPI_6_3_GIC_STRUCTURE) * GicCCount);

  GicDOffset = TableSize;
  TableSize += (sizeof (EFI_ACPI_6_3_GIC_DISTRIBUTOR_STRUCTURE) * GicDCount);

  GicMSIOffset = TableSize;
  TableSize += (sizeof (EFI_ACPI_6_3_GIC_MSI_FRAME_STRUCTURE) * GicMSICount);

  GicRedistOffset = TableSize;
  TableSize += (sizeof (EFI_ACPI_6_3_GICR_STRUCTURE) * GicRedistCount);

  GicItsOffset = TableSize;
  TableSize += (sizeof (EFI_ACPI_6_3_GIC_ITS_STRUCTURE) * GicItsCount);

  // Allocate the Buffer for MADT table
  Madt = AllocateZeroPool (TableSize);
  if (Madt == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  DEBUG ((
    DEBUG_INFO,
    "MADT: Madt = 0x%p TableSize = 0x%x\n",
    Madt,
    TableSize
    ));

  Status = AddAcpiHeader (This, &Madt->Header, AcpiTableInfo, TableSize);
  if (EFI_ERROR (Status)) {
    goto error_handler;
  }

  Status = AddGICCList (
    (EFI_ACPI_6_3_GIC_STRUCTURE *)((UINT8 *)Madt + GicCOffset),
    Madt->Header.Revision);

  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "ERROR: MADT: Failed to add GICC structures. Status = %r\n",
      Status
      ));
    goto error_handler;
  }

  AddGICD ((VOID *)((UINT8 *)Madt + GicDOffset));

  if (GicMSICount != 0) {
    AddGICMsiFrameInfoList ((VOID *)((UINT8 *)Madt + GicMSIOffset));
  }

  if (GicRedistCount != 0) {
    AddGICRedistributorList ((VOID *)((UINT8 *)Madt + GicRedistOffset));
  }

  if (GicItsCount != 0) {
    AddGICItsList ((VOID *)((UINT8 *)Madt + GicItsOffset));
  }

  *Table = (EFI_ACPI_DESCRIPTION_HEADER*)Madt;
  return EFI_SUCCESS;

error_handler:
  FreePool (Madt);
  return Status;
}

/** Free any resources allocated for constructing the MADT

  @param [in]      This           Pointer to the table generator.
  @param [in]      AcpiTableInfo  Pointer to the ACPI Table Info.
  @param [in]      CfgMgrProtocol Pointer to the Configuration Manager
                                  Protocol Interface.
  @param [in, out] Table          Pointer to the ACPI Table.

  @retval EFI_SUCCESS           The resources were freed successfully.
  @retval EFI_INVALID_PARAMETER The table pointer is NULL or invalid.
**/
STATIC
EFI_STATUS
FreeMadtTableResources (
  IN      CONST ACPI_TABLE_GENERATOR                  * CONST This,
  IN      CONST CM_STD_OBJ_ACPI_TABLE_INFO            * CONST AcpiTableInfo,
  IN      CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST CfgMgrProtocol,
  IN OUT        EFI_ACPI_DESCRIPTION_HEADER          ** CONST Table
  )
{
  ASSERT (This != NULL);
  ASSERT (AcpiTableInfo != NULL);
  ASSERT (AcpiTableInfo->TableGeneratorId == This->GeneratorID);
  ASSERT (AcpiTableInfo->AcpiTableSignature == This->AcpiTableSignature);

  if ((Table == NULL) || (*Table == NULL)) {
    DEBUG ((DEBUG_ERROR, "ERROR: MADT: Invalid Table Pointer\n"));
    ASSERT ((Table != NULL) && (*Table != NULL));
    return EFI_INVALID_PARAMETER;
  }

  FreePool (*Table);
  *Table = NULL;
  return EFI_SUCCESS;
}

/** The MADT Table Generator revision.
*/
#define MADT_GENERATOR_REVISION CREATE_REVISION (1, 0)

/** The interface for the MADT Table Generator.
*/
STATIC
CONST
ACPI_TABLE_GENERATOR MadtGenerator = {
  // Generator ID
  CREATE_STD_ACPI_TABLE_GEN_ID (EStdAcpiTableIdMadt),
  // Generator Description
  L"ACPI.STD.MADT.GENERATOR",
  // ACPI Table Signature
  EFI_ACPI_6_3_MULTIPLE_APIC_DESCRIPTION_TABLE_SIGNATURE,
  // ACPI Table Revision supported by this Generator
  EFI_ACPI_6_3_MULTIPLE_APIC_DESCRIPTION_TABLE_REVISION,
  // Minimum supported ACPI Table Revision
  EFI_ACPI_6_2_MULTIPLE_APIC_DESCRIPTION_TABLE_REVISION,
  // Creator ID
  TABLE_GENERATOR_CREATOR_ID_ARM,
  // Creator Revision
  MADT_GENERATOR_REVISION,
  // Build Table function
  BuildMadtTable,
  // Free Resource function
  FreeMadtTableResources,
  // Extended build function not needed
  NULL,
  // Extended build function not implemented by the generator.
  // Hence extended free resource function is not required.
  NULL
};

/** Register the Generator with the ACPI Table Factory.

  @param [in]  ImageHandle  The handle to the image.
  @param [in]  SystemTable  Pointer to the System Table.

  @retval EFI_SUCCESS           The Generator is registered.
  @retval EFI_INVALID_PARAMETER A parameter is invalid.
  @retval EFI_ALREADY_STARTED   The Generator for the Table ID
                                is already registered.
**/
EFI_STATUS
EFIAPI
AcpiMadtLibConstructor (
  IN  EFI_HANDLE           ImageHandle,
  IN  EFI_SYSTEM_TABLE  *  SystemTable
  )
{
  EFI_STATUS  Status;
  Status = RegisterAcpiTableGenerator (&MadtGenerator);
  DEBUG ((DEBUG_INFO, "MADT: Register Generator. Status = %r\n", Status));
  ASSERT_EFI_ERROR (Status);
  return Status;
}

/** Deregister the Generator from the ACPI Table Factory.

  @param [in]  ImageHandle  The handle to the image.
  @param [in]  SystemTable  Pointer to the System Table.

  @retval EFI_SUCCESS           The Generator is deregistered.
  @retval EFI_INVALID_PARAMETER A parameter is invalid.
  @retval EFI_NOT_FOUND         The Generator is not registered.
**/
EFI_STATUS
EFIAPI
AcpiMadtLibDestructor (
  IN  EFI_HANDLE           ImageHandle,
  IN  EFI_SYSTEM_TABLE  *  SystemTable
  )
{
  EFI_STATUS  Status;
  Status = DeregisterAcpiTableGenerator (&MadtGenerator);
  DEBUG ((DEBUG_INFO, "MADT: Deregister Generator. Status = %r\n", Status));
  ASSERT_EFI_ERROR (Status);
  return Status;
}
