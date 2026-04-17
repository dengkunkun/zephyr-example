/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/pinctrl.h>
#include <soc.h>
#include <hc32_ll_gpio.h>

static int hc32_configure_pin(pinctrl_soc_pin_t pin)
{
	uint8_t port = HC32_PORT_GET(pin);
	uint8_t pin_num = HC32_PIN_GET(pin);
	uint8_t func = HC32_FUNC_GET(pin);
	uint8_t pull = HC32_PULL_GET(pin);
	uint16_t pin_mask = BIT(pin_num);
	stc_gpio_init_t gpio_cfg;

	if (pin_num > 15U) {
		return -EINVAL;
	}

	if (GPIO_StructInit(&gpio_cfg) != LL_OK) {
		return -EIO;
	}

	if (pull == HC32_PULL_UP) {
		gpio_cfg.u16PullUp = PIN_PU_ON;
	} else if (pull == HC32_PULL_DOWN) {
		/* HC32F460/F4A0 hardware does not support internal pull-down. */
		return -ENOTSUP;
	}

	if (HC32_OD_GET(pin) != 0U) {
		gpio_cfg.u16PinOutputType = PIN_OUT_TYPE_NMOS;
	}

	if (GPIO_Init(port, pin_mask, &gpio_cfg) != LL_OK) {
		return -EIO;
	}

	GPIO_SetFunc(port, pin_mask, func & 0x3fU);

	return 0;
}

int pinctrl_configure_pins(const pinctrl_soc_pin_t *pins, uint8_t pin_cnt, uintptr_t reg)
{
	ARG_UNUSED(reg);

	if (pins == NULL || pin_cnt == 0U) {
		return -EINVAL;
	}

	for (uint8_t i = 0U; i < pin_cnt; i++) {
		int ret = hc32_configure_pin(pins[i]);

		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}
