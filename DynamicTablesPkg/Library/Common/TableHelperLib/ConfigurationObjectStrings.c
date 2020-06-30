/** @file
  ConfigurationObjectStrings.c

  Copyright (c) 2020, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <ConfigurationManagerObject.h>

const CHAR16 *ArmObjectString[] = {
  L"Reserved",
  L"Boot Architecture Info",
  L"CPU Info",
  L"Power Management Profile Info",
  L"GIC CPU Interface Info",
  L"GIC Distributor Info",
  L"GIC MSI Frame Info",
  L"GIC Redistributor Info",
  L"GIC ITS Info",
  L"Serial Console Port Info",
  L"Serial Debug Port Info",
  L"Generic Timer Info",
  L"Platform GT Block Info",
  L"Generic Timer Block Frame Info",
  L"Platform Generic Watchdog",
  L"PCI Configuration Space Info",
  L"Hypervisor Vendor Id",
  L"Fixed feature flags for FADT",
  L"ITS Group",
  L"Named Component",
  L"Root Complex",
  L"SMMUv1 or SMMUv2",
  L"SMMUv3",
  L"PMCG",
  L"GIC ITS Identifier Array",
  L"ID Mapping Array",
  L"SMMU Interrupt Array",
  L"Processor Hierarchy Info",
  L"Cache Info",
  L"Processor Node ID Info",
  L"CM Object Reference",
  L"Memory Affinity Info",
  L"Device Handle Acpi",
  L"Device Handle Pci",
  L"Generic Initiator Affinity"
};

const CHAR16 *ObjectNameSpaceString[] = {
  L"Standard Objects Namespace",
  L"ARM Objects Namespace",
  L"OEM Objects Namespace"
};

const CHAR16 *StdObjectString[] = {
  L"Configuration Manager Info",
  L"ACPI table Info List",
  L"SMBIOS table Info List"
};

const CHAR16* UnknownObject = L"Unknown Object";

/**
  Returns the user friendly name for the given ObjectId.

  @param[in]  CmObjectId   The id of the configuration manager object
  @return                  User friendly name for object id.
**/
const CHAR16*
EFIAPI
CmObjectIdName(
  IN CONST CM_OBJECT_ID            CmObjectId
  )
{
  switch (GET_CM_NAMESPACE_ID(CmObjectId)) {
    case EObjNameSpaceStandard:
      if (CmObjectId < EStdObjMax) {
        return StdObjectString[CmObjectId - EObjNameSpaceStandard];
      } else {
        return UnknownObject;
      }
    case EObjNameSpaceArm:
      if (CmObjectId < EArmObjMax) {
        return ArmObjectString[CmObjectId - EObjNameSpaceArm];
      } else {
        return UnknownObject;
      }
    default:
      return UnknownObject;
  }

  return UnknownObject;
}
