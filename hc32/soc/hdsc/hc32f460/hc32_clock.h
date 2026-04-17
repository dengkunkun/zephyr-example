/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_ARM_HDSC_HC32F460_HC32_CLOCK_H_
#define ZEPHYR_SOC_ARM_HDSC_HC32F460_HC32_CLOCK_H_

#include <stdint.h>

enum hc32_clock_mode {
	HC32_CLOCK_MODE_HIGH_PERFORMANCE = 0,
	HC32_CLOCK_MODE_LOW_SPEED = 1,
};

int hc32_clock_set_mode(enum hc32_clock_mode mode);
enum hc32_clock_mode hc32_clock_get_mode(void);
uint32_t hc32_clock_get_sysclk_hz(void);

#endif /* ZEPHYR_SOC_ARM_HDSC_HC32F460_HC32_CLOCK_H_ */
