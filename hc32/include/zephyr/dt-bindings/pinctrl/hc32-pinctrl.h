/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_HC32_PINCTRL_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_HC32_PINCTRL_H_

#define HC32_PORT_A 0
#define HC32_PORT_B 1
#define HC32_PORT_C 2
#define HC32_PORT_D 3
#define HC32_PORT_E 4
#define HC32_PORT_F 5
#define HC32_PORT_G 6
#define HC32_PORT_H 7
#define HC32_PORT_I 8

#define HC32_PINMUX(port, pin, func) \
	((((port) & 0xffU) << 16) | (((pin) & 0xffU) << 8) | ((func) & 0x3fU))

/* Full function-code table (shared between HC32F460 and HC32F4A0). */
#include <zephyr/dt-bindings/pinctrl/hc32-pinctrl-func.h>

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_HC32_PINCTRL_H_ */
