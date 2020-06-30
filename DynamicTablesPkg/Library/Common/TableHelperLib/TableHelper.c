/** @file
  Table Helper

Copyright (c) 2017 - 2019, ARM Limited. All rights reserved.
SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Protocol/AcpiTable.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/PrintLib.h>

// Module specific include files.
#include <AcpiTableGenerator.h>
#include <ConfigurationManagerObject.h>
#include <Library/TableHelperLib.h>
#include <Protocol/ConfigurationManagerProtocol.h>

/**
  Get a unique token that can be used for configuration object
  cross referencing.

  @retval Unique arbitrary cross reference token
**/
UINTN
EFIAPI
GetNewToken()
{
  UINTN Token;
  EFI_STATUS Status = gBS->GetNextMonotonicCount(&Token);
  if (EFI_ERROR(Status)) {
    return CM_NULL_TOKEN;
  }

  return Token;
}

/**
  Event callback for executing the registered component library
  inintialiser with the newly installed ConfigurationManagerProtocol
  as the only parameter.
**/
STATIC
VOID
EFIAPI
ComponentInitEvent (
  IN EFI_EVENT Event,
  IN VOID     *Context
  )
{
  ASSERT (Context != NULL);

  CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL *CfgMgrProtocol;
  CFG_MGR_COMPONENT_LIB_INIT InitFunction = Context;

  EFI_STATUS Status = gBS->LocateProtocol (
    &gEdkiiConfigurationManagerProtocolGuid,
    NULL,
    (VOID **) &CfgMgrProtocol);


  if (EFI_ERROR(Status)) {    // Should never happen
    gBS->CloseEvent(Event);
    RegisterForCfgManager(InitFunction);
    return;
  }

  InitFunction(CfgMgrProtocol);
}

/**
  Register a callback inintialiser to be called when a configuration
  manager is installed. The initialiser function is expected to
  populate the newly installed configuration manager with objects when
  called.

  This helper should be used by component libraries that want to
  provide configuration objects and are to be linked in as NULL
  libraries into the configuration manager binary.

  @param[in] InitFunction   An initialiser function that will be called when
                            a configuration manager becomes available.
  @retval EFI_OUT_OF_RESOURCES   Failed to allocate necessary memory
  @retval EFI_SUCCESS            Registration was successful
**/
EFI_STATUS
EFIAPI
RegisterForCfgManager (
  CONST CFG_MGR_COMPONENT_LIB_INIT InitFunction
  )
{
  EFI_STATUS Status = EFI_NOT_STARTED;
  EFI_EVENT InitEvent;
  VOID *Registration;

  ASSERT(InitFunction != NULL);

  Status = gBS->CreateEvent (
    EVT_NOTIFY_SIGNAL,
    TPL_NOTIFY,
    ComponentInitEvent,
    InitFunction,
    &InitEvent);

  if (EFI_ERROR(Status)) {
    return Status;
  }

  Status = gBS->RegisterProtocolNotify (
    &gEdkiiConfigurationManagerProtocolGuid,
    InitEvent,
    &Registration);

  if (EFI_ERROR(Status)) {
    gBS->CloseEvent(InitEvent);
  }

  return Status;
}

/**
  Return the count of objects of a given ObjectId.
  If there are no objects, ItemCount is set to zero.

  @param[in]  CmObjectId   The id of the desired configuration objects.
  @param[out] ItemCount    Number of objects with given ObjectId.
**/
EFI_STATUS
EFIAPI
CfgMgrCountObjects (
  IN   CONST  CM_OBJECT_ID         CmObjectId,
  OUT         UINT32               *ItemCount
  )
{
  EFI_STATUS Status = EFI_NOT_STARTED;

  Status = CfgMgrGetObjects (CmObjectId, CM_NULL_TOKEN, NULL, ItemCount);
  if (Status == EFI_NOT_FOUND) {
    *ItemCount = 0;
  }

  return Status;
}

/**
  Retrieve an object with a given id from the installed configuration
  manager. If a token is not specified, returns all objects of given
  id, regardless of token. The methods unwraps the CmObject abstraction
  and only returns the payloads.

  If Buffer is not NULL, the data will be returned in allocated memory. The
  caller must free this memory when they are done with the data.

  If ItemCount is not NULL, the count of items matching the criteria
  is returned.

  @param[in]  CmObjectId   The id of the desired configuration objects
  @param[in]  Token        Optional cross reference token. If not supplied, all
                           objects of the given id are returned.
  @param[out] Buffer       Buffer containing a number of payloads of CmObjects.
  @param[out] ItemCount    The count of payloads in Buffer
**/
EFI_STATUS
EFIAPI
CfgMgrGetObjects (
  IN   CONST  CM_OBJECT_ID         CmObjectId,
  IN   CONST  CM_OBJECT_TOKEN      Token        OPTIONAL,
  OUT         VOID **              Buffer       OPTIONAL,
  OUT         UINT32 *             ItemCount    OPTIONAL
  )
{
  EDKII_CONFIGURATION_MANAGER_PROTOCOL *CfgMgr;
  EFI_STATUS Status;

  Status = gBS->LocateProtocol (
    &gEdkiiConfigurationManagerProtocolGuid, NULL, (VOID **) &CfgMgr);
  if (EFI_ERROR(Status)) {
    DEBUG ((DEBUG_ERROR, "ERROR: No Configuration Manager Protocol Found!\n"));
    return EFI_UNSUPPORTED;
  }

  CM_OBJ_DESCRIPTOR Object;

  Status = CfgMgr->GetObject(CfgMgr, CmObjectId, Token, &Object);
  if (EFI_ERROR(Status)) {
    if (Status != EFI_NOT_FOUND) {
      DEBUG (
        (DEBUG_ERROR,
         "ERROR: FADT: Failed to get <%s> [%r]\n",
         CmObjectIdName (CmObjectId),
         Status));
    }

    return Status;
  }

  if (Buffer) {
    *Buffer = AllocateCopyPool (Object.Size, Object.Data);
    if (Buffer == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
  }

  if (ItemCount) {
    *ItemCount = Object.Count;
  }

  if (CfgMgr->Revision >= CREATE_REVISION(1, 1)) {
    CfgMgr->FreeObject(CfgMgr, &Object);
  }

  return EFI_SUCCESS;
}

/**
  Get a single object form the configuration manager with the
  matching ObjectId regardless of any cross reference tokens.

  @param[in]  CmObjectId   The id of the desired configuration object
  @param[out] Buffer       Buffer containing the payload of the CmObject.

  @retval EFI_SUCCESS      Payload was successfully returned
  @retval EFI_NOT_FOUND    There was no such object
  @retval EFI_UNSUPPORTED  ConfigurationManangerProtocol is not installed
**/
EFI_STATUS
EFIAPI
CfgMgrGetSimpleObject(
  IN  CONST CM_OBJECT_ID             CmObjectId,
  OUT         VOID **                Buffer
  )
{
  EFI_STATUS Status;

  Status = CfgMgrGetObjects(CmObjectId, CM_NULL_TOKEN, Buffer, NULL);
  if (Status == EFI_NOT_FOUND) {
    DEBUG ((DEBUG_ERROR,
            "ERROR: Failed to get <%s> [%r]\n",
            CmObjectIdName (CmObjectId),
            Status));
  }
  return Status;
}

/**
  Add an instance of object to the configuration manager. If an object with
  the specified object id and token already exists in the manager, append the
  provided object to the existing list. Otherwise, create a new list with this
  object being the only member.

  @param[in] CmObjectId The id of the object that is to be added
  @param[in] Token      The unique cross-reference token for this object
  @param[in] Buffer     The instance of the object being added
  @param[in] BufferSize Size of Buffer in bytes

  @retval EFI_OUT_OF_RESOURCES   Failed to allocate required memory when appending data
  @retval EFI_UNSUPPORTED        There is no Configuration Manager installed
  @retval EFI_SUCCESS            Object was successfully added to the Configuration Manager
**/
EFI_STATUS
EFIAPI
CfgMgrAddObject (
  IN  CONST CM_OBJECT_ID                                  CmObjectId,
  IN  CONST CM_OBJECT_TOKEN                               Token OPTIONAL,
  IN        VOID *                                        Buffer,
  IN        UINTN                                         BufferSize
  )
{
  EFI_STATUS Status;
  EDKII_CONFIGURATION_MANAGER_PROTOCOL *CfgMgrProtocol;
  CM_OBJ_DESCRIPTOR CurrentObject = { 0 };
  CM_OBJ_DESCRIPTOR NewObject;

  ASSERT(Buffer != NULL);
  ASSERT(BufferSize != 0);

  Status = gBS->LocateProtocol (
    &gEdkiiConfigurationManagerProtocolGuid, NULL, (VOID **) &CfgMgrProtocol);

  if (EFI_ERROR(Status)) {
    return EFI_UNSUPPORTED;
  }

  Status = CfgMgrProtocol->GetObject (
    CfgMgrProtocol, CmObjectId, Token, &CurrentObject);

  NewObject.ObjectId = CmObjectId;
  NewObject.Count = 1 + CurrentObject.Count;
  NewObject.Size = BufferSize +CurrentObject.Size;

  NewObject.Data = AllocateZeroPool(NewObject.Size);
  if (NewObject.Data == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  CopyMem(NewObject.Data, CurrentObject.Data, CurrentObject.Size); // NOP if CurrentObject does not exist
  CopyMem((UINT8 *) NewObject.Data + CurrentObject.Size, Buffer, BufferSize);

  Status =
    CfgMgrProtocol->SetObject (CfgMgrProtocol, CmObjectId, Token, &NewObject);

  FreePool (NewObject.Data);
  return Status;
}

/**
  Add multiple objects of the same type/token to the configuration manager.
  If an object with the specified object id and token already exists in the
  manager, append the provided objects to the existing list. Otherwise, create
  a new list.

  @param[in] CmObjectId The id of the object that is to be added.
  @param[in] Token      The unique cross-reference token for this object.
  @param[in] Buffer     The instance of the objects being added.
  @param[in] BufferSize Size of Buffer in bytes.
  @param[in] ItemCount  Number of instances of object in the Buffer.

  @retval EFI_OUT_OF_RESOURCES   Failed to allocate required memory when appending data.
  @retval EFI_UNSUPPORTED        There is no Configuration Manager installed.
  @retval EFI_SUCCESS            Object was successfully added to the Configuration Manager.
**/
EFI_STATUS
EFIAPI
CfgMgrAddObjects (
  IN  CONST CM_OBJECT_ID                                  CmObjectId,
  IN  CONST CM_OBJECT_TOKEN                               Token OPTIONAL,
  IN        VOID *                                        Buffer,
  IN        UINTN                                         BufferSize,
  IN        UINT32                                        ItemCount
  )
{
  UINTN Index;
  UINT8 *Cursor = Buffer;
  UINTN ItemSize = BufferSize / ItemCount;
  EFI_STATUS Status = EFI_NOT_STARTED;

  for (Index = 0; Index < ItemCount; Index++) {
    Status = CfgMgrAddObject(CmObjectId, Token, Cursor, ItemSize);
    if (EFI_ERROR(Status)) {
      return Status;
    }
    Cursor += ItemSize;
  }

  return EFI_SUCCESS;
}

/**
  Remove a configuration object from the configuration manager. If a
  cross reference token is supplied, only objects referenced by that
  token will be removed. If a token is not supplied, all objects of the
  given type will be removed.

  @param[in] CmObjectId   The id of object that is to be removed
  @param[in] Token        Unique cross-reference token of the object to be removed

  @retval EFI_UNSUPPORTED There is no configuration manager installed
  @retval EFI_NOT_FOUND   The combination of id and token was not found in the
                          configuration manager
  @retval EFI_SUCCESS     Object was successfully deleted
**/
EFI_STATUS
EFIAPI
CfgMgrRemoveObject (
  IN  CONST CM_OBJECT_ID                                  CmObjectId,
  IN  CONST CM_OBJECT_TOKEN                               Token OPTIONAL
  )
{
  EFI_STATUS Status = EFI_NOT_STARTED;
  EDKII_CONFIGURATION_MANAGER_PROTOCOL *CfgMgrProtocol;
  CM_OBJ_DESCRIPTOR CurrentObject;

  Status = gBS->LocateProtocol (
    &gEdkiiConfigurationManagerProtocolGuid, NULL, (VOID **) &CfgMgrProtocol);

  if (EFI_ERROR(Status)) {
    return EFI_UNSUPPORTED;
  }

  Status = CfgMgrProtocol->GetObject (
    CfgMgrProtocol, CmObjectId, Token, &CurrentObject);

  if (EFI_ERROR(Status)) {
    return Status;
  }

  return CfgMgrProtocol->SetObject (CfgMgrProtocol, CmObjectId, Token, NULL);
}


/** The GetCgfMgrInfo function gets the CM_STD_OBJ_CONFIGURATION_MANAGER_INFO
    object from the Configuration Manager.

  @param [in]  CfgMgrProtocol Pointer to the Configuration Manager protocol
                              interface.
  @param [out] CfgMfrInfo     Pointer to the Configuration Manager Info
                              object structure.

  @retval EFI_SUCCESS           The object is returned.
  @retval EFI_INVALID_PARAMETER The Object ID is invalid.
  @retval EFI_NOT_FOUND         The requested Object is not found.
  @retval EFI_BAD_BUFFER_SIZE   The size returned by the Configuration
                                Manager is less than the Object size.
**/
EFI_STATUS
EFIAPI
GetCgfMgrInfo (
  IN  CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL      * CONST  CfgMgrProtocol,
  OUT       CM_STD_OBJ_CONFIGURATION_MANAGER_INFO    **        CfgMfrInfo
  )
{
  EFI_STATUS         Status;
  CM_OBJ_DESCRIPTOR  CmObjectDesc;

  ASSERT (CfgMgrProtocol != NULL);
  ASSERT (CfgMfrInfo != NULL);

  *CfgMfrInfo = NULL;

  Status = CfgMgrProtocol->GetObject (
                             CfgMgrProtocol,
                             CREATE_CM_STD_OBJECT_ID (EStdObjCfgMgrInfo),
                             CM_NULL_TOKEN,
                             &CmObjectDesc
                             );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "ERROR: Failed to Get Configuration Manager Info. Status = %r\n",
      Status
      ));
    return Status;
  }

  if (CmObjectDesc.ObjectId != CREATE_CM_STD_OBJECT_ID (EStdObjCfgMgrInfo)) {
    DEBUG ((
      DEBUG_ERROR,
      "ERROR: EStdObjCfgMgrInfo: Invalid ObjectId = 0x%x, expected Id = 0x%x\n",
      CmObjectDesc.ObjectId,
      CREATE_CM_STD_OBJECT_ID (EStdObjCfgMgrInfo)
      ));
    ASSERT (FALSE);
    return EFI_INVALID_PARAMETER;
  }

  if (CmObjectDesc.Size <
      (sizeof (CM_STD_OBJ_CONFIGURATION_MANAGER_INFO) * CmObjectDesc.Count)) {
    DEBUG ((
      DEBUG_ERROR,
      "ERROR: EStdObjCfgMgrInfo: Buffer too small, size  = 0x%x\n",
      CmObjectDesc.Size
      ));
    ASSERT (FALSE);
    return EFI_BAD_BUFFER_SIZE;
  }

  *CfgMfrInfo = (CM_STD_OBJ_CONFIGURATION_MANAGER_INFO*)CmObjectDesc.Data;
  return Status;
}

/** The AddAcpiHeader function updates the ACPI header structure pointed by
    the AcpiHeader. It utilizes the ACPI table Generator and the Configuration
    Manager protocol to obtain any information required for constructing the
    header.

  @param [in]     CfgMgrProtocol Pointer to the Configuration Manager
                                 protocol interface.
  @param [in]     Generator      Pointer to the ACPI table Generator.
  @param [in,out] AcpiHeader     Pointer to the ACPI table header to be
                                 updated.
  @param [in]     AcpiTableInfo  Pointer to the ACPI table info structure.
  @param [in]     Length         Length of the ACPI table.

  @retval EFI_SUCCESS           The ACPI table is updated successfully.
  @retval EFI_INVALID_PARAMETER A parameter is invalid.
  @retval EFI_NOT_FOUND         The required object information is not found.
  @retval EFI_BAD_BUFFER_SIZE   The size returned by the Configuration
                                Manager is less than the Object size for the
                                requested object.
**/
EFI_STATUS
EFIAPI
AddAcpiHeader (
  IN      CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST CfgMgrProtocol,
  IN      CONST ACPI_TABLE_GENERATOR                  * CONST Generator,
  IN OUT  EFI_ACPI_DESCRIPTION_HEADER                 * CONST AcpiHeader,
  IN      CONST CM_STD_OBJ_ACPI_TABLE_INFO            * CONST AcpiTableInfo,
  IN      CONST UINT32                                        Length
  )
{
  EFI_STATUS                               Status;
  CM_STD_OBJ_CONFIGURATION_MANAGER_INFO  * CfgMfrInfo;

  ASSERT (CfgMgrProtocol != NULL);
  ASSERT (Generator != NULL);
  ASSERT (AcpiHeader != NULL);
  ASSERT (Length >= sizeof (EFI_ACPI_DESCRIPTION_HEADER));

  if ((CfgMgrProtocol == NULL) ||
      (Generator == NULL) ||
      (AcpiHeader == NULL) ||
      (Length < sizeof (EFI_ACPI_DESCRIPTION_HEADER))
    ) {
    return EFI_INVALID_PARAMETER;
  }

  Status = GetCgfMgrInfo (CfgMgrProtocol, &CfgMfrInfo);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "ERROR: Failed to get Configuration Manager info. Status = %r\n",
      Status
      ));
    goto error_handler;
  }

  // UINT32  Signature
  AcpiHeader->Signature = Generator->AcpiTableSignature;
  // UINT32  Length
  AcpiHeader->Length = Length;
  // UINT8   Revision
  AcpiHeader->Revision = AcpiTableInfo->AcpiTableRevision;
  // UINT8   Checksum
  AcpiHeader->Checksum = 0;

  // UINT8   OemId[6]
  CopyMem (AcpiHeader->OemId, CfgMfrInfo->OemId, sizeof (AcpiHeader->OemId));

  // UINT64  OemTableId
  if (AcpiTableInfo->OemTableId != 0) {
    AcpiHeader->OemTableId = AcpiTableInfo->OemTableId;
  } else {
    AcpiHeader->OemTableId = SIGNATURE_32 (
                               CfgMfrInfo->OemId[0],
                               CfgMfrInfo->OemId[1],
                               CfgMfrInfo->OemId[2],
                               CfgMfrInfo->OemId[3]
                               ) |
                             ((UINT64)Generator->AcpiTableSignature << 32);
  }

  // UINT32  OemRevision
  if (AcpiTableInfo->OemRevision != 0) {
    AcpiHeader->OemRevision = AcpiTableInfo->OemRevision;
  } else {
    AcpiHeader->OemRevision = CfgMfrInfo->Revision;
  }

  // UINT32  CreatorId
  AcpiHeader->CreatorId = Generator->CreatorId;
  // UINT32  CreatorRevision
  AcpiHeader->CreatorRevision = Generator->CreatorRevision;

error_handler:
  return Status;
}

/**
  Test and report if a duplicate entry exists in the given array of comparable
  elements.

  @param [in] Array                 Array of elements to test for duplicates.
  @param [in] Count                 Number of elements in Array.
  @param [in] ElementSize           Size of an element in bytes
  @param [in] EqualTestFunction     The function to call to check if any two
                                    elements are equal.

  @retval TRUE                      A duplicate element was found or one of
                                    the input arguments is invalid.
  @retval FALSE                     Every element in Array is unique.
**/
BOOLEAN
EFIAPI
FindDuplicateValue (
  IN  CONST VOID          * Array,
  IN  CONST UINTN           Count,
  IN  CONST UINTN           ElementSize,
  IN        PFN_IS_EQUAL    EqualTestFunction
  )
{
  UINTN         Index1;
  UINTN         Index2;
  UINT8       * Element1;
  UINT8       * Element2;

  if (Array == NULL) {
    DEBUG ((DEBUG_ERROR, "ERROR: FindDuplicateValues: Array is NULL.\n"));
    return TRUE;
  }

  if (ElementSize == 0) {
    DEBUG ((DEBUG_ERROR, "ERROR: FindDuplicateValues: ElementSize is 0.\n"));
    return TRUE;
  }

  if (EqualTestFunction == NULL) {
    DEBUG ((
      DEBUG_ERROR,
      "ERROR: FindDuplicateValues: EqualTestFunction is NULL.\n"
      ));
    return TRUE;
  }

  if (Count < 2) {
    return FALSE;
  }

  for (Index1 = 0; Index1 < Count - 1; Index1++) {
    for (Index2 = Index1 + 1; Index2 < Count; Index2++) {
      Element1 = (UINT8*)Array + (Index1 * ElementSize);
      Element2 = (UINT8*)Array + (Index2 * ElementSize);

      if (EqualTestFunction (Element1, Element2, Index1, Index2)) {
        return TRUE;
      }
    }
  }
  return FALSE;
}
