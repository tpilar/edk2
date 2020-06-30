/** @file

  Copyright (c) 2020, ARM Limited. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef CONFIGURATION_MANAGER_NAMESPACE_H_
#define CONFIGURATION_MANAGER_NAMESPACE_H_

/** The EOBJECT_NAMESPACE_ID enum describes the defined namespaces
    for the Configuration Manager Objects.

 Description of Configuration Manager Object ID
_______________________________________________________________________________
|31 |30 |29 |28 || 27 | 26 | 25 | 24 || 23 | 22 | 21 | 20 || 19 | 18 | 17 | 16|
-------------------------------------------------------------------------------
| Name Space ID ||  0 |  0 |  0 |  0 ||  0 |  0 |  0 |  0 ||  0 |  0 |  0 |  0|
_______________________________________________________________________________

Bits: [31:28] - Name Space ID
                0000 - Standard
                0001 - ARM
                1000 - Custom/OEM
                All other values are reserved.

Bits: [27:16] - Reserved.
_______________________________________________________________________________
|15 |14 |13 |12 || 11 | 10 |  9 |  8 ||  7 |  6 |  5 |  4 ||  3 |  2 |  1 |  0|
-------------------------------------------------------------------------------
| 0 | 0 | 0 | 0 ||  0 |  0 |  0 |  0 ||                 Object ID             |
_______________________________________________________________________________

Bits: [15:8] - Are reserved and must be zero.
Bits: [7:0] - Object ID
*/
typedef enum ObjectNameSpaceID {
  EObjNameSpaceStandard = 0x00000000,      ///< Standard Objects Namespace
  EObjNameSpaceArm      = 0x10000000,      ///< ARM Objects Namespace
  EObjNameSpaceOem      = 0x80000000,      ///< OEM Objects Namespace
} EOBJECT_NAMESPACE_ID;

#endif
