/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file led_statu.c
 * @brief 74HC595 LED status controller (Zephyr port of exio_595 + led_statu_ctrl).
 *
 * Hardware: SCK=PB9, RCK=PB8, DAT=PB7 (DT node labels led_sck/led_rck/led_dat).
 * Update period: 100 ms via k_work_delayable.
 * Only compiled when CONFIG_APP_FMR_LED_595=y.
 */

#include "led_statu.h"

#ifdef CONFIG_APP_FMR_LED_595

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led_statu, LOG_LEVEL_INF);

/* DT GPIO specs */
static const struct gpio_dt_spec sck_gpio =
	GPIO_DT_SPEC_GET(DT_NODELABEL(led_sck), gpios);
static const struct gpio_dt_spec rck_gpio =
	GPIO_DT_SPEC_GET(DT_NODELABEL(led_rck), gpios);
static const struct gpio_dt_spec dat_gpio =
	GPIO_DT_SPEC_GET(DT_NODELABEL(led_dat), gpios);

#define LED_CH_MAX 8
#define LED_UPDATE_MS 100

struct led_ch_state {
	enum led_statu_mode mode;
	uint8_t period;   /**< blink period in 100 ms ticks (0=solid) */
	uint8_t tick;     /**< current tick counter */
	uint8_t on;       /**< current physical state */
};

static struct led_ch_state ch_state[LED_CH_MAX];

static struct k_work_delayable led_work;

/** Shift 8 bits MSB-first into the 74HC595 */
static void shift_out(uint8_t byte)
{
	for (int i = 7; i >= 0; i--) {
		gpio_pin_set_dt(&dat_gpio, (byte >> i) & 1);
		gpio_pin_set_dt(&sck_gpio, 1);
		gpio_pin_set_dt(&sck_gpio, 0);
	}
}

static void latch(void)
{
	gpio_pin_set_dt(&rck_gpio, 1);
	gpio_pin_set_dt(&rck_gpio, 0);
}

static void led_update_work_fn(struct k_work *work)
{
	uint8_t out = 0;

	for (int i = 0; i < LED_CH_MAX; i++) {
		struct led_ch_state *s = &ch_state[i];

		switch (s->mode) {
		case LED_STATU_ON:
			s->on = 1;
			break;
		case LED_STATU_OFF:
			s->on = 0;
			break;
		case LED_STATU_BLINK:
			if (s->period == 0) {
				s->on = 1;
			} else {
				s->tick++;
				if (s->tick >= s->period) {
					s->tick = 0;
					s->on = !s->on;
				}
			}
			break;
		default:
			break;
		}
		if (s->on) {
			out |= BIT(i);
		}
	}

	shift_out(out);
	latch();

	k_work_reschedule(&led_work, K_MSEC(LED_UPDATE_MS));
}

int led_statu_init(void)
{
	int ret;

	ret = gpio_pin_configure_dt(&sck_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret) {
		return ret;
	}
	ret = gpio_pin_configure_dt(&rck_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret) {
		return ret;
	}
	ret = gpio_pin_configure_dt(&dat_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret) {
		return ret;
	}

	/* All LEDs off initially */
	shift_out(0);
	latch();

	k_work_init_delayable(&led_work, led_update_work_fn);
	k_work_reschedule(&led_work, K_MSEC(LED_UPDATE_MS));

	LOG_INF("LED 595 init OK");
	return 0;
}

void lsc_ctrl(uint8_t ch, enum led_statu_mode mode)
{
	if (ch < 1 || ch > LED_CH_MAX) {
		return;
	}
	ch_state[ch - 1].mode = mode;
}

void lsc_set_period(uint8_t ch, uint8_t period)
{
	if (ch < 1 || ch > LED_CH_MAX) {
		return;
	}
	ch_state[ch - 1].period = period;
	ch_state[ch - 1].tick   = 0;
}

void lsc_flush(void)
{
	/* Cancel pending work and run immediately */
	k_work_cancel_delayable(&led_work);
	led_update_work_fn(NULL);
}

#else /* !CONFIG_APP_FMR_LED_595 */

int led_statu_init(void) { return 0; }
void lsc_ctrl(uint8_t ch, enum led_statu_mode mode) { (void)ch; (void)mode; }
void lsc_set_period(uint8_t ch, uint8_t period) { (void)ch; (void)period; }
void lsc_flush(void) {}

#endif /* CONFIG_APP_FMR_LED_595 */
