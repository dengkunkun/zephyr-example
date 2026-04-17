/*
 * SPDX-License-Identifier: Apache-2.0
 */

ASSERT(. <= (__rom_region_start + 0x400),
       "HC32 ICG area overlaps the vector table or ROM start content");
. = __rom_region_start + 0x400;
KEEP(*(.icg_sec))
. = ALIGN(4);
