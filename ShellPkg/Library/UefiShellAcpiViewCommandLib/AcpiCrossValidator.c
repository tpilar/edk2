/** @file
  ACPI cross-structure validator.

  Copyright (c) 2020, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent

  A set of methods for validating ACPI table contents where an entire table
  or multiple tables are in scope.

  One example is finding duplicate field values across ACPI table structures
  of the same type.
**/

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiLib.h>
#include "AcpiCrossValidator.h"
#include "AcpiParser.h"

/**
  Add a field value to the ACPI cross-structure field validator.

  @param [in] UniqueList    List head of a linked list with unique entries.
  @param [in] Item          Pointer to field value.
  @param [in] Size          Size of the field value to add.
  @param [in] Type          ACPI-defined structure type for cross reference.
  @param [in] Offset        Offset of the structure from the start of the table.
**/
EFI_STATUS
EFIAPI
AcpiCrossValidatorAdd (
  IN       LIST_ENTRY             *UniqueList,
  IN CONST VOID*                  Item,
  IN       UINTN                  Size,
  IN       UINT32                 Type,
  IN       UINT32                 Offset
  )
{
  ACPI_CROSS_ENTRY *Entry;

  ASSERT (UniqueList != NULL);
  ASSERT (Item != NULL);

  Entry = AllocateZeroPool(sizeof(ACPI_CROSS_ENTRY));
  if (Entry == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Entry->Buffer = AllocateCopyPool(Size, Item);
  if (Entry->Buffer == NULL) {
    FreePool(Entry);
    return EFI_OUT_OF_RESOURCES;
  }

  Entry->Size = Size;
  Entry->Offset = Offset;
  Entry->Type = Type;

  InsertTailList(UniqueList, (LIST_ENTRY*)Entry);

  return EFI_SUCCESS;
}

/**
  Check if all elements in the ACPI cross-structure field validator are unique.

  If consistency checks are enabled, report an error if there are one or more
  duplicate values.

  @param [in] UniqueList        List head with all entries to be checked for uniqueness.
  @param [in] CompareFunction   The function to call to perform the comparison
                                of any two elements.
  @param [in] StructureName     ACPI structure name with the validated field.
  @param [in] FieldName         Name of the validated field.

  @retval TRUE    if all elements are unique.
  @retval FALSE   if there is at least one duplicate element.
**/
BOOLEAN
EFIAPI
AcpiCrossValidatorAllUnique (
  IN       LIST_ENTRY             *UniqueList,
  IN       SORT_COMPARE           CompareFunction,
  IN CONST CHAR8*                 StructureName,
  IN CONST CHAR16*                FieldName
  )
{
  LIST_ENTRY *EntryA;
  LIST_ENTRY *EntryB;
  BOOLEAN AllUnique;
  INTN    CompareResult;

  ASSERT (UniqueList != NULL);
  ASSERT (CompareFunction != NULL);
  ASSERT (StructureName != NULL);
  ASSERT (FieldName != NULL);

  AllUnique = TRUE;
  EntryA = GetFirstNode(UniqueList);

  while (EntryA != UniqueList) {
    EntryB = GetNextNode(UniqueList, EntryA);
    while (EntryB != UniqueList) {
      CompareResult = CompareFunction (EntryA, EntryB);

      if (CompareResult == 0) {
        AllUnique = FALSE;

        AcpiError (
          ACPI_ERROR_CROSS,
          L"ERROR: %a structures %p (table+0x%x) and %p (table+0x%x) have the "
          L"same %s\n",
          StructureName,
          ((ACPI_CROSS_ENTRY*)EntryA)->Offset,
          ((ACPI_CROSS_ENTRY*)EntryB)->Offset,
          FieldName);
      }

      EntryB = GetNextNode (UniqueList, EntryB);
    }

    EntryA = GetNextNode (UniqueList, EntryA);
  }

  return AllUnique;
}

/**
  Delete the ACPI cross-structure field validator given.

  @param [in] Validator       Validator to delete.
**/
VOID
EFIAPI
AcpiCrossValidatorDelete (
  IN LIST_ENTRY*  List
  )
{
  LIST_ENTRY *Entry;
  ACPI_CROSS_ENTRY *AcpiEntry;

  if (List == NULL) {
    return;
  }

  Entry = GetFirstNode(List);
  while (Entry != List) {
    AcpiEntry = (ACPI_CROSS_ENTRY*) Entry;
    if (AcpiEntry->Buffer != NULL) {
      FreePool(AcpiEntry->Buffer);
    }
    RemoveEntryList(Entry);
    ZeroMem(AcpiEntry, sizeof(AcpiEntry));
    FreePool(AcpiEntry);

    Entry = GetFirstNode(List);
  }
}

/**
  Check if the reference made between two structures in an ACPI table
  is allowed.

  If consistency checks are enabled, report an error if the reference is not
  allowed between a structure with the input Type value and a structure
  located at the offset given.

  The offset arguments below are with respect to the starting address of
  the table.

  @param [in] RefList       Cross-reference list tracking all structures
                            in the table which can be referenced.
  @param [in] ValidRefs     Cross-reference types allowed for the table.
  @param [in] FromOffset    Offset of the structure making the reference.
  @param [in] ToOffset      Offset of the structure being referenced.
  @param [in] FromType      Type of the structure making the reference.

  @retval TRUE    if the cross-reference is valid.
  @retval FALSE   if the cross-reference is not valid.
**/
BOOLEAN
EFIAPI
AcpiCrossValidatorRefsValid (
  IN CONST LIST_ENTRY*                RefList,
  IN CONST ACPI_VALID_REFS*           ValidRefs,
  IN       UINT32                     FromOffset,
  IN       UINT32                     ToOffset,
  IN       UINT32                     FromType
  )
{
  LIST_ENTRY *Entry;
  ACPI_CROSS_ENTRY *AcpiEntry;
  BOOLEAN    IsValid;
  UINT32     ToType;

  ASSERT (RefList != NULL);
  ASSERT (ValidRefs != NULL);
  ASSERT (ValidRefs->IsValid != NULL);
  ASSERT (ValidRefs->Name != NULL);

  if (FromType >= ValidRefs->TypeCount) {
    AcpiError (
      ACPI_ERROR_CROSS,
      L"ERROR: Structure of unrecognized type (%d) at offset 0x%x is making "
      L"a '%s' reference\n",
      FromType,
      FromOffset,
      ValidRefs->Name
      );
    return FALSE;
  }


  if (FromOffset == ToOffset) {
    AcpiError (
      ACPI_ERROR_CROSS,
      L"ERROR: Structure at offset 0x%x is making a '%s' reference to itself\n",
      FromOffset,
      ValidRefs->Name
      );
    return FALSE;
  }

  Entry = GetFirstNode(RefList);
  while (Entry != RefList) {

    AcpiEntry = (ACPI_CROSS_ENTRY*) Entry;
    // A referenced structure with a given offset from the start of the
    // table exists.
    if (AcpiEntry->Offset == ToOffset) {
      ToType = AcpiEntry->Type;

      // Check if the reference between the two structures is allowed given
      // their types.
      IsValid = (ToType < ValidRefs->TypeCount) &&
                ValidRefs->IsValid[(FromType * ValidRefs->TypeCount) + ToType];

      if (!IsValid) {
        AcpiError (
          ACPI_ERROR_CROSS,
          L"ERROR: Structure at offset 0x%x is making a '%s' reference to "
          L"another structure at offset 0x%x which is not allowed for "
          L"the two structure types (%d and %d)\n",
          FromOffset,
          ValidRefs->Name,
          ToOffset,
          FromType,
          ToType
          );
      }

      return IsValid;
    }
  }

  AcpiError (
    ACPI_ERROR_CROSS,
    L"ERROR: Structure at offset 0x%x is making a '%s' reference to another "
    L"structure at offset 0x%x which does not exist\n",
    FromOffset,
    ValidRefs->Name,
    ToOffset
    );

  return FALSE;
}
