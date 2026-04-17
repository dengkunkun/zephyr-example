/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * SoC header for HC32F4A0 (Xiaohua Semiconductor)
 */

#ifndef _HC32F4A0_SOC_H_
#define _HC32F4A0_SOC_H_

#ifndef _ASMLANGUAGE

#include <hc32f4a0.h>
#include <system_hc32f4a0.h>

/* HC32F4A0 CMSIS header uses non-standard IRQ names;
 * Zephyr / CMSIS-6 expects the standard CMSIS names. */
#ifndef SVCall_IRQn
#define SVCall_IRQn             SVC_IRQn
#endif
#ifndef MemoryManagement_IRQn
#define MemoryManagement_IRQn   MemManageFault_IRQn
#endif

#endif /* !_ASMLANGUAGE */

#endif /* _HC32F4A0_SOC_H_ */
