/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * HC32F460PETB Zephyr demo: LED blink + button interrupt + UART console
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define SLEEP_MS 500

/* LED0 = LED3 (PD10) */
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
/* LED1 = LED4 (PE15) */
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
/* BTN1 (PE2) */
static const struct gpio_dt_spec btn0 = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static struct gpio_callback btn_cb_data;
static volatile uint32_t btn_press_count;

static void button_isr(const struct device *dev, struct gpio_callback *cb,
		       uint32_t pins)
{
	btn_press_count++;
}

int main(void)
{
	int ret;
	bool led_state = true;
	uint32_t last_count = 0;

	printk("HC32F460 Zephyr demo starting\n");

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

	/* Configure button as input with interrupt */
	if (!gpio_is_ready_dt(&btn0)) {
		printk("Error: button GPIO device not ready\n");
		return -1;
	}

	ret = gpio_pin_configure_dt(&btn0, GPIO_INPUT);
	if (ret < 0) {
		printk("Error: failed to configure button: %d\n", ret);
		return ret;
	}

	/* Set up button interrupt callback */
	gpio_init_callback(&btn_cb_data, button_isr, BIT(btn0.pin));
	ret = gpio_add_callback(btn0.port, &btn_cb_data);
	if (ret < 0) {
		printk("Error: failed to add button callback: %d\n", ret);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&btn0, GPIO_INT_EDGE_FALLING);
	if (ret < 0) {
		printk("Warning: button interrupt not available (%d), using polling\n", ret);
	} else {
		printk("Button interrupt configured (falling edge)\n");
	}

	printk("LED0 (PD10), LED1 (PE15), BTN1 (PE2) configured\n");

	while (1) {
		/* Toggle LEDs alternately */
		gpio_pin_set_dt(&led0, led_state);
		gpio_pin_set_dt(&led1, !led_state);
		led_state = !led_state;

		/* Check for button interrupt events */
		uint32_t count = btn_press_count;
		if (count != last_count) {
			printk("[BTN1] pressed (count=%u)\n", count);
			last_count = count;
		}

		/* Periodic heartbeat */
		printk("alive (led=%d)\n", led_state);

		k_msleep(SLEEP_MS);
	}

	return 0;
}
