/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_ARM_HDSC_HC32F4A0_HC32_UART_H_
#define ZEPHYR_SOC_ARM_HDSC_HC32F4A0_HC32_UART_H_

#include <stdbool.h>

#include <zephyr/device.h>

int hc32_uart_reconfigure(const struct device *dev);
int hc32_uart_pm_control(const struct device *dev, bool suspended);

#endif /* ZEPHYR_SOC_ARM_HDSC_HC32F4A0_HC32_UART_H_ */
