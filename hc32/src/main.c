/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * HC32F460PETB Zephyr demo:
 *   - LED blink (PD10/PE15)
 *   - Button via Zephyr input subsystem (PE2, gpio-keys)
 *   - UART console on USART1 (PA9/PA10)
 *   - WDT/SWDT watchdog keepalive
 *   - Zephyr shell with demo commands
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/input/input.h>
#include <hc32_power.h>

#define SLEEP_MS 500

int hc32_run_core_selftests(void);
int hc32_run_timer_selftests(void);
int hc32_run_watchdog_selftests(void);

/* LED0 = LED3 (PD10) */
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
/* LED1 = LED4 (PE15) */
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

/* Button press counter (updated by input callback) */
static volatile uint32_t btn_press_count;

/* Input event callback for gpio-keys */
static void btn_input_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if (evt->type == INPUT_EV_KEY && evt->value) {
		btn_press_count++;
		printk("[BTN1] pressed (count=%u, code=%u)\n",
		       btn_press_count, evt->code);
	}
}

/* Register callback for all input events from the gpio_keys node */
INPUT_CALLBACK_DEFINE(NULL, btn_input_cb, NULL);

int main(void)
{
	int ret;
	bool led_state = true;
	const struct hc32_power_boot_status *boot = hc32_power_boot_status_get();

	printk("HC32F460 Zephyr demo starting\n");
	printk("USART1: PA9(TX) PA10(RX) @ 115200\n");
	printk("Clock: HXT 8MHz -> PLL 200MHz (PCLK1 100MHz)\n");
	if (boot->power_down_reset) {
		printk("Wake: power-down reset (%s)\n",
		       boot->wakeup_timer ? "WKTM" : "other");
	}

	/* Configure LEDs */
	if (!gpio_is_ready_dt(&led0) || !gpio_is_ready_dt(&led1)) {
		printk("Error: LED GPIO devices not ready\n");
		return -1;
	}

	ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		printk("Error: failed to configure LED0: %d\n", ret);
		return ret;
	}

	ret = gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		printk("Error: failed to configure LED1: %d\n", ret);
		return ret;
	}

	printk("LED0 (PD10), LED1 (PE15), BTN1 (PE2) configured\n");
	ret = hc32_run_core_selftests();
	printk("Core self-tests: %s\n", ret == 0 ? "PASS" : "FAIL");
	ret = hc32_run_timer_selftests();
	printk("Timer self-tests: %s\n", ret == 0 ? "PASS" : "FAIL");
	ret = hc32_run_watchdog_selftests();
	printk("Watchdog self-tests: %s\n", ret == 0 ? "PASS" : "FAIL");
	printk("Shell available — type 'help' for commands\n");

	while (1) {
		/* Toggle LEDs alternately */
		gpio_pin_set_dt(&led0, led_state);
		gpio_pin_set_dt(&led1, !led_state);
		led_state = !led_state;

		k_msleep(SLEEP_MS);
	}

	return 0;
}
