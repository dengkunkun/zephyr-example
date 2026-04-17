/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_ARM_HDSC_HC32F4A0_PINCTRL_SOC_H_
#define ZEPHYR_SOC_ARM_HDSC_HC32F4A0_PINCTRL_SOC_H_

#include <zephyr/devicetree.h>
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t pinctrl_soc_pin_t;

#define HC32_PIN_PORT_POS              16U
#define HC32_PIN_PIN_POS               8U
#define HC32_PIN_FUNC_POS              0U
#define HC32_PIN_PULL_POS              24U
#define HC32_PIN_OD_POS                26U

#define HC32_PIN_PORT_MASK             0xffU
#define HC32_PIN_PIN_MASK              0xffU
#define HC32_PIN_FUNC_MASK             0x3fU
#define HC32_PIN_PULL_MASK             0x3U
#define HC32_PIN_OD_MASK               0x1U

#define HC32_PULL_NONE                 0U
#define HC32_PULL_UP                   1U
#define HC32_PULL_DOWN                 2U

#define HC32_PORT_GET(pin) \
	(((pin) >> HC32_PIN_PORT_POS) & HC32_PIN_PORT_MASK)
#define HC32_PIN_GET(pin) \
	(((pin) >> HC32_PIN_PIN_POS) & HC32_PIN_PIN_MASK)
#define HC32_FUNC_GET(pin) \
	(((pin) >> HC32_PIN_FUNC_POS) & HC32_PIN_FUNC_MASK)
#define HC32_PULL_GET(pin) \
	(((pin) >> HC32_PIN_PULL_POS) & HC32_PIN_PULL_MASK)
#define HC32_OD_GET(pin) \
	(((pin) >> HC32_PIN_OD_POS) & HC32_PIN_OD_MASK)

#define Z_PINCTRL_STATE_PIN_INIT(node_id, prop, idx)                              \
	(DT_PROP_BY_IDX(node_id, prop, idx) |                                     \
	 ((DT_PROP(node_id, bias_pull_up) ? HC32_PULL_UP :                        \
	   (DT_PROP(node_id, bias_pull_down) ? HC32_PULL_DOWN : HC32_PULL_NONE))  \
	  << HC32_PIN_PULL_POS) |                                                  \
	 ((DT_PROP(node_id, drive_open_drain) ? 1U : 0U) << HC32_PIN_OD_POS)),

#define Z_PINCTRL_STATE_PINS_INIT(node_id, prop)                                  \
	{DT_FOREACH_CHILD_VARGS(DT_PHANDLE(node_id, prop),                       \
				DT_FOREACH_PROP_ELEM, pinmux,                \
				Z_PINCTRL_STATE_PIN_INIT)}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SOC_ARM_HDSC_HC32F4A0_PINCTRL_SOC_H_ */
