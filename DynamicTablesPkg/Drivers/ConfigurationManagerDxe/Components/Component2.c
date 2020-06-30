#include <Library/BaseLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/TableHelperLib.h>
#include <Protocol/ConfigurationManagerProtocol.h>
#include <Uefi.h>

EFI_STATUS
EFIAPI
Component2Init (
  IN CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL *CfgMgrProtocol
  )
{
  PrintSerial(L"Triggering Init on Component1\n");

  EFI_STRING Data = L"This is object 2.1";
  EFI_STRING Data2 = L"This is object 2.2";

  CfgMgrAddObject(0x1, CM_NULL_TOKEN, Data, StrnSizeS(Data, 64));
  CfgMgrAddObject(0x1, CM_NULL_TOKEN, Data2, StrnSizeS(Data2, 64));
  CfgMgrAddObject(0x2, CM_NULL_TOKEN, Data2, StrnSizeS(Data2, 64));

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
ComponentLib2Constructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  RegisterForCfgManager(Component2Init);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
ComponentLib2Init (
  IN  EFI_HANDLE            ImageHandle,
  IN  EFI_SYSTEM_TABLE     *SystemTable
  )
{
  ComponentLib2Constructor(ImageHandle, SystemTable);
  return EFI_SUCCESS;
}
