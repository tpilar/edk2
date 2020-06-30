/** @file

  Copyright (c) 2017 - 2019, ARM Limited. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

  @par Glossary:
    - PFN   - Pointer to a Function

**/

#ifndef TABLE_HELPER_LIB_H_
#define TABLE_HELPER_LIB_H_

#include <Protocol/ConfigurationManagerProtocol.h>

/**
  Get a unique token that can be used for configuration object
  cross referencing.

  @retval Unique arbitrary cross reference token.
**/
UINTN
EFIAPI
GetNewToken();

/**
  Returns the user friendly name for the given ObjectId.

  @param[in]  CmObjectId   The id of the configuration manager object
  @return                  User friendly name for object id.
**/
const CHAR16*
EFIAPI
CmObjectIdName(
  IN CONST CM_OBJECT_ID            CmObjectrId
  );

/**
  Return the count of objects of a given ObjectId.

  @param[in]  CmObjectId   The id of the desired configuration objects.
  @param[out] ItemCount    Number of objects with given ObjectId.
**/
EFI_STATUS
EFIAPI
CfgMgrCountObjects (
  IN   CONST  CM_OBJECT_ID         CmObjectId,
  OUT         UINT32               *ItemCount
  );

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
  );

/**
  Prototype for an initialiser function to be used by component
  libraries that are linked as NULL libraries to a Configuration
  Manager binary and used to populate said Configuration Manager
  with objects.

  @param[in] CfgMgrProtocol  The newly installed ConfigurationManagerProtocol
                             that can be used by the library to populate the
                             Configuration Manager with objects.
**/
typedef EFI_STATUS (EFIAPI *CFG_MGR_COMPONENT_LIB_INIT) (
  IN CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL *CfgMgrProtocol
  );

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
  @retval EFI_OUT_OF_RESOURCES   Failed to allocate necessary memory.
  @retval EFI_SUCCESS            Registration was successful.
**/
EFI_STATUS
EFIAPI
RegisterForCfgManager (
  IN CONST CFG_MGR_COMPONENT_LIB_INIT InitFunction
  );

/**
  Remove a configuration object from the configuration manager. If a
  cross reference token is supplied, only objects referenced by that
  token will be removed. If a token is not supplied, all objects of the
  given type will be removed.

  @param[in] CmObjectId   The id of the object that is to be removed.
  @param[in] Token        Unique cross-reference token of the object to be removed.

  @retval EFI_UNSUPPORTED There is no configuration manager installed.
  @retval EFI_NOT_FOUND   The combination of id and token was not found in the
                          configuration manager.
  @retval EFI_SUCCESS     Object was successfully deleted.
**/
EFI_STATUS
EFIAPI
CfgMgrRemoveObject (
  IN  CONST CM_OBJECT_ID                                  CmObjectId,
  IN  CONST CM_OBJECT_TOKEN                               Token OPTIONAL
  );

/**
  Add an instance of object to the configuration manager. If an object with
  the specified object id and token already exists in the manager, append the
  provided object to the existing list. Otherwise, create a new list with this
  object being the only member.

  @param[in] CmObjectId The id of the object that is to be added.
  @param[in] Token      The unique cross-reference token for this object.
  @param[in] Buffer     The instance of the object being added.
  @param[in] BufferSize Size of Buffer in bytes.

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
  );

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
  );

/**
  Retrieve an object with a given id from the installed configuration
  manager. If a token is not specified, returns all objects of given
  id, regardless of token. The methods unwraps the CmObject abstraction
  and only returns the payloads.

  @param[in]  CmObjectId   The id of the desired configuration objects.
  @param[in]  Token        Optional cross reference token. If not supplied, all.
                           objects of the given id are returned.
  @param[out] Buffer       Buffer containing a number of payloads of CmObjects.
  @param[out] ItemCount    The count of payloads in Buffer.
**/
EFI_STATUS
EFIAPI
CfgMgrGetObjects (
  IN   CONST  CM_OBJECT_ID         CmObjectId,
  IN   CONST  CM_OBJECT_TOKEN      Token        OPTIONAL,
  OUT         VOID **              Buffer       OPTIONAL,
  OUT         UINT32 *             ItemCount    OPTIONAL
  );

/** The CfgMgrGetInfo function gets the CM_STD_OBJ_CONFIGURATION_MANAGER_INFO
    object from the Configuration Manager.

  @param [out] CfgMfrInfo     Pointer to the Configuration Manager Info
                              object structure.

  @retval EFI_SUCCESS           The object is returned.
  @retval EFI_NOT_FOUND         The requested Object is not found.
**/
EFI_STATUS
EFIAPI
CfgMgrGetInfo (
  OUT       CM_STD_OBJ_CONFIGURATION_MANAGER_INFO    **        CfgMfrInfo
  );

/** The AddAcpiHeader function updates the ACPI header structure pointed by
    the AcpiHeader. It utilizes the ACPI table Generator and the Configuration
    Manager protocol to obtain any information required for constructing the
    header.

  @param [in]     Generator      Pointer to the ACPI table Generator.
  @param [in,out] AcpiHeader     Pointer to the ACPI table header to be
                                 updated.
  @param [in]     AcpiTableInfo  Pointer to the ACPI table info structure.
  @param [in]     Length         Length of the ACPI table.

  @retval EFI_SUCCESS           The ACPI table is updated successfully.
  @retval EFI_INVALID_PARAMETER A parameter is invalid.
  @retval EFI_NOT_FOUND         The required object information is not found.
**/
EFI_STATUS
EFIAPI
AddAcpiHeader (
  IN      CONST ACPI_TABLE_GENERATOR                  * CONST Generator,
  IN OUT  EFI_ACPI_DESCRIPTION_HEADER                 * CONST AcpiHeader,
  IN      CONST CM_STD_OBJ_ACPI_TABLE_INFO            * CONST AcpiTableInfo,
  IN      CONST UINT32                                        Length
  );

/**
  Function prototype for testing if two arbitrary objects are equal.

  @param [in] Object1           Pointer to the first object to compare.
  @param [in] Object2           Pointer to the second object to compare.
  @param [in] Index1            Index of Object1. This value is optional and
                                can be ignored by the specified implementation.
  @param [in] Index2            Index of Object2. This value is optional and
                                can be ignored by the specified implementation.

  @retval TRUE                  Object1 and Object2 are equal.
  @retval FALSE                 Object1 and Object2 are NOT equal.
**/
typedef
BOOLEAN
(EFIAPI *PFN_IS_EQUAL)(
  IN CONST  VOID            * Object1,
  IN CONST  VOID            * Object2,
  IN        UINTN             Index1 OPTIONAL,
  IN        UINTN             Index2 OPTIONAL
  );

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
  );

#endif // TABLE_HELPER_LIB_H_
