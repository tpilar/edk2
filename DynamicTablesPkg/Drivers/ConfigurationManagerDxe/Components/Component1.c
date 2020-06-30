#include <Library/BaseLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/TableHelperLib.h>
#include <Protocol/ConfigurationManagerProtocol.h>
#include <Uefi.h>

EFI_STATUS
EFIAPI
Component1Init (
  IN CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL *CfgMgrProtocol
  )
{
  PrintSerial(L"Triggering Init on Component1\n");

  CHAR16 *Data = L"This is object 1.2";
  CHAR16 *Data2 = L"This is object 1.2";

  CfgMgrAddObject(0x1, CM_NULL_TOKEN, Data, StrnSizeS(Data, 64));
  CfgMgrAddObject(0x1, CM_NULL_TOKEN, Data2, StrnSizeS(Data2, 64));
  CfgMgrAddObject(0x2, CM_NULL_TOKEN, Data2, StrnSizeS(Data2, 64));

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
ComponentLib1Constructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  RegisterForCfgManager(Component1Init);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
ComponentLib1Init (
  IN  EFI_HANDLE            ImageHandle,
  IN  EFI_SYSTEM_TABLE     *SystemTable
  )
{
  ComponentLib1Constructor(ImageHandle, SystemTable);
  return EFI_SUCCESS;
}
