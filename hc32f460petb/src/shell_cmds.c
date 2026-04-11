/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shell demo commands for HC32F460PETB.
 *
 * Commands:
 *   led on <n>      — turn on LED n (0 or 1)
 *   led off <n>     — turn off LED n
 *   led toggle <n>  — toggle LED n
 *   led status      — show LED states
 *   gpio read <port> <pin> — read GPIO pin value
 *   sysinfo         — show system information
 */

#include <zephyr/shell/shell.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/version.h>
#include <stdlib.h>
#include <soc.h>

/* GPIO base for direct register reads */
#define GPIO_BASE  0x40053800UL
#define GPIO_PIDR(port) (GPIO_BASE + 0x0000U + (uint32_t)(port) * 0x10U)

static const struct gpio_dt_spec leds[] = {
	GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios),
};

static int parse_led_index(const struct shell *sh, const char *arg)
{
	int n = atoi(arg);

	if (n < 0 || n >= (int)ARRAY_SIZE(leds)) {
		shell_error(sh, "Invalid LED index %d (valid: 0-%d)",
			    n, (int)ARRAY_SIZE(leds) - 1);
		return -EINVAL;
	}
	return n;
}

static int cmd_led_on(const struct shell *sh, size_t argc, char **argv)
{
	int n = parse_led_index(sh, argv[1]);

	if (n < 0) {
		return n;
	}
	gpio_pin_set_dt(&leds[n], 1);
	shell_print(sh, "LED%d ON", n);
	return 0;
}

static int cmd_led_off(const struct shell *sh, size_t argc, char **argv)
{
	int n = parse_led_index(sh, argv[1]);

	if (n < 0) {
		return n;
	}
	gpio_pin_set_dt(&leds[n], 0);
	shell_print(sh, "LED%d OFF", n);
	return 0;
}

static int cmd_led_toggle(const struct shell *sh, size_t argc, char **argv)
{
	int n = parse_led_index(sh, argv[1]);

	if (n < 0) {
		return n;
	}
	gpio_pin_toggle_dt(&leds[n]);
	shell_print(sh, "LED%d toggled", n);
	return 0;
}

static int cmd_led_status(const struct shell *sh, size_t argc, char **argv)
{
	for (int i = 0; i < (int)ARRAY_SIZE(leds); i++) {
		int val = gpio_pin_get_dt(&leds[i]);

		shell_print(sh, "LED%d: %s", i, val ? "ON" : "OFF");
	}
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_led,
	SHELL_CMD_ARG(on, NULL, "Turn on LED <n>", cmd_led_on, 2, 0),
	SHELL_CMD_ARG(off, NULL, "Turn off LED <n>", cmd_led_off, 2, 0),
	SHELL_CMD_ARG(toggle, NULL, "Toggle LED <n>", cmd_led_toggle, 2, 0),
	SHELL_CMD(status, NULL, "Show LED states", cmd_led_status),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(led, &sub_led, "LED control commands", NULL);

/* --- gpio read command --- */

static int cmd_gpio_read(const struct shell *sh, size_t argc, char **argv)
{
	int port = atoi(argv[1]);
	int pin  = atoi(argv[2]);

	if (port < 0 || port > 4 || pin < 0 || pin > 15) {
		shell_error(sh, "Invalid port %d (0=A..4=E) or pin %d (0-15)",
			    port, pin);
		return -EINVAL;
	}

	uint16_t pidr = sys_read16(GPIO_PIDR(port));
	int val = (pidr >> pin) & 1;

	shell_print(sh, "GPIO P%c%d = %d", 'A' + port, pin, val);
	return 0;
}

SHELL_CMD_ARG_REGISTER(gpio_read, NULL,
	"Read GPIO pin: gpio_read <port:0-4> <pin:0-15>",
	cmd_gpio_read, 3, 0);

/* --- sysinfo command --- */

static int cmd_sysinfo(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "=== HC32F460PETB System Info ===");
	shell_print(sh, "MCU: HC32F460 (Cortex-M4F)");
	shell_print(sh, "SystemCoreClock: %u Hz", SystemCoreClock);
	shell_print(sh, "Zephyr: %s", KERNEL_VERSION_STRING);
	shell_print(sh, "Uptime: %lld ms", k_uptime_get());
	shell_print(sh, "UART: USART1 PA9(TX)/PA10(RX) @ 115200");
	shell_print(sh, "LEDs: PD10 (LED3), PE15 (LED4)");
	shell_print(sh, "Button: PE2 (BTN1, input subsystem)");

	/* Read some key registers */
	uint16_t pwpr = sys_read16(0x40053BFCUL);

	shell_print(sh, "GPIO PWPR: 0x%04x (%s)", pwpr,
		    (pwpr & 1) ? "unlocked" : "locked");

	return 0;
}

SHELL_CMD_REGISTER(sysinfo, NULL, "Show system information", cmd_sysinfo);
