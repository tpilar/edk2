## @file
#  Dsc include file for Dynamic Tables Framework.
#
#  Copyright (c) 2017 - 2019, ARM Limited. All rights reserved.<BR>
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

[Defines]

[BuildOptions]
  *_*_*_ASL_FLAGS          = -tc -li -so

[LibraryClasses.common]
  TableHelperLib|DynamicTablesPkg/Library/Common/TableHelperLib/TableHelperLib.inf

[Components.common]
  #
  # Dynamic Table Factory Dxe
  #
  DynamicTablesPkg/Drivers/DynamicTableFactoryDxe/DynamicTableFactoryDxe.inf {
    <LibraryClasses>
      NULL|DynamicTablesPkg/Library/Acpi/Arm/AcpiDbg2LibArm/AcpiDbg2LibArm.inf
      NULL|DynamicTablesPkg/Library/Acpi/Arm/AcpiFadtLibArm/AcpiFadtLibArm.inf
      NULL|DynamicTablesPkg/Library/Acpi/Arm/AcpiGtdtLibArm/AcpiGtdtLibArm.inf
      NULL|DynamicTablesPkg/Library/Acpi/Arm/AcpiIortLibArm/AcpiIortLibArm.inf
      NULL|DynamicTablesPkg/Library/Acpi/Arm/AcpiMadtLibArm/AcpiMadtLibArm.inf
      NULL|DynamicTablesPkg/Library/Acpi/Arm/AcpiMcfgLibArm/AcpiMcfgLibArm.inf
      NULL|DynamicTablesPkg/Library/Acpi/Arm/AcpiPpttLibArm/AcpiPpttLibArm.inf
      NULL|DynamicTablesPkg/Library/Acpi/Arm/AcpiRawLibArm/AcpiRawLibArm.inf
      NULL|DynamicTablesPkg/Library/Acpi/Arm/AcpiSpcrLibArm/AcpiSpcrLibArm.inf
      NULL|DynamicTablesPkg/Library/Acpi/Arm/AcpiSratLibArm/AcpiSratLibArm.inf
  }

  #
  # Dynamic Tables Manager Dxe
  #
  DynamicTablesPkg/Drivers/DynamicTableManagerDxe/DynamicTableManagerDxe.inf

  #
  # Dummy Component Libraries
  #
  DynamicTablesPkg/Drivers/ConfigurationManagerDxe/Components/Component1.inf
  DynamicTablesPkg/Drivers/ConfigurationManagerDxe/Components/Component2.inf
  
  #
  # Reference Implementation of Configuration Manager with some dummy libraries
  #
  DynamicTablesPkg/Drivers/ConfigurationManagerDxe/ConfigurationManagerDxe.inf {
    <Defines>
      FILE_GUID = C4A34C80-526D-4DA1-8F87-4335EF13FB8F
    <LibraryClasses>
      NULL|DynamicTablesPkg/Drivers/ConfigurationManagerDxe/Components/Component1.inf
      NULL|DynamicTablesPkg/Drivers/ConfigurationManagerDxe/Components/Component2.inf
  }

