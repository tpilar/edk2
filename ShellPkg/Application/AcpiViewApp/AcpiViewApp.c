/** @file
  Main file for AcpiViewApp application

  Copyright (c) 2020, ARM Limited. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/BaseLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Protocol/ShellParameters.h>

/**
  Execute the AcpiView command from UefiShellAcpiViewCommandLib.
  This function is pulled in directly from the library source.
**/
SHELL_STATUS
EFIAPI
ShellCommandRunAcpiView (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE* SystemTable
  );

CHAR16 mAcpiViewAppHelp[] =
  u"\r\n"
  "Display ACPI Table information.\r\n"
  "\r\n"
  "AcpiViewApp.efi [[-?] | [[-l] | [-s AcpiTable [-d]]] [-q] [-h]]\r\n"
  " \r\n"
  "  -l - Display list of installed ACPI Tables.\r\n"
  "  -s - Display only the specified AcpiTable type and only support single\r\n"
  "       invocation option.\r\n"
  "         AcpiTable    : The required ACPI Table type.\r\n"
  "  -d - Generate a binary file dump of the specified AcpiTable.\r\n"
  "  -q - Quiet. Suppress errors and warnings. Disables consistency checks.\r\n"
  "  -h - Enable colour highlighting.\r\n"
  "  -? - Show help.\r\n"
  " \r\n"
  "  This program is provided to allow examination of ACPI table values from "
  "the\r\n"
  "  UEFI Shell. This can help with investigations, especially at that "
  "stage\r\n"
  "  where the tables are not enabling an OS to boot.\r\n"
  "  The program is not exhaustive, and only encapsulates detailed knowledge "
  "of a\r\n"
  "  limited number of table types.\r\n"
  " \r\n"
  "  Default behaviour is to display the content of all tables installed.\r\n"
  "  'Known' table types (listed in NOTES below) will be parsed and "
  "displayed\r\n"
  "  with descriptions and field values. Where appropriate a degree of\r\n"
  "  consistency checking is done and errors may be reported in the output.\r\n"
  "  Other table types will be displayed as an array of Hexadecimal bytes.\r\n"
  " \r\n"
  "  To facilitate debugging, the -s and -d options can be used to generate "
  "a\r\n"
  "  binary file image of a table that can be copied elsewhere for "
  "investigation\r\n"
  "  using tools such as those provided by acpica.org. This is especially\r\n"
  "  relevant for AML type tables like DSDT and SSDT.\r\n"
  " \r\n"
  "NOTES:\r\n"
  "  1. The AcpiTable parameter can match any installed table type.\r\n"
  "     Tables without specific handling will be displayed as a raw hex dump "
  "(or\r\n"
  "     dumped to a file if -d is used).\r\n"
  "  2. -s option supports to display the specified AcpiTable type that is "
  "present\r\n"
  "     in the system. For normal type AcpiTable, it would display the data of "
  "the\r\n"
  "     AcpiTable and AcpiTable header. The following type may contain header "
  "type\r\n"
  "     other than AcpiTable header. The actual header can refer to the ACPI "
  "spec\r\n"
  "     6.3\r\n"
  "     Extra A. Particular types:\r\n"
  "       APIC  - Multiple APIC Description Table (MADT)\r\n"
  "       BGRT  - Boot Graphics Resource Table\r\n"
  "       DBG2  - Debug Port Table 2\r\n"
  "       DSDT  - Differentiated System Description Table\r\n"
  "       FACP  - Fixed ACPI Description Table (FADT)\r\n"
  "       GTDT  - Generic Timer Description Table\r\n"
  "       IORT  - IO Remapping Table\r\n"
  "       MCFG  - Memory Mapped Config Space Base Address Description Table\r\n"
  "       PPTT  - Processor Properties Topology Table\r\n"
  "       RSDP  - Root System Description Pointer\r\n"
  "       SLIT  - System Locality Information Table\r\n"
  "       SPCR  - Serial Port Console Redirection Table\r\n"
  "       SRAT  - System Resource Affinity Table\r\n"
  "       SSDT  - Secondary SystemDescription Table\r\n"
  "       XSDT  - Extended System Description Table\r\n"
  " \r\n"
  "  Table details correspond to those in 'Advanced Configuration and Power\r\n"
  "  Interface Specification' Version 6.3 [January 2019]\r\n"
  "  (https://uefi.org/specifications)\r\n"
  "  "
  " \r\n"
  "  NOTE: The nature of the ACPI standard means that almost all tables in "
  "6.1\r\n"
  "        will be 'backwards compatible' with prior version of the "
  "specification\r\n"
  "        in terms of structure, so formatted output should be correct. The "
  "main\r\n"
  "        exception will be that previously 'reserved' fields will be "
  "reported\r\n"
  "        with new names, where they have been added in later versions of "
  "the\r\n"
  "        specification.\r\n"
  " \r\n"
  " \r\n"
  "EXAMPLES:\r\n"
  "  * To display a list of the installed table types:\r\n"
  "    fs0:\\> AcpiViewApp.efi -l\r\n"
  " \r\n"
  "  * To parse and display a specific table type:\r\n"
  "    fs0:\\> AcpiViewApp.efi -s GTDT\r\n"
  " \r\n"
  "  * To save a binary dump of the contents of a table to a file\r\n"
  "    in the current working directory:\r\n"
  "    fs0:\\> AcpiViewApp.efi -s DSDT -d\r\n"
  " \r\n"
  "  * To display contents of all ACPI tables:\r\n"
  "    fs0:\\> AcpiViewApp.efi\r\n"
  " \r\n";

/**
  Determine if the user wants to display by checking for presence
  of '/?' or '--help' on command line. We cannot override '-?' shell
  command line handling.

  @retval EFI_SUCCESS             No help was printed
  @retval EFI_INVALID_PARAMETER   Help was printed
**/
STATIC
EFI_STATUS
CheckForHelpRequest (
  EFI_HANDLE ImageHandle
  )
{
  EFI_STATUS                    Status;
  EFI_SHELL_PARAMETERS_PROTOCOL *ShellParameters;
  UINTN                         Index;

  Status = gBS->HandleProtocol (
    ImageHandle, 
    &gEfiShellParametersProtocolGuid,
    (VOID **)&ShellParameters
    );
  if (EFI_ERROR(Status)) {
    return Status;
  }

  for (Index = 1; Index < ShellParameters->Argc; Index++) {
    if ((StrCmp (ShellParameters->Argv[Index], L"/?") == 0) ||
        (StrCmp (ShellParameters->Argv[Index], L"--help") == 0))  {
      Print (mAcpiViewAppHelp);
      return EFI_INVALID_PARAMETER;
    }
  }

  return EFI_SUCCESS;
}


/**
  Application Entry Point wrapper around the shell command

  @param[in] ImageHandle  Handle to the Image (NULL if internal).
  @param[in] SystemTable  Pointer to the System Table (NULL if internal).
**/
EFI_STATUS
EFIAPI
AcpiViewAppMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS Status;

  Status = CheckForHelpRequest (ImageHandle);

  // Do not run code if help was printed
  if (Status != EFI_INVALID_PARAMETER) {
    Status = ShellCommandRunAcpiView (gImageHandle, SystemTable);
  }

  return Status;
}
