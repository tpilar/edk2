/** @file

  Copyright (c) 2017 - 2018, ARM Limited. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

  @par Glossary:
    - Cm or CM   - Configuration Manager
    - Obj or OBJ - Object
**/

#ifndef CONFIGURATION_MANAGER_PROTOCOL_H_
#define CONFIGURATION_MANAGER_PROTOCOL_H_

#include <ConfigurationManagerObject.h>

/** This macro defines the Configuration Manager Protocol GUID.

  GUID: {D85A4835-5A82-4894-AC02-706F43D5978E}
*/
#define EDKII_CONFIGURATION_MANAGER_PROTOCOL_GUID       \
  { 0xd85a4835, 0x5a82, 0x4894,                         \
    { 0xac, 0x2, 0x70, 0x6f, 0x43, 0xd5, 0x97, 0x8e }   \
  };

/** This macro defines the Configuration Manager Protocol Revision.
*/
#define EDKII_CONFIGURATION_MANAGER_PROTOCOL_REVISION  CREATE_REVISION (1, 1)

#pragma pack(1)

/**
  Forward declarations:
*/
typedef struct ConfigurationManagerProtocol EDKII_CONFIGURATION_MANAGER_PROTOCOL;
typedef struct PlatformRepositoryInfo       EDKII_PLATFORM_REPOSITORY_INFO;

/** The GetObject function defines the interface of the
    Configuration Manager Protocol for returning the Configuration
    Manager Objects.

    If Token is CM_NULL_TOKEN, the function provides in its output all
    the objects of the given CmObjectId. If the Token is not CM_NULL_TOKEN,
    the function provides only those object that match both the CmObjectId
    and Token.

    The memory in CmObject.Data may be static or dynamic. The caller of this
    function must call FreeObject on the CmObject populated by this function.

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
typedef
EFI_STATUS
(EFIAPI * EDKII_CONFIGURATION_MANAGER_GET_OBJECT) (
  IN  CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST This,
  IN  CONST CM_OBJECT_ID                                  CmObjectId,
  IN  CONST CM_OBJECT_TOKEN                               Token OPTIONAL,
  IN  OUT   CM_OBJ_DESCRIPTOR                     * CONST CmObject
  );

/** The SetObject function defines the interface of the
    Configuration Manager Protocol for updating the Configuration
    Manager Objects.

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
typedef
EFI_STATUS
(EFIAPI * EDKII_CONFIGURATION_MANAGER_SET_OBJECT) (
  IN  CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST This,
  IN  CONST CM_OBJECT_ID                                  CmObjectId,
  IN  CONST CM_OBJECT_TOKEN                               Token OPTIONAL,
  IN        CM_OBJ_DESCRIPTOR                     * CONST CmObject
  );

/** The FreeObject function defines the interface of the
    Configuration Manager Protocol for correctly freeing resources
    that have been reserved by calls to the GetObject interface.

    The caller of GetObject must use this function to dispose of CmObject
    populated by the GetObject call when the CmObject is no longer needed.

    If an implementation of the Configuration Manager Protocol does not
    use dynamically allocated memory, this function should simply return
    EFI_SUCCESS.

    @param [in]  This         Pointer to the Configuration Manager Protocol
    @param [in]  CmObject     Pointer to the CmObject that has been populated
                              by the GetObject function and is to be destroyed.
    @retval EFI_SUCCESS       The CmObject was successfully destroyed
**/
typedef
EFI_STATUS
(EFIAPI * EDKII_CONFIGURATION_MANAGER_FREE_OBJECT) (
  IN  CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST This,
  IN        CM_OBJ_DESCRIPTOR                     *       CmObject
  );

/** The EDKII_CONFIGURATION_MANAGER_PROTOCOL structure describes the
    Configuration Manager Protocol interface.
*/
typedef struct ConfigurationManagerProtocol {
  /// The Configuration Manager Protocol revision.
  UINT32                                  Revision;

  /** The interface used to request information about
      the Configuration Manager Objects.
  */
  EDKII_CONFIGURATION_MANAGER_GET_OBJECT  GetObject;

  /** The interface used to update the information stored
      in the Configuration Manager repository.
  */
  EDKII_CONFIGURATION_MANAGER_SET_OBJECT  SetObject;

  /** Pointer to an implementation defined abstract repository
      provisioned by the Configuration Manager.
  */
  EDKII_PLATFORM_REPOSITORY_INFO        * PlatRepoInfo;

  /** The interface used to destroy CmObject instances
      populated by calls to GetObject
  */
  EDKII_CONFIGURATION_MANAGER_FREE_OBJECT FreeObject;
} EDKII_CONFIGURATION_MANAGER_PROTOCOL;

/** The Configuration Manager Protocol GUID.
*/
extern EFI_GUID gEdkiiConfigurationManagerProtocolGuid;

/** Inline NULL implementation of FreeObject for backward compatibility
    of configuration managers that do not require to deallocate any
    memory following a call to GetObject.

    @param[in] This       Pointer to Configuration Manager Protocol instance
    @param[in] CmObject   Pointer to CmObject populated by GetObject

    @retval EFI_SUCCESS            Successfully handled CmObject.
    @retval EFI_INVALID_PARAMETER  CmObject is NULL.
    @retval EFI_INVALID_PARAMETER  This is NULL.
    @retval EFI_INVALID_PARAMETER  CmObject is not valid.
**/
static
inline
EFI_STATUS
EFIAPI EdkiiCfgMgrFreeObjectNull (
  IN  CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST This,
  IN        CM_OBJ_DESCRIPTOR                     *       CmObject
  )
{
  if (!This || !CmObject) {
    return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

#pragma pack()

#endif // CONFIGURATION_MANAGER_PROTOCOL_H_
