/** @file
  ACPI parser

  Copyright (c) 2016 - 2020, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include "AcpiParser.h"
#include "AcpiView.h"
#include "AcpiViewConfig.h"
#include "AcpiParser.h"
#include "AcpiViewLog.h"

STATIC ACPI_DESCRIPTION_HEADER_INFO AcpiHdrInfo;

/**
  An ACPI_PARSER array describing the ACPI header.
**/
STATIC CONST ACPI_PARSER AcpiHeaderParser[] = {
  PARSE_ACPI_HEADER (&AcpiHdrInfo)
};

/**
  This function verifies the ACPI table checksum.

  This function verifies the checksum for the ACPI table and optionally
  prints the status.

  @param [in] Log     If TRUE log the status of the checksum.
  @param [in] Ptr     Pointer to the start of the table buffer.
  @param [in] Length  The length of the buffer.

  @retval TRUE        The checksum is OK.
  @retval FALSE       The checksum failed.
**/
BOOLEAN
EFIAPI
VerifyChecksum (
  IN BOOLEAN Log,
  IN UINT8*  Ptr,
  IN UINT32  Length
  )
{
  UINTN ByteCount;
  UINT8 Checksum;

  ByteCount = 0;
  Checksum = 0;

  while (ByteCount < Length) {
    Checksum += *(Ptr++);
    ByteCount++;
  }

  if (Log) {
    if (Checksum == 0) {
      AcpiLog (ACPI_GOOD, L"Table Checksum : OK\n");
    } else {
      AcpiError (ACPI_ERROR_CSUM, L"Table Checksum (0x%X != 0)\n", Checksum);
    }
  }

  return (Checksum == 0);
}

/**
  This function performs a raw data dump of the ACPI table.

  @param [in] Ptr     Pointer to the start of the table buffer.
  @param [in] Length  The length of the buffer.
**/
VOID
EFIAPI
DumpRaw (
  IN UINT8* Ptr,
  IN UINT32 Length
  )
{
  UINTN ByteCount;
  CHAR8 AsciiBuffer[17];
  CHAR8 HexBuffer[128];
  CHAR8 *HexCursor;

  ByteCount = 0;

  AcpiInfo (L"Address  : 0x%p", Ptr);
  AcpiInfo (L"Length   : %d\n", Length);

  while (ByteCount < Length) {

    // Reset ascii and hex strings
    if (ByteCount % 16 == 0) {
      HexCursor = HexBuffer;
      ZeroMem (AsciiBuffer, sizeof(AsciiBuffer));
      ZeroMem (HexBuffer, sizeof(HexBuffer));
    } else if (ByteCount % 8 == 0) {
      HexCursor += AsciiSPrint (HexCursor, sizeof(HexBuffer), "- ");
    }

    // Add hex couplet to hex buffer
    HexCursor += AsciiSPrint (HexCursor, sizeof(HexBuffer), "%02X ", *Ptr);

    // Add ascii letter to the ascii buffer
    AsciiBuffer[ByteCount & 0xF] = '.';
    if ((*Ptr >= ' ') && (*Ptr < 0x7F)) {
      AsciiBuffer[ByteCount & 0xF] = *Ptr;
    }

    // Print line with fixed width hex part
    if (ByteCount % 16 == 15) {
      AcpiInfo (L"%08X : %-.*a %a", ByteCount + 1, 46, HexBuffer, AsciiBuffer);
    }

    ByteCount++;
    Ptr++;
  }

    // Print the last line
    if (ByteCount % 16 != 15) {
      AcpiInfo (
        L"%08X : %-*a %.*a",
        (ByteCount + 16) & ~0xF,
        46,
        HexBuffer,
        ByteCount & 0xF,
        AsciiBuffer);
    }
}

/**
  Prints an arbitrary variable to screen using a given parser.
  Also calls the internal validator if it exists.

  @param[in] Parser The parser to use to print to screen
  @param[in] Prt    Pointer to variable that should be printed
**/
STATIC
VOID
EFIAPI
DumpAndValidate(
  IN CONST ACPI_PARSER* Parser,
  IN VOID* Ptr
  )
{
  // if there is a Formatter function let the function handle
  // the printing else if a Format is specified in the table use
  // the Format for printing
  PrintFieldName (2, Parser->NameStr);
  if (Parser->PrintFormatter != NULL) {
    Parser->PrintFormatter(Parser->Format, Ptr);
  } else if (Parser->Format != NULL) {
    switch (Parser->Length) {
    case 1:
      AcpiInfo (Parser->Format, *(UINT8 *)Ptr);
      break;
    case 2:
      AcpiInfo (Parser->Format, ReadUnaligned16 ((CONST UINT16 *)Ptr));
      break;
    case 4:
      AcpiInfo (Parser->Format, ReadUnaligned32 ((CONST UINT32 *)Ptr));
      break;
    case 8:
      AcpiInfo (Parser->Format, ReadUnaligned64 ((CONST UINT64 *)Ptr));
      break;
    default:
      AcpiLog (ACPI_BAD, L"<Parse Error>");
    } // switch
  }

  // Validating only makes sense if we are tracing
  // the parsed table entries, to report by table name.
  if (mConfig.ConsistencyCheck && (Parser->FieldValidator != NULL)) {
    Parser->FieldValidator(Ptr, Parser->Context);
  }
}

/**
  Set all ACPI structure instance counts to 0.

  @param [in,out] StructDb     ACPI structure database with counts to reset.
**/
VOID
EFIAPI
ResetAcpiStructCounts (
  IN OUT ACPI_STRUCT_DATABASE* StructDb
  )
{
  UINT32 Type;

  ASSERT (StructDb != NULL);
  ASSERT (StructDb->Entries != NULL);

  for (Type = 0; Type < StructDb->EntryCount; Type++) {
    StructDb->Entries[Type].Count = 0;
  }
}

/**
  Sum all ACPI structure instance counts.

  @param [in] StructDb     ACPI structure database with per-type counts to sum.

  @return   Total number of structure instances recorded in the database.
**/
UINT32
EFIAPI
SumAcpiStructCounts (
  IN  CONST ACPI_STRUCT_DATABASE* StructDb
  )
{
  UINT32 Type;
  UINT32 Total;

  ASSERT (StructDb != NULL);
  ASSERT (StructDb->Entries != NULL);

  Total = 0;

  for (Type = 0; Type < StructDb->EntryCount; Type++) {
    Total += StructDb->Entries[Type].Count;
  }

  return Total;
}

/**
  Validate that a structure with a given type value is defined for the given
  ACPI table and target architecture.

  The target architecture is evaluated from the firmare build parameters.

  @param [in] Type        ACPI-defined structure type.
  @param [in] StructDb    ACPI structure database with architecture
                          compatibility info.

  @retval TRUE    Structure is valid.
  @retval FALSE   Structure is not valid.
**/
BOOLEAN
EFIAPI
IsAcpiStructTypeValid (
  IN        UINT32                Type,
  IN  CONST ACPI_STRUCT_DATABASE* StructDb
  )
{
  UINT32 Compatible;

  ASSERT (StructDb != NULL);
  ASSERT (StructDb->Entries != NULL);

  if (Type >= StructDb->EntryCount) {
    return FALSE;
  }

#if defined (MDE_CPU_ARM) || defined (MDE_CPU_AARCH64)
  Compatible = StructDb->Entries[Type].CompatArch &
               (ARCH_COMPAT_ARM | ARCH_COMPAT_AARCH64);
#else
  Compatible = StructDb->Entries[Type].CompatArch;
#endif

  return (Compatible != 0);
}

/**
  Print the instance count of each structure in an ACPI table that is
  compatible with the target architecture.

  For structures which are not allowed for the target architecture,
  validate that their instance counts are 0.

  @param [in] StructDb     ACPI structure database with counts to validate.

  @retval TRUE    All structures are compatible.
  @retval FALSE   One or more incompatible structures present.
**/
BOOLEAN
EFIAPI
ValidateAcpiStructCounts (
  IN  CONST ACPI_STRUCT_DATABASE* StructDb
  )
{
  BOOLEAN   AllValid;
  UINT32    Type;

  ASSERT (StructDb != NULL);
  ASSERT (StructDb->Entries != NULL);

  AllValid = TRUE;
  AcpiInfo (L"\nTable Breakdown:");

  for (Type = 0; Type < StructDb->EntryCount; Type++) {
    ASSERT (Type == StructDb->Entries[Type].Type);

    if (IsAcpiStructTypeValid (Type, StructDb)) {
      AcpiInfo (
        L"  %-*a : %d",
        OUTPUT_FIELD_COLUMN_WIDTH - 2,
        StructDb->Entries[Type].Name,
        StructDb->Entries[Type].Count);
    } else if (StructDb->Entries[Type].Count > 0) {
      AllValid = FALSE;
      AcpiError (ACPI_ERROR_VALUE,
        L"%a Structure is not valid for the target architecture (found %d)",
        StructDb->Entries[Type].Name,
        StructDb->Entries[Type].Count);
    }
  }

  return AllValid;
}

/**
  Parse the ACPI structure with the type value given according to instructions
  defined in the ACPI structure database.

  If the input structure type is defined in the database, increment structure's
  instance count.

  If ACPI_PARSER array is used to parse the input structure, the index of the
  structure (instance count for the type before update) gets printed alongside
  the structure name. This helps debugging if there are many instances of the
  type in a table. For ACPI_STRUCT_PARSER_FUNC, the printing of the index must
  be implemented separately.

  @param [in]     Indent    Number of spaces to indent the output.
  @param [in]     Ptr       Ptr to the start of the structure.
  @param [in,out] StructDb  ACPI structure database with instructions on how
                            parse every structure type.
  @param [in]     Offset    Structure offset from the start of the table.
  @param [in]     Type      ACPI-defined structure type.
  @param [in]     Length    Length of the structure in bytes.

  @retval TRUE    ACPI structure parsed successfully.
  @retval FALSE   Undefined structure type or insufficient data to parse.
**/
BOOLEAN
EFIAPI
ParseAcpiStruct (
  IN            UINT32                 Indent,
  IN            UINT8*                 Ptr,
  IN OUT        ACPI_STRUCT_DATABASE*  StructDb,
  IN            UINT32                 Offset,
  IN            UINT32                 Type,
  IN            UINT32                 Length
  )
{
  ASSERT (Ptr != NULL);
  ASSERT (StructDb != NULL);
  ASSERT (StructDb->Entries != NULL);
  ASSERT (StructDb->Name != NULL);

  if (Type >= StructDb->EntryCount) {
    AcpiError (
      ACPI_ERROR_VALUE, L"Unknown %a. Type = %d", StructDb->Name, Type);
    return FALSE;
  }

  AcpiLog (
    ACPI_ITEM,
    L"%*.a%a[%d] (+0x%x)",
    Indent,
    "",
    StructDb->Entries[Type].Name,
    StructDb->Entries[Type].Count,
    Offset
    );

  StructDb->Entries[Type].Count++;

  if (StructDb->Entries[Type].Handler.ParserFunc != NULL) {
    StructDb->Entries[Type].Handler.ParserFunc (Ptr, Length);
  } else if (StructDb->Entries[Type].Handler.ParserArray != NULL) {
    ASSERT (StructDb->Entries[Type].Handler.Elements != 0);
    ParseAcpi (
      TRUE,
      Indent + gIndent,
      NULL,
      Ptr,
      Length,
      StructDb->Entries[Type].Handler.ParserArray,
      StructDb->Entries[Type].Handler.Elements
      );
  } else {
    AcpiFatal (
      L"Parsing of %a Structure is not implemented",
      StructDb->Entries[Type].Name);
    return FALSE;
  }

  return TRUE;
}

/**
  This function is used to parse an ACPI table buffer.

  The ACPI table buffer is parsed using the ACPI table parser information
  specified by a pointer to an array of ACPI_PARSER elements. This parser
  function iterates through each item on the ACPI_PARSER array and logs the
  ACPI table fields.

  This function can optionally be used to parse ACPI tables and fetch specific
  field values. The ItemPtr member of the ACPI_PARSER structure (where used)
  is updated by this parser function to point to the selected field data
  (e.g. useful for variable length nested fields).

  @param [in] Trace        Trace the ACPI fields TRUE else only parse the
                           table.
  @param [in] Indent       Number of spaces to indent the output.
  @param [in] AsciiName    Optional pointer to an ASCII string that describes
                           the table being parsed.
  @param [in] Ptr          Pointer to the start of the buffer.
  @param [in] Length       Length of the buffer pointed by Ptr.
  @param [in] Parser       Pointer to an array of ACPI_PARSER structure that
                           describes the table being parsed.
  @param [in] ParserItems  Number of items in the ACPI_PARSER array.

  @retval Number of bytes parsed.
**/
UINT32
EFIAPI
ParseAcpi (
  IN BOOLEAN            Trace,
  IN UINT32             Indent,
  IN CONST CHAR8*       AsciiName OPTIONAL,
  IN UINT8*             Ptr,
  IN UINT32             Length,
  IN CONST ACPI_PARSER* Parser,
  IN UINT32             ParserItems
)
{
  UINT32  Index;
  UINT32  Offset;

  if (Length == 0) {
    AcpiLog (
      ACPI_WARN,
      L"Will not parse zero-length buffer <%a>=%p",
      AsciiName ? AsciiName : "Unknown Item",
      Ptr);
    return 0;
  }

  // Increment the Indent
  gIndent += Indent;

  if (Trace && (AsciiName != NULL)){
    AcpiLog (
      ACPI_ITEM, L"%*.a%a", gIndent, "", AsciiName);
  }

  Offset = 0;
  for (Index = 0; Index < ParserItems; Index++) {
    if ((Offset + Parser[Index].Length) > Length) {

      // For fields outside the buffer length provided, reset any pointers
      // which were supposed to be updated by this function call
      if (Parser[Index].ItemPtr != NULL) {
        *Parser[Index].ItemPtr = NULL;
      }

      // We don't parse past the end of the max length specified
      continue;
    }

    if (mConfig.ConsistencyCheck && (Offset != Parser[Index].Offset)) {
      AcpiError (ACPI_ERROR_PARSE,
        L"%a: Offset Mismatch for %s (%d != %d)",
        AsciiName,
        Parser[Index].NameStr,
        Offset,
        Parser[Index].Offset
        );
    }

    if (Trace) {
      DumpAndValidate (&Parser[Index], &Ptr[Offset]);
    }

    if (Parser[Index].ItemPtr != NULL) {
      *Parser[Index].ItemPtr = Ptr + Offset;
    }

    Offset += Parser[Index].Length;
  } // for

  // Decrement the Indent
  gIndent -= Indent;
  return Offset;
}

/**
  An array describing the ACPI Generic Address Structure.
  The GasParser array is used by the ParseAcpi function to parse and/or trace
  the GAS structure.
**/
STATIC CONST ACPI_PARSER GasParser[] = {
  {L"Address Space ID", 1, 0, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Register Bit Width", 1, 1, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Register Bit Offset", 1, 2, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Address Size", 1, 3, L"0x%x", NULL, NULL, NULL, NULL},
  {L"Address", 8, 4, L"0x%lx", NULL, NULL, NULL, NULL}
};

/**
  This function indents and traces the GAS structure as described by the GasParser.

  @param [in] Ptr     Pointer to the start of the buffer.
  @param [in] Indent  Number of spaces to indent the output.
  @param [in] Length  Length of the GAS structure buffer.

  @retval Number of bytes parsed.
**/
UINT32
EFIAPI
DumpGasStruct (
  IN UINT8*        Ptr,
  IN UINT32        Indent,
  IN UINT32        Length
  )
{
  AcpiInfo(L"");
  return ParseAcpi (
           TRUE,
           Indent,
           NULL,
           Ptr,
           Length,
           PARSER_PARAMS (GasParser)
           );
}

/**
  This function traces the GAS structure as described by the GasParser.

  @param [in] Format  Optional format string for tracing the data.
  @param [in] Ptr     Pointer to the start of the buffer.
**/
VOID
EFIAPI
DumpGas (
  IN CONST CHAR16* Format OPTIONAL,
  IN UINT8*        Ptr
  )
{
  DumpGasStruct (Ptr, 2, sizeof (EFI_ACPI_6_3_GENERIC_ADDRESS_STRUCTURE));
}

/**
  This function traces the ACPI header as described by the AcpiHeaderParser.

  @param [in] Ptr          Pointer to the start of the buffer.

  @retval Number of bytes parsed.
**/
UINT32
EFIAPI
DumpAcpiHeader (
  IN UINT8* Ptr
  )
{
  return ParseAcpi (
           TRUE,
           0,
           "ACPI Table Header",
           Ptr,
           sizeof (EFI_ACPI_DESCRIPTION_HEADER),
           PARSER_PARAMS (AcpiHeaderParser)
           );
}

/**
  This function parses the ACPI header as described by the AcpiHeaderParser.

  This function optionally returns the signature, length and revision of the
  ACPI table.

  @param [in]  Ptr        Pointer to the start of the buffer.
  @param [out] Signature  Gets location of the ACPI table signature.
  @param [out] Length     Gets location of the length of the ACPI table.
  @param [out] Revision   Gets location of the revision of the ACPI table.

  @retval Number of bytes parsed.
**/
UINT32
EFIAPI
ParseAcpiHeader (
  IN  VOID*         Ptr,
  OUT CONST UINT32** Signature,
  OUT CONST UINT32** Length,
  OUT CONST UINT8**  Revision
  )
{
  UINT32                        BytesParsed;

  BytesParsed = ParseAcpi (
                  FALSE,
                  0,
                  NULL,
                  Ptr,
                  sizeof (EFI_ACPI_DESCRIPTION_HEADER),
                  PARSER_PARAMS (AcpiHeaderParser)
                  );

  *Signature = AcpiHdrInfo.Signature;
  *Length = AcpiHdrInfo.Length;
  *Revision = AcpiHdrInfo.Revision;

  return BytesParsed;
}
