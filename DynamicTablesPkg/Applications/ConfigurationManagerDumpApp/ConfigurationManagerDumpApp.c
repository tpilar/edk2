#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/ConfigurationManagerProtocol.h>

#include "ConfigurationManagerDumpApp.h"

EDKII_CONFIGURATION_MANAGER_PROTOCOL *mCfgMgr;

EFI_STATUS
EFIAPI
UefiMain(
  IN  EFI_HANDLE            ImageHandle,
  IN  EFI_SYSTEM_TABLE   *  SystemTable
  )
{
  EFI_STATUS Status = gBS->LocateProtocol (
    &gEdkiiConfigurationManagerProtocolGuid, NULL, (VOID **)&mCfgMgr);

  UINTN ObjectId;
  CM_OBJ_DESCRIPTOR CmObject;
  UINTN Count = 0;


  if (EFI_ERROR(Status)) {
    Print(L"No Configuration Manager installed!\n");
    return EFI_UNSUPPORTED;
  }

  for (ObjectId = EObjNameSpaceStandard; ObjectId < EStdObjMax; ObjectId++) {
    Status = mCfgMgr->GetObject (mCfgMgr, ObjectId, CM_NULL_TOKEN, &CmObject);
    if (EFI_ERROR(Status)) {
      continue;
    }

    Print (
      L"<%s>::<%s>\n",
      ObjectNameSpaceString[EObjNameSpaceStandard],
      StdObjectString[ObjectId - EObjNameSpaceStandard]);

    Print (
      L"Id=%x Size=0x%x at=%p count=%d\n",
      CmObject.ObjectId,
      CmObject.Size,
      CmObject.Count,
      CmObject.Count);

    Count++;
  }

  for (ObjectId = EObjNameSpaceArm; ObjectId < EArmObjMax; ObjectId++) {
    Status = mCfgMgr->GetObject (mCfgMgr, ObjectId, CM_NULL_TOKEN, &CmObject);
    if (EFI_ERROR(Status)) {
      continue;
    }

    Print (
      L"<%s>::<%s>\n",
      ObjectNameSpaceString[EObjNameSpaceArm],
      ArmObjectString[ObjectId - EObjNameSpaceArm]);

    Print (
      L"Id=%x Size=0x%x at=%p count=%d\n",
      CmObject.ObjectId,
      CmObject.Size,
      CmObject.Count,
      CmObject.Count);

      Count++;
  }

  Print(L"Found %d objects\n", Count);
  return EFI_SUCCESS;
}


