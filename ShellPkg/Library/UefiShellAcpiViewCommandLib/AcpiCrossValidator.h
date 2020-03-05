/** @file
  Interface for ACPI cross-structure field validator.

  Copyright (c) 2020, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef ACPI_CROSS_VALIDATOR_H_
#define ACPI_CROSS_VALIDATOR_H_

#include <Library/SortLib.h>
#include <Library/BaseLib.h>

/**
  Return byte-size of a struct member.
**/
#define FIELD_SIZE_OF(TYPE, Field) ((UINTN)sizeof(((TYPE *)0)->Field))

/**
  Linked list entry for creating lists to validate
*/
typedef struct {
  LIST_ENTRY Entry;        ///< List entry structure
  VOID *     Buffer;       ///< Points to the copy of the item
  UINTN      Size;         ///< Size of the buffer
  UINT32     Type;         ///< ACPI-defined structure type
  UINT32     Offset;       ///< Offset of item from the start of the table
} ACPI_CROSS_ENTRY;

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
  );

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
  );

/**
  Delete the ACPI cross-structure field validator given.

  @param [in] Validator       Validator to delete.
**/
VOID
EFIAPI
AcpiCrossValidatorDelete (
  IN LIST_ENTRY                   *List
  );


/**
  Map of valid cross-references which can be made between two structure
  types in the same ACPI table.

  The key component of ACPI_VALID_REFS is the IsValid array describing
  whether or not a reference between two structure types is allowed.

  It is accessed in the following way: IsValid[(A * TypeCount) + B], where:
    'A' is the type of structure making the reference
    'B' is the type of structure being referenced
    'TypeCount' is the number of unique values allowed for 'A' and 'B'
**/
typedef struct AcpiValidRefs {
  CONST BOOLEAN*  IsValid;      // Cross-reference validity information
  UINTN           TypeCount;    // Number of unique type values allowed
  CONST CHAR16*   Name;         // Name for the reference being validated
} ACPI_VALID_REFS;

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
  );

#endif // ACPI_CROSS_VALIDATOR
