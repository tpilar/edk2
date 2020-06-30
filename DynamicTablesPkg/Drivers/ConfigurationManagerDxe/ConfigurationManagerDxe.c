#include <Uefi.h>
#include <Protocol/ConfigurationManagerProtocol.h>

#include <Library/DebugLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/TableHelperLib.h>
#include <Library/UefiLib.h>

typedef struct _CM_LIST_ENTRY {
  LIST_ENTRY Entry;               ///< Linked list descriptor
  CM_OBJECT_TOKEN Token;          ///< Cross reference token for the object
  CM_OBJ_DESCRIPTOR Object;       ///< CM Object descriptor
} CM_LIST_ENTRY;

/*
  Linked list to internally store CM_LIST_ENTRIES
  Head node will never contain any data
*/
LIST_ENTRY ObjectList;

/**
  Unlink a given node from the object list, unallocating and
  zeroing memory.

  The method removes the entry from the object list, frees the
  underlying memory in the object stored in the entry, zeroes
  the entry instance and then frees the entry instance.

  @param[in] CmEntry The entry into the local ObjectList
**/
static
VOID
EFIAPI
DestroyNode (
  IN CM_LIST_ENTRY *CmEntry
  )
{
  if (!CmEntry) {
    return;
  }

  RemoveEntryList (&CmEntry->Entry);
  if (CmEntry->Object.Data) {
    FreePool(CmEntry->Object.Data);
  }

  ZeroMem(CmEntry, sizeof(CM_LIST_ENTRY));
}

/**
  Debug method to print information about stored nodes to serial
  port. Prints one line for each entry in the ObjectList.
**/
static
VOID
EFIAPI
DescribeDb (VOID)
{
  LIST_ENTRY *Entry;

  for (Entry = GetFirstNode (&ObjectList); Entry != &ObjectList;
       Entry = GetNextNode (&ObjectList, Entry)) {
    CM_LIST_ENTRY *CmEntry = (CM_LIST_ENTRY *) Entry;
    PrintSerial (
      L"Entry=%p Id=%x Token=%x %p[%d]=%x\n",
      CmEntry,
      CmEntry->Object.ObjectId,
      CmEntry->Token,
      CmEntry->Object.Data,
      CmEntry->Object.Count,
      CmEntry->Object.Size);
  }
}

/** Destroys a CmObject populated by a call to the GetObject interface.

    The caller of GetObject must use this function to dispose of CmObject
    populated by the GetObject call when the CmObject is no longer needed.

    @param [in]  This         Pointer to the Configuration Manager Protocol
    @param [in]  CmObject     Pointer to the CmObject that has been populated
                              by the GetObject function and is to be destroyed.
    @retval EFI_SUCCESS       The CmObject was successfully destroyed
**/
EFI_STATUS
EFIAPI
FreeObject (
  IN  CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST This,
  IN        CM_OBJ_DESCRIPTOR                     *       CmObject
  )
{
  if (!CmObject || !This || !CmObject->Data) {
    return EFI_INVALID_PARAMETER;
  }

  FreePool(CmObject->Data);
  ZeroMem(CmObject, sizeof(CM_OBJ_DESCRIPTOR));

  return EFI_SUCCESS;
}

/** Retrieves a CmObject with a matching ObjectId and a cross reference
    Token from the configuration manager.

    If Token is CM_NULL_TOKEN, the function provides in its output all
    the objects of the given CmObjectId. If the Token is not CM_NULL_TOKEN,
    the function provides only those object that match both the CmObjectId
    and Token.

    CmObject populated by this method must be destryed by the caller using
    the FreeObject interface.

  @param [in]  This        Pointer to the Configuration Manager Protocol.
  @param [in]  CmObjectId  The Configuration Manager Object ID.
  @param [in]  Token       An optional token identifying the object. If
                           unused this must be CM_NULL_TOKEN.
  @param [out] CmObject    Pointer to the Configuration Manager Object
                           descriptor describing the requested Object.

  @retval EFI_SUCCESS           Success.
  @retval EFI_INVALID_PARAMETER A parameter is invalid.
  @retval EFI_NOT_FOUND         The required object information is not found.
  @retval EFI_BAD_BUFFER_SIZE   The size returned by the Configuration Manager
                                is less than the Object size for the requested
                                object.
**/
EFI_STATUS
EFIAPI
GetObject (
  IN  CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST This,
  IN  CONST CM_OBJECT_ID                                  CmObjectId,
  IN  CONST CM_OBJECT_TOKEN                               Token OPTIONAL,
  IN  OUT   CM_OBJ_DESCRIPTOR                     * CONST CmObject
  )
{
  LIST_ENTRY *Entry;
  CM_LIST_ENTRY *CmEntry;
  UINTN NewSize;
  UINTN AllocatedSize;

  AllocatedSize = EFI_PAGE_SIZE;
  CmObject->Data = AllocateZeroPool (AllocatedSize);
  if (!CmObject->Data) {
    return EFI_OUT_OF_RESOURCES;
  }

  CmObject->Size = 0;
  CmObject->ObjectId = CmObjectId;

  for (Entry = GetFirstNode (&ObjectList); Entry != &ObjectList;
       Entry = GetNextNode (&ObjectList, Entry)) {
    CmEntry = (CM_LIST_ENTRY *) Entry;

    // Ignore items that do not match CmObjectId
    if (CmEntry->Object.ObjectId != CmObjectId) {
      continue;
    }

    // If we have Token, ignore items that do not match it
    if (Token != CM_NULL_TOKEN && Token != CmEntry->Token) {
      continue;
    }

    // We should almost never need to reallocate
    // 4k ought to be enough for everyone
    if (CmObject->Size + CmEntry->Object.Size > AllocatedSize) {
      NewSize = (AllocatedSize * CmEntry->Object.Size) * 2;
      CmObject->Data = ReallocatePool (AllocatedSize, NewSize, CmObject->Data);
      if (!CmObject->Data) {
        return EFI_OUT_OF_RESOURCES;
      }
      AllocatedSize = NewSize;
    }

    // Copy the contents into output object
    CopyMem (
      CmObject->Data + CmObject->Size,
      CmEntry->Object.Data,
      CmEntry->Object.Size);
    CmObject->Size += CmEntry->Object.Size;
    CmObject->Count += CmEntry->Object.Count;
  }

  if (CmObject->Count == 0) {
    FreeObject(This, CmObject);
    return EFI_NOT_FOUND;
  }

  return EFI_SUCCESS;
}

/** Modify the CmObject stored in the configuration manager that has a
    matching ObjectId and a cross reference Token.

    If Token is CM_NULL_TOKEN, and CmObject is not NULL, then the objects
    in the configuration manager that match the CmObjectId and do not
    have an associated cross reference Token are replaced by the contents of
    CmObject.

    If Token is not CM_NULL_TOKEN and CmObject is not NULL, then the objects
    that match both CmObjectId and Token in the configuration manager are
    replaced with the contents of CmObject.

    If CmObject is NULL, then objects that match the CmObjectId and Token
    are removed from the configuration manager. If Token is also CM_NULL_TOKEN,
    then all objects of given CmObjectId are removed, regardless of their
    cross-reference Token.

  @param [in]  This        Pointer to the Configuration Manager Protocol.
  @param [in]  CmObjectId  The Configuration Manager Object ID.
  @param [in]  Token       An optional token identifying the object. If
                           unused this must be CM_NULL_TOKEN.
  @param [out] CmObject    Pointer to the Configuration Manager Object
                           descriptor describing the Object.

  @retval EFI_SUCCESS           The operation completed successfully.
  @retval EFI_INVALID_PARAMETER A parameter is invalid.
  @retval EFI_NOT_FOUND         The required object information is not found.
  @retval EFI_BAD_BUFFER_SIZE   The size returned by the Configuration Manager
                                is less than the Object size for the requested
                                object.
  @retval EFI_UNSUPPORTED       This operation is not supported.
**/
EFI_STATUS
EFIAPI
SetObject (
  IN  CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST This,
  IN  CONST CM_OBJECT_ID                                  CmObjectId,
  IN  CONST CM_OBJECT_TOKEN                               Token OPTIONAL,
  IN        CM_OBJ_DESCRIPTOR                     * CONST CmObject
  )
{
  LIST_ENTRY *Entry;
  CM_LIST_ENTRY *CmEntry;

  VOID *Data = AllocateCopyPool (CmObject->Size, CmObject->Data);
  if (!Data) {
    return EFI_OUT_OF_RESOURCES;
  }

  for (Entry = GetFirstNode (&ObjectList); Entry != &ObjectList;
       Entry = GetNextNode (&ObjectList, Entry)) {
    CmEntry = (CM_LIST_ENTRY *) Entry;

    // Do not modify entry if object id mismatches.
    if (CmEntry->Object.ObjectId != CmObjectId) {
      continue;
    }

    // If token specified, do not modify entry if token mismatches.
    if (Token != CM_NULL_TOKEN && Token != CmEntry->Token) {
      continue;
    }

    // After discarding mismatched type and tokens erase all remaining nodes
    // if CmObject is NULL. If token is not specified this will erase all
    // nodes of the given id even if they have a token.
    if (CmObject == NULL) {
      DestroyNode(CmEntry);

    // If Object is not NULL, we set it on entry that matches token, even
    // if the token is CM_NULL_TOKEN. So setting an object without a token
    // will not erase objects of same id that do have a cross-reference token
    } else if (Token == CmEntry->Token) {
      FreePool(CmEntry->Object.Data);
      CmEntry->Object.Size = CmObject->Size;
      CmEntry->Object.Count = CmObject->Count;
      CmEntry->Object.ObjectId = CmObject->ObjectId;
      CmEntry->Object.Data = Data;
      return EFI_SUCCESS;
    }
  }

  if (!CmObject) {
    return EFI_SUCCESS;
  }

  // We still have an object and did not find a matching entry
  // in the ObjectList so we create a new one
  CmEntry = AllocateZeroPool (sizeof (CM_LIST_ENTRY));
  if (!CmEntry) {
    FreePool(Data);
    return EFI_OUT_OF_RESOURCES;
  }

  CmEntry->Token = Token;
  CmEntry->Object.Data = Data;
  CmEntry->Object.Size = CmObject->Size;
  CmEntry->Object.Count = CmObject->Count;
  CmEntry->Object.ObjectId = CmObject->ObjectId;

  InsertHeadList (&ObjectList, &CmEntry->Entry);

  return EFI_SUCCESS;
}

EDKII_CONFIGURATION_MANAGER_PROTOCOL CfgMgr = {
  EDKII_CONFIGURATION_MANAGER_PROTOCOL_REVISION,
  GetObject,
  SetObject,
  NULL,
  FreeObject
};

/**
  Initialiser method called when module is loaded and executed.

  Initialise the ObjectList and install the ConfigurationManagerProtocol
  instance on the ImageHandle.
**/
EFI_STATUS
EFIAPI
ConfigurationManagerInit (
  IN  EFI_HANDLE            ImageHandle,
  IN  EFI_SYSTEM_TABLE   *  SystemTable
  )
{
  EFI_STATUS Status = EFI_NOT_STARTED;
  CM_STD_OBJ_CONFIGURATION_MANAGER_INFO CfgMgrInfo = {
    .Revision = CREATE_REVISION(1, 1),
    .OemId = { 0 },
  };
  EFI_TPL CurrentTpl;

  // Lockout callbacks to prevent NULL libraries from populating
  // the manager before we are ready.
  CurrentTpl = gBS->RaiseTPL(TPL_NOTIFY);

  InitializeListHead(&ObjectList);

  Status = gBS->InstallMultipleProtocolInterfaces (
    &ImageHandle, &gEdkiiConfigurationManagerProtocolGuid, &CfgMgr, NULL);

  CfgMgrAddObject (
    EStdObjCfgMgrInfo,
    CM_NULL_TOKEN,
    &CfgMgrInfo,
    sizeof (CfgMgrInfo));

  if (CurrentTpl < TPL_NOTIFY) {
    gBS->RestoreTPL(CurrentTpl);
  }

  DescribeDb();

  return EFI_SUCCESS;
}
