/** @file
  IORT Table Generator

  Copyright (c) 2017 - 2019, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent

  @par Reference(s):
  - IO Remapping Table, Platform Design Document,
    Document number: ARM DEN 0049D, Issue D, March 2018

**/

#include <IndustryStandard/IoRemappingTable.h>
#include <Library/AcpiLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Protocol/AcpiTable.h>

// Module specific include files.
#include <AcpiTableGenerator.h>
#include <ConfigurationManagerObject.h>
#include <Library/TableHelperLib.h>
#include <Protocol/ConfigurationManagerProtocol.h>

#include "IortGenerator.h"

/** ARM standard IORT Generator

Requirements:
  The following Configuration Manager Object(s) are required by
  this Generator:
  - EArmObjItsGroup
  - EArmObjNamedComponent
  - EArmObjRootComplex
  - EArmObjSmmuV1SmmuV2
  - EArmObjSmmuV3
  - EArmObjPmcg
  - EArmObjGicItsIdentifierArray
  - EArmObjIdMappingArray
  - EArmObjGicItsIdentifierArray
*/

/*
  Function type that evaluates the size of a node and sets
  the node pointer to the next node. Used in iteration over
  node lists.
*/
typedef UINT32 (EFIAPI *INDEX_NODE)(VOID ** Node);

/** Returns the size of the ITS Group node, increments
    to the next node.

    @param [in,out]  Ptr    Pointer to ITS Group node.
    @retval Size of the ITS Group Node.
**/
STATIC
UINT32
GetItsGroupNodeSize (
  IN OUT  VOID ** Ptr
  )
{
  ASSERT (Ptr != NULL && *Ptr != NULL);

  CM_ARM_ITS_GROUP_NODE *Node = *Ptr;
  *Ptr = Node + 1;

  /* Size of ITS Group Node +
     Size of ITS Identifier array
  */
  return (UINT32) (
    sizeof (EFI_ACPI_6_0_IO_REMAPPING_ITS_NODE) +
    (Node->ItsIdCount) * sizeof (UINT32));
}

/** Returns the size of the Named Component node and
    point to the next node

    @param [in,out]  Ptr    Pointer to Named Component node.
    @retval Size of the Named Component node.
**/
STATIC
UINT32
GetNamedComponentNodeSize (
  IN OUT  VOID ** Ptr
  )
{
  ASSERT (Ptr != NULL && *Ptr != NULL);

  CM_ARM_NAMED_COMPONENT_NODE * Node = *Ptr;
  *Ptr = Node + 1;

  /* Size of Named Component node +
     Size of ID mapping array +
     Size of ASCII string + 'padding to 32-bit word aligned'.
  */

  return (UINT32)(sizeof (EFI_ACPI_6_0_IO_REMAPPING_NAMED_COMP_NODE) +
                    ((Node->IdMappingCount *
                     sizeof (EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE)) +
                    ALIGN_VALUE (AsciiStrSize (Node->ObjectName), 4)));
}

/** Returns the size of the Root Complex node and point
    to the next node.

    @param [in,out]  Ptr    Pointer to Root Complex node.
    @retval Size of the Root Complex node.
**/
STATIC
UINT32
GetRootComplexNodeSize (
  IN OUT  VOID  ** Ptr
  )
{
  ASSERT (Ptr != NULL && *Ptr != NULL);

  CM_ARM_ROOT_COMPLEX_NODE *Node = *Ptr;
  *Ptr = Node + 1;

  /* Size of Root Complex node +
     Size of ID mapping array
  */
  return (UINT32)(sizeof (EFI_ACPI_6_0_IO_REMAPPING_RC_NODE) +
                    (Node->IdMappingCount *
                     sizeof (EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE)));
}

/** Returns the size of the SMMUv1/SMMUv2 node and point
    to the next node.

    @param [in,out]  Ptr    Pointer to SMMUv1/SMMUv2 node list.
    @retval Size of the SMMUv1/SMMUv2 node.
**/
STATIC
UINT32
GetSmmuV1V2NodeSize (
  IN OUT  VOID  **Ptr
  )
{
  ASSERT (Ptr != NULL && *Ptr != NULL);

  CM_ARM_SMMUV1_SMMUV2_NODE  * Node = *Ptr;
  *Ptr = Node + 1;

  /* Size of SMMU v1/SMMU v2 node +
     Size of ID mapping array +
     Size of context interrupt array +
     Size of PMU interrupt array
  */
  return (UINT32)(sizeof (EFI_ACPI_6_0_IO_REMAPPING_SMMU_NODE) +
                    (Node->IdMappingCount *
                     sizeof (EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE)) +
                    (Node->ContextInterruptCount *
                     sizeof (EFI_ACPI_6_0_IO_REMAPPING_SMMU_INT)) +
                    (Node->PmuInterruptCount *
                     sizeof (EFI_ACPI_6_0_IO_REMAPPING_SMMU_INT)));
}

/** Returns the size of the SMMUv3 node and point to the next
    node.

    @param [in,out]  Ptr    Pointer to SMMUv3 node list.
    @retval Total size of the SMMUv3 nodes.
**/
STATIC
UINT32
GetSmmuV3NodeSize (
  IN OUT  VOID ** Ptr
  )
{
  ASSERT (Ptr != NULL && *Ptr != NULL);

  CM_ARM_SMMUV3_NODE  *Node = *Ptr;
  *Ptr = Node + 1;

  /* Size of SMMU v1/SMMU v2 node +
     Size of ID mapping array
  */
  return (UINT32)(sizeof (EFI_ACPI_6_0_IO_REMAPPING_SMMU3_NODE) +
                    (Node->IdMappingCount *
                     sizeof (EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE)));
}

/** Returns the size of the PMCG node and point to the next
    node.

    @param [in,out]  Ptr    Pointer to PMCG node.
    @retval Size of the PMCG node.
**/
STATIC
UINT32
GetPmcgNodeSize (
  IN OUT  VOID ** Ptr
  )
{
  ASSERT (Ptr != NULL && *Ptr != NULL);

  CM_ARM_PMCG_NODE  * Node = *Ptr;
  *Ptr = Node + 1;

  /* Size of PMCG node +
     Size of ID mapping array
  */
  return (UINT32)(sizeof (EFI_ACPI_6_0_IO_REMAPPING_PMCG_NODE) +
                    (Node->IdMappingCount *
                     sizeof (EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE)));
}

/** Returns the total size required for a group of IORT nodes. The configuration
    manager objects specified by object id must contain CM_OBJECT_TOKEN as
    their first field.

    This function calculates the size required for the node group
    and also populates the Node Indexer array with offsets for the
    individual nodes.

    @param [in]       ObjectId        The configuration manager object id of
                                      nodes that are to be summed.
    @param [in]       NodeStartOffset Offset from the start of the
                                      IORT where this node group starts.
    @param [in, out]  NodeIndexer     Pointer to the next Node Indexer.
    @param [in]       GetNodeSize     The function to determine the size of a single node
                                      of the appropriate type determined by object id.

    @retval Total size of the group of nodes
**/
STATIC
UINT64
GetSizeOfNodes (
  IN      CONST CM_OBJECT_ID                   ObjectId,
  IN      CONST UINT32                         NodeStartOffset,
  IN OUT        IORT_NODE_INDEXER     ** CONST NodeIndexer,
  IN      CONST INDEX_NODE                     IndexNode
  )
{
  UINT64 Size;
  EFI_STATUS Status;
  VOID *NodeList;
  UINT32 NodeCount;
  VOID *Cursor;

  Status = CfgMgrGetObjects (ObjectId, CM_NULL_TOKEN, &NodeList, &NodeCount);
  if (EFI_ERROR(Status)) {
    return 0;
  }

  Cursor = NodeList;
  Size = 0;
  while (NodeCount-- != 0) {
    (*NodeIndexer)->Token = *(CM_OBJECT_TOKEN *) Cursor; // CM_OBJECT_TOKEN is always the first element of a node
    (*NodeIndexer)->Object = Cursor;
    (*NodeIndexer)->Offset = (UINT32)(Size + NodeStartOffset);
    DEBUG ((
      DEBUG_INFO,
      "IORT: Node Indexer = %p, Token = %p, Object = %p, Offset = 0x%x\n",
      *NodeIndexer,
      (*NodeIndexer)->Token,
      (*NodeIndexer)->Object,
      (*NodeIndexer)->Offset
      ));

    Size += IndexNode (&Cursor);
    (*NodeIndexer)++;
  }

  FreePool (NodeList);
  return Size;
}

/** Returns the offset of the Node referenced by the Token.

    @param [in]  NodeIndexer  Pointer to node indexer array.
    @param [in]  NodeCount    Count of the nodes.
    @param [in]  Token        Reference token for the node.
    @param [out] NodeOffset   Offset of the node from the
                              start of the IORT table.

    @retval EFI_SUCCESS       Success.
    @retval EFI_NOT_FOUND     No matching token reference
                              found in node indexer array.
**/
STATIC
EFI_STATUS
GetNodeOffsetReferencedByToken (
  IN  IORT_NODE_INDEXER * NodeIndexer,
  IN  UINT32              NodeCount,
  IN  CM_OBJECT_TOKEN     Token,
  OUT UINT32            * NodeOffset
  )
{
  DEBUG ((
      DEBUG_INFO,
      "IORT: Node Indexer: Search Token = %p\n",
      Token
      ));
  while (NodeCount-- != 0) {
    DEBUG ((
      DEBUG_INFO,
      "IORT: Node Indexer: NodeIndexer->Token = %p, Offset = %d\n",
      NodeIndexer->Token,
      NodeIndexer->Offset
      ));
    if (NodeIndexer->Token == Token) {
      *NodeOffset = NodeIndexer->Offset;
      DEBUG ((
        DEBUG_INFO,
        "IORT: Node Indexer: Token = %p, Found\n",
        Token
        ));
      return EFI_SUCCESS;
    }
    NodeIndexer++;
  }
  DEBUG ((
    DEBUG_INFO,
    "IORT: Node Indexer: Token = %p, Not Found\n",
    Token
    ));
  return EFI_NOT_FOUND;
}

/** Update the Id Mapping Array.

    This function retrieves the Id Mapping Array object referenced by the
    IdMappingToken and updates the IdMapArray.

    @param [in]     This             Pointer to the table Generator.
    @param [in]     CfgMgrProtocol   Pointer to the Configuration Manager
                                     Protocol Interface.
    @param [in]     IdMapArray       Pointer to an array of Id Mappings.
    @param [in]     IdCount          Number of Id Mappings.
    @param [in]     IdMappingToken   Reference Token for retrieving the
                                     Id Mapping Array object.

    @retval EFI_SUCCESS           Table generated successfully.
    @retval EFI_INVALID_PARAMETER A parameter is invalid.
    @retval EFI_NOT_FOUND         The required object was not found.
**/
STATIC
EFI_STATUS
AddIdMappingArray (
  IN      CONST ACPI_TABLE_GENERATOR                   * CONST This,
  IN            EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE     *       IdMapArray,
  IN            UINT32                                         IdCount,
  IN      CONST CM_OBJECT_TOKEN                                IdMappingToken
  )
{
  EFI_STATUS            Status;
  VOID                * IdMappings;
  CM_ARM_ID_MAPPING   * Cursor;
  UINT32                IdMappingCount;
  ACPI_IORT_GENERATOR * Generator;

  ASSERT (IdMapArray != NULL);

  Generator = (ACPI_IORT_GENERATOR*)This;

  // Get the Id Mapping Array
  Status = CfgMgrGetObjects (
    EArmObjIdMappingArray,
    IdMappingToken,
    &IdMappings,
    &IdMappingCount);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (IdMappingCount < IdCount) {
    DEBUG ((
      DEBUG_ERROR,
      "ERROR: IORT: Failed to get the required number of Id Mappings.\n"
      ));
    Status = EFI_NOT_FOUND;
    goto EXIT;
  }

  Cursor = IdMappings;
  // Populate the Id Mapping array
  while (IdCount-- != 0) {
    Status = GetNodeOffsetReferencedByToken (
              Generator->NodeIndexer,
              Generator->IortNodeCount,
              Cursor->OutputReferenceToken,
              &IdMapArray->OutputReference
              );
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "ERROR: IORT: Failed to get Output Reference for ITS Identifier array."
        "Reference Token = %p"
        " Status = %r\n",
        Cursor->OutputReferenceToken,
        Status
        ));
      goto EXIT;
    }

    IdMapArray->InputBase = Cursor->InputBase;
    IdMapArray->NumIds = Cursor->NumIds;
    IdMapArray->OutputBase = Cursor->OutputBase;
    IdMapArray->Flags = Cursor->Flags;

    IdMapArray++;
    Cursor++;
  } // Id Mapping array

EXIT:
  FreePool (IdMappings);
  return Status;
}

/** Update the ITS Group Node Information.

    @param [in]     This             Pointer to the table Generator.
    @param [in]     CfgMgrProtocol   Pointer to the Configuration Manager
                                     Protocol Interface.
    @param [in]     Iort             Pointer to IORT table structure.
    @param [in]     NodesStartOffset Offset for the start of the ITS Group
                                     Nodes.
    @param [in]     NodeList         Pointer to an array of ITS Group Node
                                     Objects.
    @param [in]     NodeCount        Number of ITS Group Node Objects.

    @retval EFI_SUCCESS           Table generated successfully.
    @retval EFI_INVALID_PARAMETER A parameter is invalid.
    @retval EFI_NOT_FOUND         The required object was not found.
**/
STATIC
EFI_STATUS
AddItsGroupNodes (
  IN  CONST ACPI_TABLE_GENERATOR                  * CONST This,
  IN  CONST EFI_ACPI_6_0_IO_REMAPPING_TABLE       *       Iort,
  IN  CONST UINT32                                        NodesStartOffset,
  IN        VOID                                  *       NodeList,
  IN        UINT32                                        NodeCount
  )
{
  EFI_STATUS                            Status;
  EFI_ACPI_6_0_IO_REMAPPING_ITS_NODE  * ItsGroupNode;
  UINT32                              * ItsIds;
  CM_ARM_ITS_IDENTIFIER               * ItsIdentifier;
  UINT32                                ItsIdentifierCount;
  UINT32                                IdIndex;
  UINT64                                NodeLength;
  CM_ARM_ITS_GROUP_NODE                 *Node;

  ASSERT (Iort != NULL);

  ItsGroupNode = (EFI_ACPI_6_0_IO_REMAPPING_ITS_NODE*)((UINT8*)Iort +
                  NodesStartOffset);

  while (NodeCount-- != 0) {
    Node = (CM_ARM_ITS_GROUP_NODE *) NodeList;
    NodeLength = GetItsGroupNodeSize (&NodeList);  // Advances NodeList
    if (NodeLength > MAX_UINT16) {
      Status = EFI_INVALID_PARAMETER;
      DEBUG ((
        DEBUG_ERROR,
        "ERROR: IORT: ITS Id Array Node length 0x%lx > MAX_UINT16."
        " Status = %r\n",
        NodeLength,
        Status
        ));
      return Status;
    }

    // Populate the node header
    ItsGroupNode->Node.Type = EFI_ACPI_IORT_TYPE_ITS_GROUP;
    ItsGroupNode->Node.Length = (UINT16)NodeLength;
    ItsGroupNode->Node.Revision = 0;
    ItsGroupNode->Node.Reserved = EFI_ACPI_RESERVED_DWORD;
    ItsGroupNode->Node.NumIdMappings = 0;
    ItsGroupNode->Node.IdReference = 0;

    // IORT specific data
    ItsGroupNode->NumItsIdentifiers = Node->ItsIdCount;
    ItsIds = (UINT32*)((UINT8*)ItsGroupNode +
      sizeof (EFI_ACPI_6_0_IO_REMAPPING_ITS_NODE));

    Status = CfgMgrGetObjects (
      EArmObjGicItsIdentifierArray,
      Node->ItsIdToken,
      (VOID **)&ItsIdentifier,
      &ItsIdentifierCount);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if (ItsIdentifierCount < ItsGroupNode->NumItsIdentifiers) {
      DEBUG ((
        DEBUG_ERROR,
        "ERROR: IORT: Failed to get the required number of ITS Identifiers.\n"
        ));
      Status = EFI_NOT_FOUND;
      goto EXIT;
    }

    // Populate the ITS identifier array
    for (IdIndex = 0; IdIndex < ItsGroupNode->NumItsIdentifiers; IdIndex++) {
      ItsIds[IdIndex] = ItsIdentifier[IdIndex].ItsId;
    } // ITS identifier array

    // Next IORT Group Node
    ItsGroupNode = (EFI_ACPI_6_0_IO_REMAPPING_ITS_NODE*)((UINT8*)ItsGroupNode +
                    ItsGroupNode->Node.Length);
  } // IORT Group Node

EXIT:
  FreePool (ItsIdentifier);
  return Status;
}

/** Update the Named Component Node Information.

    This function updates the Named Component node information in the IORT
    table.

    @param [in]     This             Pointer to the table Generator.
    @param [in]     Iort             Pointer to IORT table structure.
    @param [in]     NodesStartOffset Offset for the start of the Named
                                     Component Nodes.
    @param [in]     NodeList         Pointer to an array of Named Component
                                     Node Objects.
    @param [in]     NodeCount        Number of Named Component Node Objects.

    @retval EFI_SUCCESS           Table generated successfully.
    @retval EFI_INVALID_PARAMETER A parameter is invalid.
    @retval EFI_NOT_FOUND         The required object was not found.
**/
STATIC
EFI_STATUS
AddNamedComponentNodes (
  IN      CONST ACPI_TABLE_GENERATOR                   * CONST This,
  IN      CONST EFI_ACPI_6_0_IO_REMAPPING_TABLE        *       Iort,
  IN      CONST UINT32                                         NodesStartOffset,
  IN      VOID                                         *       NodeList,
  IN            UINT32                                         NodeCount
  )
{
  EFI_STATUS                                   Status;
  EFI_ACPI_6_0_IO_REMAPPING_NAMED_COMP_NODE  * NcNode;
  EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE         * IdMapArray;
  CHAR8                                      * ObjectName;
  UINTN                                        ObjectNameLength;
  UINT64                                       NodeLength;
  CM_ARM_NAMED_COMPONENT_NODE            *     Node;

  ASSERT (Iort != NULL);

  NcNode = (EFI_ACPI_6_0_IO_REMAPPING_NAMED_COMP_NODE*)((UINT8*)Iort +
            NodesStartOffset);

  while (NodeCount-- != 0) {
    Node = (CM_ARM_NAMED_COMPONENT_NODE*) NodeList;
    NodeLength = GetNamedComponentNodeSize (&NodeList);  // Advances NodeList
    if (NodeLength > MAX_UINT16) {
      Status = EFI_INVALID_PARAMETER;
      DEBUG ((
        DEBUG_ERROR,
        "ERROR: IORT: Named Component Node length 0x%lx > MAX_UINT16."
        " Status = %r\n",
        NodeLength,
        Status
        ));
      return Status;
    }

    // Populate the node header
    NcNode->Node.Type = EFI_ACPI_IORT_TYPE_NAMED_COMP;
    NcNode->Node.Length = (UINT16)NodeLength;
    NcNode->Node.Revision = 2;
    NcNode->Node.Reserved = EFI_ACPI_RESERVED_DWORD;
    NcNode->Node.NumIdMappings = Node->IdMappingCount;

    ObjectNameLength = AsciiStrLen (Node->ObjectName) + 1;
    NcNode->Node.IdReference =
      (UINT32)(sizeof (EFI_ACPI_6_0_IO_REMAPPING_NAMED_COMP_NODE) +
        (ALIGN_VALUE (ObjectNameLength, 4)));

    // Named Component specific data
    NcNode->Flags = Node->Flags;
    NcNode->CacheCoherent = Node->CacheCoherent;
    NcNode->AllocationHints = Node->AllocationHints;
    NcNode->Reserved = EFI_ACPI_RESERVED_WORD;
    NcNode->MemoryAccessFlags = Node->MemoryAccessFlags;
    NcNode->AddressSizeLimit = Node->AddressSizeLimit;

    // Copy the object name
    ObjectName = (CHAR8*)((UINT8*)NcNode +
      sizeof (EFI_ACPI_6_0_IO_REMAPPING_NAMED_COMP_NODE));
    Status = AsciiStrCpyS (
               ObjectName,
               ObjectNameLength,
               Node->ObjectName
               );
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "ERROR: IORT: Failed to copy Object Name. Status = %r\n",
        Status
        ));
      return Status;
    }

    if ((Node->IdMappingCount > 0) &&
        (Node->IdMappingToken != CM_NULL_TOKEN)) {
      // Ids for Named Component
      IdMapArray = (EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE*)((UINT8*)NcNode +
                    NcNode->Node.IdReference);

      Status = AddIdMappingArray (
        This, IdMapArray, Node->IdMappingCount, Node->IdMappingToken);
      if (EFI_ERROR (Status)) {
        DEBUG ((
          DEBUG_ERROR,
          "ERROR: IORT: Failed to add Id Mapping Array. Status = %r\n",
          Status
          ));
        return Status;
      }
    }

    // Next Named Component Node
    NcNode = (EFI_ACPI_6_0_IO_REMAPPING_NAMED_COMP_NODE*)((UINT8*)NcNode +
              NcNode->Node.Length);
  } // Named Component Node

  return EFI_SUCCESS;
}

/** Update the Root Complex Node Information.

    This function updates the Root Complex node information in the IORT table.

    @param [in]     This             Pointer to the table Generator.
    @param [in]     Iort             Pointer to IORT table structure.
    @param [in]     NodesStartOffset Offset for the start of the Root Complex
                                     Nodes.
    @param [in]     NodeList         Pointer to an array of Root Complex Node
                                     Objects.
    @param [in]     NodeCount        Number of Root Complex Node Objects.

    @retval EFI_SUCCESS           Table generated successfully.
    @retval EFI_INVALID_PARAMETER A parameter is invalid.
    @retval EFI_NOT_FOUND         The required object was not found.
**/
STATIC
EFI_STATUS
AddRootComplexNodes (
  IN      CONST ACPI_TABLE_GENERATOR                   * CONST This,
  IN      CONST EFI_ACPI_6_0_IO_REMAPPING_TABLE        *       Iort,
  IN      CONST UINT32                                         NodesStartOffset,
  IN            VOID                                   *       NodeList,
  IN            UINT32                                         NodeCount
  )
{
  EFI_STATUS                           Status;
  EFI_ACPI_6_0_IO_REMAPPING_RC_NODE  * RcNode;
  EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE * IdMapArray;
  UINT64                               NodeLength;
  CM_ARM_ROOT_COMPLEX_NODE           * Node;

  ASSERT (Iort != NULL);

  RcNode = (EFI_ACPI_6_0_IO_REMAPPING_RC_NODE*)((UINT8*)Iort +
            NodesStartOffset);

  while (NodeCount-- != 0) {
    Node = (CM_ARM_ROOT_COMPLEX_NODE *) NodeList;
    NodeLength = GetRootComplexNodeSize (&NodeList);  // Advances NodeList
    if (NodeLength > MAX_UINT16) {
      Status = EFI_INVALID_PARAMETER;
      DEBUG ((
        DEBUG_ERROR,
        "ERROR: IORT: Root Complex Node length 0x%lx > MAX_UINT16."
        " Status = %r\n",
        NodeLength,
        Status
        ));
      return Status;
    }

    // Populate the node header
    RcNode->Node.Type = EFI_ACPI_IORT_TYPE_ROOT_COMPLEX;
    RcNode->Node.Length = (UINT16)NodeLength;
    RcNode->Node.Revision = 1;
    RcNode->Node.Reserved = EFI_ACPI_RESERVED_DWORD;
    RcNode->Node.NumIdMappings = Node->IdMappingCount;
    RcNode->Node.IdReference = sizeof (EFI_ACPI_6_0_IO_REMAPPING_RC_NODE);

    // Root Complex specific data
    RcNode->CacheCoherent = Node->CacheCoherent;
    RcNode->AllocationHints = Node->AllocationHints;
    RcNode->Reserved = EFI_ACPI_RESERVED_WORD;
    RcNode->MemoryAccessFlags = Node->MemoryAccessFlags;
    RcNode->AtsAttribute = Node->AtsAttribute;
    RcNode->PciSegmentNumber = Node->PciSegmentNumber;
    RcNode->MemoryAddressSize = Node->MemoryAddressSize;
    RcNode->Reserved1[0] = EFI_ACPI_RESERVED_BYTE;
    RcNode->Reserved1[1] = EFI_ACPI_RESERVED_BYTE;
    RcNode->Reserved1[2] = EFI_ACPI_RESERVED_BYTE;

    if ((Node->IdMappingCount > 0) &&
        (Node->IdMappingToken != CM_NULL_TOKEN)) {
      // Ids for Root Complex
      IdMapArray = (EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE*)((UINT8*)RcNode +
                    RcNode->Node.IdReference);
      Status = AddIdMappingArray (
        This, IdMapArray, Node->IdMappingCount, Node->IdMappingToken);
      if (EFI_ERROR (Status)) {
        DEBUG ((
          DEBUG_ERROR,
          "ERROR: IORT: Failed to add Id Mapping Array. Status = %r\n",
          Status
          ));
        return Status;
      }
    }

    // Next Root Complex Node
    RcNode = (EFI_ACPI_6_0_IO_REMAPPING_RC_NODE*)((UINT8*)RcNode +
              RcNode->Node.Length);
  } // Root Complex Node

  return EFI_SUCCESS;
}

/** Update the SMMU Interrupt Array.

    This function retrieves the InterruptArray object referenced by the
    InterruptToken and updates the SMMU InterruptArray.

    @param [in, out] InterruptArray   Pointer to an array of Interrupts.
    @param [in]      InterruptCount   Number of entries in the InterruptArray.
    @param [in]      InterruptToken   Reference Token for retrieving the SMMU
                                      InterruptArray object.

    @retval EFI_SUCCESS           Table generated successfully.
    @retval EFI_INVALID_PARAMETER A parameter is invalid.
    @retval EFI_NOT_FOUND         The required object was not found.
**/
STATIC
EFI_STATUS
AddSmmuInterrruptArray (
  IN OUT        EFI_ACPI_6_0_IO_REMAPPING_SMMU_INT    *       InterruptArray,
  IN            UINT32                                        InterruptCount,
  IN      CONST CM_OBJECT_TOKEN                               InterruptToken
  )
{
  EFI_STATUS              Status;
  CM_ARM_SMMU_INTERRUPT * Cursor;
  VOID                  * SmmuInterrupt;
  UINT32                  SmmuInterruptCount;

  ASSERT (InterruptArray != NULL);

  // Get the SMMU Interrupt Array
  Status = CfgMgrGetObjects (
    EArmObjSmmuInterruptArray,
    InterruptToken,
    &SmmuInterrupt,
    &SmmuInterruptCount);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (SmmuInterruptCount < InterruptCount) {
    DEBUG ((
      DEBUG_ERROR,
      "ERROR: IORT: Failed to get the required number of SMMU Interrupts.\n"
      ));
    Status = EFI_NOT_FOUND;
    goto EXIT;
  }

  Cursor = SmmuInterrupt;
  // Populate the Id Mapping array
  while (InterruptCount-- != 0) {
    InterruptArray->Interrupt = Cursor->Interrupt;
    InterruptArray->InterruptFlags = Cursor->Flags;
    InterruptArray++;
    Cursor++;
  } // Id Mapping array

EXIT:
  FreePool (SmmuInterrupt);
  return EFI_SUCCESS;
}

/** Update the SMMU v1/v2 Node Information.

    @param [in]     This             Pointer to the table Generator.
    @param [in]     Iort             Pointer to IORT table structure.
    @param [in]     NodesStartOffset Offset for the start of the SMMU v1/v2
                                     Nodes.
    @param [in]     NodeList         Pointer to an array of SMMU v1/v2 Node
                                     Objects.
    @param [in]     NodeCount        Number of SMMU v1/v2 Node Objects.

    @retval EFI_SUCCESS           Table generated successfully.
    @retval EFI_INVALID_PARAMETER A parameter is invalid.
    @retval EFI_NOT_FOUND         The required object was not found.
**/
STATIC
EFI_STATUS
AddSmmuV1V2Nodes (
  IN      CONST ACPI_TABLE_GENERATOR                  * CONST This,
  IN      CONST EFI_ACPI_6_0_IO_REMAPPING_TABLE       *       Iort,
  IN      CONST UINT32                                        NodesStartOffset,
  IN            VOID                                  *       NodeList,
  IN            UINT32                                        NodeCount
  )
{
  EFI_STATUS                            Status;
  EFI_ACPI_6_0_IO_REMAPPING_SMMU_NODE * SmmuNode;
  EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE  * IdMapArray;

  EFI_ACPI_6_0_IO_REMAPPING_SMMU_INT  * ContextInterruptArray;
  EFI_ACPI_6_0_IO_REMAPPING_SMMU_INT  * PmuInterruptArray;
  UINT64                                NodeLength;
  CM_ARM_SMMUV1_SMMUV2_NODE           * Node;

  ASSERT (Iort != NULL);

  SmmuNode = (EFI_ACPI_6_0_IO_REMAPPING_SMMU_NODE*)((UINT8*)Iort +
              NodesStartOffset);

  while (NodeCount-- != 0) {
    Node = (CM_ARM_SMMUV1_SMMUV2_NODE*) NodeList;
    NodeLength = GetSmmuV1V2NodeSize (&NodeList);  // Advances NodeList
    if (NodeLength > MAX_UINT16) {
      Status = EFI_INVALID_PARAMETER;
      DEBUG ((
        DEBUG_ERROR,
        "ERROR: IORT: SMMU V1/V2 Node length 0x%lx > MAX_UINT16. Status = %r\n",
        NodeLength,
        Status
        ));
      return Status;
    }

    // Populate the node header
    SmmuNode->Node.Type = EFI_ACPI_IORT_TYPE_SMMUv1v2;
    SmmuNode->Node.Length = (UINT16)NodeLength;
    SmmuNode->Node.Revision = 0;
    SmmuNode->Node.Reserved = EFI_ACPI_RESERVED_DWORD;
    SmmuNode->Node.NumIdMappings = Node->IdMappingCount;
    SmmuNode->Node.IdReference = sizeof (EFI_ACPI_6_0_IO_REMAPPING_SMMU_NODE) +
      (Node->ContextInterruptCount *
      sizeof (EFI_ACPI_6_0_IO_REMAPPING_SMMU_INT)) +
      (Node->PmuInterruptCount *
      sizeof (EFI_ACPI_6_0_IO_REMAPPING_SMMU_INT));

    // SMMU v1/v2 specific data
    SmmuNode->Base = Node->BaseAddress;
    SmmuNode->Span = Node->Span;
    SmmuNode->Model = Node->Model;
    SmmuNode->Flags = Node->Flags;

    // Reference to Global Interrupt Array
    SmmuNode->GlobalInterruptArrayRef =
      OFFSET_OF (EFI_ACPI_6_0_IO_REMAPPING_SMMU_NODE, SMMU_NSgIrpt);

    // Context Interrupt
    SmmuNode->NumContextInterrupts = Node->ContextInterruptCount;
    SmmuNode->ContextInterruptArrayRef =
      sizeof (EFI_ACPI_6_0_IO_REMAPPING_SMMU_NODE);
    ContextInterruptArray =
      (EFI_ACPI_6_0_IO_REMAPPING_SMMU_INT*)((UINT8*)SmmuNode +
      sizeof (EFI_ACPI_6_0_IO_REMAPPING_SMMU_NODE));

    // PMU Interrupt
    SmmuNode->NumPmuInterrupts = Node->PmuInterruptCount;
    SmmuNode->PmuInterruptArrayRef = SmmuNode->ContextInterruptArrayRef +
      (Node->ContextInterruptCount *
      sizeof (EFI_ACPI_6_0_IO_REMAPPING_SMMU_INT));
    PmuInterruptArray =
      (EFI_ACPI_6_0_IO_REMAPPING_SMMU_INT*)((UINT8*)SmmuNode +
      SmmuNode->PmuInterruptArrayRef);

    SmmuNode->SMMU_NSgIrpt = Node->SMMU_NSgIrpt;
    SmmuNode->SMMU_NSgIrptFlags = Node->SMMU_NSgIrptFlags;
    SmmuNode->SMMU_NSgCfgIrpt = Node->SMMU_NSgCfgIrpt;
    SmmuNode->SMMU_NSgCfgIrptFlags = Node->SMMU_NSgCfgIrptFlags;

    // Add Context Interrupt Array
    Status = AddSmmuInterrruptArray (
      ContextInterruptArray,
      SmmuNode->NumContextInterrupts,
      Node->ContextInterruptToken);
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "ERROR: IORT: Failed to Context Interrupt Array. Status = %r\n",
        Status
        ));
      return Status;
    }

    // Add PMU Interrupt Array
    if ((SmmuNode->NumPmuInterrupts > 0) &&
        (Node->PmuInterruptToken != CM_NULL_TOKEN)) {
      Status = AddSmmuInterrruptArray (
        PmuInterruptArray,
        SmmuNode->NumPmuInterrupts,
        Node->PmuInterruptToken);
      if (EFI_ERROR (Status)) {
        DEBUG ((
          DEBUG_ERROR,
          "ERROR: IORT: Failed to PMU Interrupt Array. Status = %r\n",
          Status
          ));
        return Status;
      }
    }

    if ((Node->IdMappingCount > 0) &&
        (Node->IdMappingToken != CM_NULL_TOKEN)) {
      // Ids for SMMU v1/v2 Node
      IdMapArray = (EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE*)((UINT8*)SmmuNode +
                    SmmuNode->Node.IdReference);
      Status = AddIdMappingArray (
        This, IdMapArray, Node->IdMappingCount, Node->IdMappingToken);
      if (EFI_ERROR (Status)) {
        DEBUG ((
          DEBUG_ERROR,
          "ERROR: IORT: Failed to add Id Mapping Array. Status = %r\n",
          Status
          ));
        return Status;
      }
    }
    // Next SMMU v1/v2 Node
    SmmuNode = (EFI_ACPI_6_0_IO_REMAPPING_SMMU_NODE*)((UINT8*)SmmuNode +
                SmmuNode->Node.Length);
  } // SMMU v1/v2 Node

  return EFI_SUCCESS;
}

/** Update the SMMUv3 Node Information.

    This function updates the SMMUv3 node information in the IORT table.

    @param [in]     This             Pointer to the table Generator.
    @param [in]     Iort             Pointer to IORT table structure.
    @param [in]     NodesStartOffset Offset for the start of the SMMUv3 Nodes.
    @param [in]     NodeList         Pointer to an array of SMMUv3 Node Objects.
    @param [in]     NodeCount        Number of SMMUv3 Node Objects.

    @retval EFI_SUCCESS           Table generated successfully.
    @retval EFI_INVALID_PARAMETER A parameter is invalid.
    @retval EFI_NOT_FOUND         The required object was not found.
**/
STATIC
EFI_STATUS
AddSmmuV3Nodes (
  IN      CONST ACPI_TABLE_GENERATOR                  * CONST This,
  IN      CONST EFI_ACPI_6_0_IO_REMAPPING_TABLE       *       Iort,
  IN      CONST UINT32                                        NodesStartOffset,
  IN            VOID                                  *       NodeList,
  IN            UINT32                                        NodeCount
  )
{
  EFI_STATUS                             Status;
  EFI_ACPI_6_0_IO_REMAPPING_SMMU3_NODE * SmmuV3Node;
  EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE   * IdMapArray;
  UINT64                                 NodeLength;
  CM_ARM_SMMUV3_NODE                   * Node;

  ASSERT (Iort != NULL);

  SmmuV3Node = (EFI_ACPI_6_0_IO_REMAPPING_SMMU3_NODE*)((UINT8*)Iort +
                NodesStartOffset);

  while (NodeCount-- != 0) {
    Node = (CM_ARM_SMMUV3_NODE*) NodeList;
    NodeLength = GetSmmuV3NodeSize (&NodeList);  // Advances NodeList
    if (NodeLength > MAX_UINT16) {
      Status = EFI_INVALID_PARAMETER;
      DEBUG ((
        DEBUG_ERROR,
        "ERROR: IORT: SMMU V3 Node length 0x%lx > MAX_UINT16. Status = %r\n",
        NodeLength,
        Status
        ));
      return Status;
    }

    // Populate the node header
    SmmuV3Node->Node.Type = EFI_ACPI_IORT_TYPE_SMMUv3;
    SmmuV3Node->Node.Length = (UINT16)NodeLength;
    SmmuV3Node->Node.Revision = 2;
    SmmuV3Node->Node.Reserved = EFI_ACPI_RESERVED_DWORD;
    SmmuV3Node->Node.NumIdMappings = Node->IdMappingCount;
    SmmuV3Node->Node.IdReference =
      sizeof (EFI_ACPI_6_0_IO_REMAPPING_SMMU3_NODE);

    // SMMUv3 specific data
    SmmuV3Node->Base = Node->BaseAddress;
    SmmuV3Node->Flags = Node->Flags;
    SmmuV3Node->Reserved = EFI_ACPI_RESERVED_WORD;
    SmmuV3Node->VatosAddress = Node->VatosAddress;
    SmmuV3Node->Model = Node->Model;
    SmmuV3Node->Event = Node->EventInterrupt;
    SmmuV3Node->Pri = Node->PriInterrupt;
    SmmuV3Node->Gerr = Node->GerrInterrupt;
    SmmuV3Node->Sync = Node->SyncInterrupt;

    if ((SmmuV3Node->Flags & EFI_ACPI_IORT_SMMUv3_FLAG_PROXIMITY_DOMAIN) != 0) {
      // The Proximity Domain Valid flag is set to 1
      SmmuV3Node->ProximityDomain = Node->ProximityDomain;
    } else {
      SmmuV3Node->ProximityDomain = 0;
    }

    if ((SmmuV3Node->Event != 0) && (SmmuV3Node->Pri != 0) &&
        (SmmuV3Node->Gerr != 0) && (SmmuV3Node->Sync != 0)) {
      // If all the SMMU control interrupts are GSIV based,
      // the DeviceID mapping index field is ignored.
      SmmuV3Node->DeviceIdMappingIndex = 0;
    } else {
      SmmuV3Node->DeviceIdMappingIndex = Node->DeviceIdMappingIndex;
    }

    if ((Node->IdMappingCount > 0) &&
        (Node->IdMappingToken != CM_NULL_TOKEN)) {
      // Ids for SMMUv3 node
      IdMapArray = (EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE*)((UINT8*)SmmuV3Node +
                    SmmuV3Node->Node.IdReference);
      Status = AddIdMappingArray (
        This, IdMapArray, Node->IdMappingCount, Node->IdMappingToken);
      if (EFI_ERROR (Status)) {
        DEBUG ((
          DEBUG_ERROR,
          "ERROR: IORT: Failed to add Id Mapping Array. Status = %r\n",
          Status
          ));
        return Status;
      }
    }

    // Next SMMUv3 Node
    SmmuV3Node = (EFI_ACPI_6_0_IO_REMAPPING_SMMU3_NODE*)((UINT8*)SmmuV3Node +
                  SmmuV3Node->Node.Length);
  } // SMMUv3 Node

  return EFI_SUCCESS;
}

/** Update the PMCG Node Information.

    This function updates the PMCG node information in the IORT table.

    @param [in]     This             Pointer to the table Generator.
    @param [in]     Iort             Pointer to IORT table structure.
    @param [in]     NodesStartOffset Offset for the start of the PMCG Nodes.
    @param [in]     NodeList         Pointer to an array of PMCG Node Objects.
    @param [in]     NodeCount        Number of PMCG Node Objects.

    @retval EFI_SUCCESS           Table generated successfully.
    @retval EFI_INVALID_PARAMETER A parameter is invalid.
    @retval EFI_NOT_FOUND         The required object was not found.
**/
STATIC
EFI_STATUS
AddPmcgNodes (
  IN      CONST ACPI_TABLE_GENERATOR                  * CONST This,
  IN      CONST EFI_ACPI_6_0_IO_REMAPPING_TABLE       *       Iort,
  IN      CONST UINT32                                        NodesStartOffset,
  IN            VOID                                  *       NodeList,
  IN            UINT32                                        NodeCount
  )
{
  EFI_STATUS                             Status;
  EFI_ACPI_6_0_IO_REMAPPING_PMCG_NODE  * PmcgNode;
  EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE   * IdMapArray;
  ACPI_IORT_GENERATOR                  * Generator;
  UINT64                                 NodeLength;
  CM_ARM_PMCG_NODE                     * Node;

  ASSERT (Iort != NULL);

  Generator = (ACPI_IORT_GENERATOR*)This;
  PmcgNode = (EFI_ACPI_6_0_IO_REMAPPING_PMCG_NODE*)((UINT8*)Iort +
              NodesStartOffset);

  while (NodeCount-- != 0) {
    Node = (CM_ARM_PMCG_NODE*) NodeList;
    NodeLength = GetPmcgNodeSize (&NodeList);  // Advances NodeList
    if (NodeLength > MAX_UINT16) {
      Status = EFI_INVALID_PARAMETER;
      DEBUG ((
        DEBUG_ERROR,
        "ERROR: IORT: PMCG Node length 0x%lx > MAX_UINT16. Status = %r\n",
        NodeLength,
        Status
        ));
      return Status;
    }

    // Populate the node header
    PmcgNode->Node.Type = EFI_ACPI_IORT_TYPE_PMCG;
    PmcgNode->Node.Length = (UINT16)NodeLength;
    PmcgNode->Node.Revision = 1;
    PmcgNode->Node.Reserved = EFI_ACPI_RESERVED_DWORD;
    PmcgNode->Node.NumIdMappings = Node->IdMappingCount;
    PmcgNode->Node.IdReference = sizeof (EFI_ACPI_6_0_IO_REMAPPING_PMCG_NODE);

    // PMCG specific data
    PmcgNode->Base = Node->BaseAddress;
    PmcgNode->OverflowInterruptGsiv = Node->OverflowInterrupt;
    PmcgNode->Page1Base = Node->Page1BaseAddress;

    Status = GetNodeOffsetReferencedByToken (
              Generator->NodeIndexer,
              Generator->IortNodeCount,
              Node->ReferenceToken,
              &PmcgNode->NodeReference
              );
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "ERROR: IORT: Failed to get Output Reference for PMCG Node."
        "Reference Token = %p"
        " Status = %r\n",
        Node->ReferenceToken,
        Status
        ));
      return Status;
    }

    if ((Node->IdMappingCount > 0) &&
        (Node->IdMappingToken != CM_NULL_TOKEN)) {
      // Ids for PMCG node
      IdMapArray = (EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE*)((UINT8*)PmcgNode +
                    PmcgNode->Node.IdReference);

      Status = AddIdMappingArray (
        This, IdMapArray, Node->IdMappingCount, Node->IdMappingToken);
      if (EFI_ERROR (Status)) {
        DEBUG ((
          DEBUG_ERROR,
          "ERROR: IORT: Failed to add Id Mapping Array. Status = %r\n",
          Status
          ));
        return Status;
      }
    }

    // Next PMCG Node
    PmcgNode = (EFI_ACPI_6_0_IO_REMAPPING_PMCG_NODE*)((UINT8*)PmcgNode +
                PmcgNode->Node.Length);
  } // PMCG Node

  return EFI_SUCCESS;
}

/** Construct the IORT ACPI table.

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
BuildIortTable (
  IN  CONST ACPI_TABLE_GENERATOR                  * CONST This,
  IN  CONST CM_STD_OBJ_ACPI_TABLE_INFO            * CONST AcpiTableInfo,
  IN  CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST CfgMgrProtocol,
  OUT       EFI_ACPI_DESCRIPTION_HEADER          ** CONST Table
  )
{
  EFI_STATUS                             Status;

  UINT64                                 TableSize;
  UINT64                                 NodeSize;

  UINT32                                 IortNodeCount;
  UINT32                                 ItsGroupNodeCount;
  UINT32                                 NamedComponentNodeCount;
  UINT32                                 RootComplexNodeCount;
  UINT32                                 SmmuV1V2NodeCount;
  UINT32                                 SmmuV3NodeCount;
  UINT32                                 PmcgNodeCount;

  UINT32                                 ItsGroupOffset;
  UINT32                                 NamedComponentOffset;
  UINT32                                 RootComplexOffset;
  UINT32                                 SmmuV1V2Offset;
  UINT32                                 SmmuV3Offset;
  UINT32                                 PmcgOffset;

  VOID                                 * NodeList;
  EFI_ACPI_6_0_IO_REMAPPING_TABLE      * Iort;
  IORT_NODE_INDEXER                    * NodeIndexer;
  ACPI_IORT_GENERATOR                  * Generator;

  ASSERT (This != NULL);
  ASSERT (AcpiTableInfo != NULL);
  ASSERT (Table != NULL);
  ASSERT (AcpiTableInfo->TableGeneratorId == This->GeneratorID);
  ASSERT (AcpiTableInfo->AcpiTableSignature == This->AcpiTableSignature);

  if ((AcpiTableInfo->AcpiTableRevision < This->MinAcpiTableRevision) ||
      (AcpiTableInfo->AcpiTableRevision > This->AcpiTableRevision)) {
    DEBUG ((
      DEBUG_ERROR,
      "ERROR: IORT: Requested table revision = %d, is not supported."
      "Supported table revision: Minimum = %d, Maximum = %d\n",
      AcpiTableInfo->AcpiTableRevision,
      This->MinAcpiTableRevision,
      This->AcpiTableRevision
      ));
    return EFI_INVALID_PARAMETER;
  }

  Generator = (ACPI_IORT_GENERATOR*)This;

  // Pointers to allocated memory
  *Table = NULL;

  // Get the ITS group node info
  Status = CfgMgrCountObjects (EArmObjItsGroup, &ItsGroupNodeCount);
  if (EFI_ERROR (Status) && (Status != EFI_NOT_FOUND)) {
    return Status;
  }

  // Add the ITS group node count
  IortNodeCount = ItsGroupNodeCount;

  // Get the Named component node info
  Status = CfgMgrCountObjects (EArmObjNamedComponent, &NamedComponentNodeCount);
  if (EFI_ERROR (Status) && (Status != EFI_NOT_FOUND)) {
    return Status;
  }

  // Add the Named Component group count
  IortNodeCount += NamedComponentNodeCount;

  // Get the Root complex node info
  Status = CfgMgrCountObjects (EArmObjRootComplex, &RootComplexNodeCount);
  if (EFI_ERROR (Status) && (Status != EFI_NOT_FOUND)) {
    return Status;
  }

  // Add the Root Complex node count
  IortNodeCount += RootComplexNodeCount;

  // Get the SMMU v1/v2 node info
  Status = CfgMgrCountObjects (EArmObjSmmuV1SmmuV2, &SmmuV1V2NodeCount);
  if (EFI_ERROR (Status) && (Status != EFI_NOT_FOUND)) {
    return Status;
  }

  // Add the SMMU v1/v2 node count
  IortNodeCount += SmmuV1V2NodeCount;

  // Get the SMMUv3 node info
  Status = CfgMgrCountObjects (EArmObjSmmuV3, &SmmuV3NodeCount);
  if (EFI_ERROR (Status) && (Status != EFI_NOT_FOUND)) {
    return Status;
  }

  // Add the SMMUv3 node count
  IortNodeCount += SmmuV3NodeCount;

  // Get the PMCG node info
  Status = CfgMgrCountObjects (EArmObjPmcg, &PmcgNodeCount);
  if (EFI_ERROR (Status) && (Status != EFI_NOT_FOUND)) {
    return Status;
  }

  // Add the PMCG node count
  IortNodeCount += PmcgNodeCount;

  // Allocate Node Indexer array
  NodeIndexer = AllocateZeroPool ((sizeof (IORT_NODE_INDEXER) * IortNodeCount));
  if (NodeIndexer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  DEBUG ((DEBUG_INFO, "INFO: NodeIndexer = %p\n", NodeIndexer));
  Generator->IortNodeCount = IortNodeCount;
  Generator->NodeIndexer = NodeIndexer;

  // Calculate the size of the IORT table
  TableSize = sizeof (EFI_ACPI_6_0_IO_REMAPPING_TABLE);

  // ITS Group Nodes
  if (ItsGroupNodeCount > 0) {
    ItsGroupOffset = (UINT32)TableSize;

    NodeSize = GetSizeOfNodes (
      EArmObjItsGroup, ItsGroupOffset, &NodeIndexer, GetItsGroupNodeSize);

    if (NodeSize > MAX_UINT32) {
      Status = EFI_INVALID_PARAMETER;
      DEBUG ((
        DEBUG_ERROR,
        "ERROR: IORT: Invalid Size of Group Nodes. Status = %r\n",
        Status
        ));
      goto error_handler;
    }
    TableSize += NodeSize;

    DEBUG ((
      DEBUG_INFO,
      " ItsGroupNodeCount = %d\n" \
      " ItsGroupOffset = %d\n",
      ItsGroupNodeCount,
      ItsGroupOffset
      ));
  }

  // Named Component Nodes
  if (NamedComponentNodeCount > 0) {
    NamedComponentOffset = (UINT32)TableSize;

    NodeSize = GetSizeOfNodes (
      EArmObjNamedComponent,
      NamedComponentOffset,
      &NodeIndexer,
      GetNamedComponentNodeSize);

    if (NodeSize > MAX_UINT32) {
      Status = EFI_INVALID_PARAMETER;
      DEBUG ((
        DEBUG_ERROR,
        "ERROR: IORT: Invalid Size of Named Component Nodes. Status = %r\n",
        Status
        ));
      goto error_handler;
    }
    TableSize += NodeSize;

    DEBUG ((
      DEBUG_INFO,
      " NamedComponentNodeCount = %d\n" \
      " NamedComponentOffset = %d\n",
      NamedComponentNodeCount,
      NamedComponentOffset
      ));
  }

  // Root Complex Nodes
  if (RootComplexNodeCount > 0) {
    RootComplexOffset = (UINT32)TableSize;

    NodeSize = GetSizeOfNodes (
      EArmObjRootComplex,
      RootComplexOffset,
      &NodeIndexer,
      GetRootComplexNodeSize);

    if (NodeSize > MAX_UINT32) {
      Status = EFI_INVALID_PARAMETER;
      DEBUG ((
        DEBUG_ERROR,
        "ERROR: IORT: Invalid Size of Root Complex Nodes. Status = %r\n",
        Status
        ));
      goto error_handler;
    }
    TableSize += NodeSize;

    DEBUG ((
      DEBUG_INFO,
      " RootComplexNodeCount = %d\n" \
      " RootComplexOffset = %d\n",
      RootComplexNodeCount,
      RootComplexOffset
      ));
  }

  // SMMUv1/SMMUv2 Nodes
  if (SmmuV1V2NodeCount > 0) {
    SmmuV1V2Offset = (UINT32)TableSize;

    NodeSize = GetSizeOfNodes (
      EArmObjSmmuV1SmmuV2, SmmuV1V2Offset, &NodeIndexer, GetSmmuV1V2NodeSize);
    if (NodeSize > MAX_UINT32) {
      Status = EFI_INVALID_PARAMETER;
      DEBUG ((
        DEBUG_ERROR,
        "ERROR: IORT: Invalid Size of SMMUv1/v2 Nodes. Status = %r\n",
        Status
        ));
      goto error_handler;
    }
    TableSize += NodeSize;

    DEBUG ((
      DEBUG_INFO,
      " SmmuV1V2NodeCount = %d\n" \
      " SmmuV1V2Offset = %d\n",
      SmmuV1V2NodeCount,
      SmmuV1V2Offset
      ));
  }

  // SMMUv3 Nodes
  if (SmmuV3NodeCount > 0) {
    SmmuV3Offset = (UINT32)TableSize;

    NodeSize = GetSizeOfNodes (
      EArmObjSmmuV3, SmmuV3Offset, &NodeIndexer, GetSmmuV3NodeSize);
    if (NodeSize > MAX_UINT32) {
      Status = EFI_INVALID_PARAMETER;
      DEBUG ((
        DEBUG_ERROR,
        "ERROR: IORT: Invalid Size of SMMUv3 Nodes. Status = %r\n",
        Status
        ));
      goto error_handler;
    }
    TableSize += NodeSize;

    DEBUG ((
      DEBUG_INFO,
      " SmmuV3NodeCount = %d\n" \
      " SmmuV3Offset = %d\n",
      SmmuV3NodeCount,
      SmmuV3Offset
      ));
  }

  // PMCG Nodes
  if (PmcgNodeCount > 0) {
    PmcgOffset = (UINT32)TableSize;

    NodeSize = GetSizeOfNodes (
      EArmObjPmcg, PmcgOffset, &NodeIndexer, GetPmcgNodeSize);
    if (NodeSize > MAX_UINT32) {
      Status = EFI_INVALID_PARAMETER;
      DEBUG ((
        DEBUG_ERROR,
        "ERROR: IORT: Invalid Size of PMCG Nodes. Status = %r\n",
        Status
        ));
      goto error_handler;
    }
    TableSize += NodeSize;

    DEBUG ((
      DEBUG_INFO,
      " PmcgNodeCount = %d\n" \
      " PmcgOffset = %d\n",
      PmcgNodeCount,
      PmcgOffset
      ));
  }

  DEBUG ((
    DEBUG_INFO,
    "INFO: IORT:\n" \
    " IortNodeCount = %d\n" \
    " TableSize = 0x%lx\n",
    IortNodeCount,
    TableSize
    ));

  if (TableSize > MAX_UINT32) {
    Status = EFI_INVALID_PARAMETER;
    DEBUG ((
      DEBUG_ERROR,
      "ERROR: IORT: IORT Table Size 0x%lx > MAX_UINT32," \
      " Status = %r\n",
      TableSize,
      Status
      ));
    goto error_handler;
  }

  // Allocate the Buffer for IORT table
  Iort = AllocateZeroPool (TableSize);
  if (Iort == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto error_handler;
  }

  DEBUG ((
    DEBUG_INFO,
    "IORT: Iort = 0x%p TableSize = 0x%lx\n",
    Iort,
    TableSize
    ));

  Status = AddAcpiHeader (This, &Iort->Header, AcpiTableInfo, (UINT32) TableSize);
  if (EFI_ERROR (Status)) {
    goto error_handler;
  }

  // Update IORT table
  Iort->NumNodes = IortNodeCount;
  Iort->NodeOffset = sizeof (EFI_ACPI_6_0_IO_REMAPPING_TABLE);
  Iort->Reserved = EFI_ACPI_RESERVED_DWORD;

  if (ItsGroupNodeCount > 0) {
    CfgMgrGetSimpleObject(EArmObjItsGroup, &NodeList);
    Status = AddItsGroupNodes (
      This, Iort, ItsGroupOffset, NodeList, ItsGroupNodeCount);
    FreePool (NodeList);
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "ERROR: IORT: Failed to add ITS Group Node. Status = %r\n",
        Status
        ));
      goto error_handler;
    }
  }

  if (NamedComponentNodeCount > 0) {
    CfgMgrGetSimpleObject(EArmObjNamedComponent, &NodeList);
    Status = AddNamedComponentNodes (
      This, Iort, NamedComponentOffset, NodeList, NamedComponentNodeCount);
    FreePool (NodeList);
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "ERROR: IORT: Failed to add Named Component Node. Status = %r\n",
        Status
        ));
      goto error_handler;
    }
  }

  if (RootComplexNodeCount > 0) {
    CfgMgrGetSimpleObject(EArmObjRootComplex, &NodeList);
    Status = AddRootComplexNodes (
      This, Iort, RootComplexOffset, NodeList, RootComplexNodeCount);
    FreePool (NodeList);
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "ERROR: IORT: Failed to add Root Complex Node. Status = %r\n",
        Status
        ));
      goto error_handler;
    }
  }

  if (SmmuV1V2NodeCount > 0) {
    CfgMgrGetSimpleObject(EArmObjSmmuV1SmmuV2, &NodeList);
    Status = AddSmmuV1V2Nodes (
      This, Iort, SmmuV1V2Offset, NodeList, SmmuV1V2NodeCount);
    FreePool (NodeList);
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "ERROR: IORT: Failed to add SMMU v1/v2 Node. Status = %r\n",
        Status
        ));
      goto error_handler;
    }
  }

  if (SmmuV3NodeCount > 0) {
    CfgMgrGetSimpleObject(EArmObjSmmuV3, &NodeList);
    Status = AddSmmuV3Nodes (
      This, Iort, SmmuV3Offset, NodeList, SmmuV3NodeCount);
    FreePool (NodeList);
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "ERROR: IORT: Failed to add SMMUv3 Node. Status = %r\n",
        Status
        ));
      goto error_handler;
    }
  }

  if (PmcgNodeCount > 0) {
    CfgMgrGetSimpleObject(EArmObjPmcg, &NodeList);
    Status = AddPmcgNodes (This, Iort, PmcgOffset, NodeList, PmcgNodeCount);
    FreePool (NodeList);
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "ERROR: IORT: Failed to add SMMUv3 Node. Status = %r\n",
        Status
        ));
      goto error_handler;
    }
  }

  *Table = (EFI_ACPI_DESCRIPTION_HEADER*) Iort;

  return EFI_SUCCESS;

error_handler:
  if (Generator->NodeIndexer != NULL) {
    FreePool (Generator->NodeIndexer);
    Generator->NodeIndexer = NULL;
  }
  if  (Iort != NULL) {
    FreePool (Iort);
  }

  return Status;
}

/** Free any resources allocated for constructing the IORT

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
FreeIortTableResources (
  IN      CONST ACPI_TABLE_GENERATOR                  * CONST This,
  IN      CONST CM_STD_OBJ_ACPI_TABLE_INFO            * CONST AcpiTableInfo,
  IN      CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST CfgMgrProtocol,
  IN OUT        EFI_ACPI_DESCRIPTION_HEADER          ** CONST Table
  )
{
  ACPI_IORT_GENERATOR   * Generator;
  ASSERT (This != NULL);
  ASSERT (AcpiTableInfo != NULL);
  ASSERT (CfgMgrProtocol != NULL);
  ASSERT (AcpiTableInfo->TableGeneratorId == This->GeneratorID);
  ASSERT (AcpiTableInfo->AcpiTableSignature == This->AcpiTableSignature);

  Generator = (ACPI_IORT_GENERATOR*)This;

  // Free any memory allocated by the generator
  if (Generator->NodeIndexer != NULL) {
    FreePool (Generator->NodeIndexer);
    Generator->NodeIndexer = NULL;
  }

  if ((Table == NULL) || (*Table == NULL)) {
    DEBUG ((DEBUG_ERROR, "ERROR: IORT: Invalid Table Pointer\n"));
    ASSERT ((Table != NULL) && (*Table != NULL));
    return EFI_INVALID_PARAMETER;
  }

  FreePool (*Table);
  *Table = NULL;
  return EFI_SUCCESS;
}

/** The IORT Table Generator revision.
*/
#define IORT_GENERATOR_REVISION CREATE_REVISION (1, 0)

/** The interface for the MADT Table Generator.
*/
STATIC
ACPI_IORT_GENERATOR IortGenerator = {
  // ACPI table generator header
  {
    // Generator ID
    CREATE_STD_ACPI_TABLE_GEN_ID (EStdAcpiTableIdIort),
    // Generator Description
    L"ACPI.STD.IORT.GENERATOR",
    // ACPI Table Signature
    EFI_ACPI_6_2_IO_REMAPPING_TABLE_SIGNATURE,
    // ACPI Table Revision supported by this Generator
    EFI_ACPI_IO_REMAPPING_TABLE_REVISION,
    // Minimum supported ACPI Table Revision
    EFI_ACPI_IO_REMAPPING_TABLE_REVISION,
    // Creator ID
    TABLE_GENERATOR_CREATOR_ID_ARM,
    // Creator Revision
    IORT_GENERATOR_REVISION,
    // Build Table function
    BuildIortTable,
    // Free Resource function
    FreeIortTableResources,
    // Extended build function not needed
    NULL,
    // Extended build function not implemented by the generator.
    // Hence extended free resource function is not required.
    NULL
  },

  // IORT Generator private data

  // Iort Node count
  0,
  // Pointer to Iort node indexer
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
AcpiIortLibConstructor (
  IN  EFI_HANDLE           ImageHandle,
  IN  EFI_SYSTEM_TABLE  *  SystemTable
  )
{
  EFI_STATUS  Status;
  Status = RegisterAcpiTableGenerator (&IortGenerator.Header);
  DEBUG ((DEBUG_INFO, "IORT: Register Generator. Status = %r\n", Status));
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
AcpiIortLibDestructor (
  IN  EFI_HANDLE           ImageHandle,
  IN  EFI_SYSTEM_TABLE  *  SystemTable
  )
{
  EFI_STATUS  Status;
  Status = DeregisterAcpiTableGenerator (&IortGenerator.Header);
  DEBUG ((DEBUG_INFO, "Iort: Deregister Generator. Status = %r\n", Status));
  ASSERT_EFI_ERROR (Status);
  return Status;
}
