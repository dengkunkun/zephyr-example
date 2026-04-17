/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/printk.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <hc32_clock.h>
#include <hc32_power.h>
#include <hc32_uart.h>
#include <soc.h>

#include <hc32_ll_clk.h>
#include <hc32_ll_swdt.h>
#include <hc32_ll_wdt.h>

#define GPIO_BASE_ADDR                  0x40053800UL
#define GPIO_PFSR(port, pin)            (GPIO_BASE_ADDR + 0x0402U + (port) * 0x40U + (pin) * 0x04U)

#define TMRA_SELFTEST_TIMEOUT_MS        200
#define TMRA_LATE_TEST_WAIT_US          100
#define TMRA_LATE_GUARD_US              200
#define WATCHDOG_PROGRESS_WAIT_MS       50U
#define WATCHDOG_RELOAD_SETTLE_MS       10U
#define WATCHDOG_KEEPALIVE_MS           1000U
#define HC32_WDT_SELFTEST_MAX_MS        5000U
#define HC32_SWDT_SELFTEST_MAX_MS       ((uint32_t)((((uint64_t)DT_PROP(DT_NODELABEL(swdt0), counter_cycles) * \
						      (uint64_t)DT_PROP(DT_NODELABEL(swdt0), clock_divider) * \
						      1000000ULL / SWDTLRC_VALUE) + 999ULL) / 1000ULL))

static const struct device *const tmra_counter = DEVICE_DT_GET(DT_ALIAS(counter0));
static const struct device *const hc32_wdt = DEVICE_DT_GET(DT_NODELABEL(wdt0));
static const struct device *const hc32_swdt = DEVICE_DT_GET(DT_NODELABEL(swdt0));
static const struct device *const hc32_console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static K_SEM_DEFINE(tmra_test_sem, 0, 8);
static volatile uint32_t tmra_alarm_count;
static volatile uint32_t tmra_top_count;
static volatile uint8_t tmra_alarm_order[2];
static bool hc32_wdt_active;
static bool hc32_swdt_active;
static bool hc32_watchdog_keepalive_started;

static void hc32_watchdog_keepalive(struct k_timer *timer);
K_TIMER_DEFINE(hc32_watchdog_keepalive_timer, hc32_watchdog_keepalive, NULL);

static void tmra_test_reset_state(void)
{
	k_sem_reset(&tmra_test_sem);
	tmra_alarm_count = 0U;
	tmra_top_count = 0U;
	tmra_alarm_order[0] = 0xffU;
	tmra_alarm_order[1] = 0xffU;
}

static void tmra_alarm_test_cb(const struct device *dev, uint8_t chan_id,
			       uint32_t ticks, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(ticks);
	ARG_UNUSED(user_data);

	if (tmra_alarm_count < ARRAY_SIZE(tmra_alarm_order)) {
		tmra_alarm_order[tmra_alarm_count] = chan_id;
	}

	tmra_alarm_count++;
	k_sem_give(&tmra_test_sem);
}

static void tmra_top_test_cb(const struct device *dev, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	tmra_top_count++;
	k_sem_give(&tmra_test_sem);
}

static void hc32_watchdog_keepalive(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	if (hc32_wdt_active) {
		(void)wdt_feed(hc32_wdt, 0);
	}

	if (hc32_swdt_active) {
		(void)wdt_feed(hc32_swdt, 0);
	}
}

static void hc32_watchdog_stop_keepalive(void)
{
	if (!hc32_watchdog_keepalive_started) {
		return;
	}

	k_timer_stop(&hc32_watchdog_keepalive_timer);
	hc32_watchdog_keepalive_started = false;
}

static void hc32_watchdog_start_keepalive(void)
{
	if (hc32_watchdog_keepalive_started ||
	    (!hc32_wdt_active && !hc32_swdt_active)) {
		return;
	}

	k_timer_start(&hc32_watchdog_keepalive_timer,
		      K_MSEC(WATCHDOG_KEEPALIVE_MS),
		      K_MSEC(WATCHDOG_KEEPALIVE_MS));
	hc32_watchdog_keepalive_started = true;
}

static uint16_t hc32_watchdog_get_count(const struct device *dev)
{
	return (dev == hc32_wdt) ? WDT_GetCountValue() : SWDT_GetCountValue();
}

static bool hc32_swdt_count_observable(void)
{
	return CM_SWDT->SR != 0U;
}

static int hc32_watchdog_setup_once(const struct device *dev,
				    const struct wdt_timeout_cfg *cfg,
				    bool *active, const char *label)
{
	int ret;

	if (*active) {
		return 0;
	}

	ret = wdt_install_timeout(dev, cfg);
	if (ret < 0) {
		printk("[TEST] %s: FAIL (install=%d)\n", label, ret);
		return 1;
	}

	ret = wdt_setup(dev, WDT_OPT_PAUSE_HALTED_BY_DBG);
	if (ret < 0) {
		printk("[TEST] %s: FAIL (setup=%d)\n", label, ret);
		return 1;
	}

	*active = true;
	printk("[TEST] %s: PASS\n", label);
	return 0;
}

static int hc32_watchdog_exercise(const char *label, const struct device *dev)
{
	uint16_t before;
	uint16_t after;
	uint16_t reload;
	int ret;

	ret = wdt_feed(dev, 0);
	if (ret < 0) {
		printk("[TEST] %s: FAIL (prime=%d)\n", label, ret);
		return 1;
	}

	k_msleep(WATCHDOG_RELOAD_SETTLE_MS);
	before = hc32_watchdog_get_count(dev);
	k_msleep(WATCHDOG_PROGRESS_WAIT_MS);
	after = hc32_watchdog_get_count(dev);

	ret = wdt_feed(dev, 0);
	if (ret < 0) {
		printk("[TEST] %s: FAIL (reload=%d)\n", label, ret);
		return 1;
	}

	k_msleep(WATCHDOG_RELOAD_SETTLE_MS);
	reload = hc32_watchdog_get_count(dev);

	if (dev == hc32_swdt && before == 0U && after == 0U && reload == 0U &&
	    !hc32_swdt_count_observable()) {
		printk("[TEST] %s: FAIL (inactive: SWDT did not start)\n",
		       label);
		return 1;
	}

	if (!(after < before) || !(reload > after)) {
		printk("[TEST] %s: FAIL (before=%u after=%u reload=%u)\n",
		       label, before, after, reload);
		return 1;
	}

	printk("[TEST] %s: PASS (before=%u after=%u reload=%u)\n",
	       label, before, after, reload);
	return 0;
}

int hc32_run_core_selftests(void)
{
	stc_clock_freq_t freq;
	uint16_t pa9_func;
	uint16_t pa10_func;
	int failures = 0;

	if (CLK_GetClockFreq(&freq) != LL_OK) {
		printk("[TEST] clock-tree: FAIL (CLK_GetClockFreq)\n");
		return 1;
	}

	if (SystemCoreClock != 200000000U ||
	    freq.u32SysclkFreq != 200000000U ||
	    freq.u32HclkFreq != 200000000U ||
	    freq.u32Pclk1Freq != 100000000U ||
	    (CM_CMU->CKSWR & CMU_CKSWR_CKSW) != CLK_SYSCLK_SRC_PLL ||
	    CLK_GetStableStatus(CLK_STB_FLAG_XTAL) != SET ||
	    CLK_GetStableStatus(CLK_STB_FLAG_PLL) != SET) {
		failures++;
		printk("[TEST] clock-tree: FAIL (sys=%u hclk=%u pclk1=%u cksw=%u)\n",
		       freq.u32SysclkFreq, freq.u32HclkFreq, freq.u32Pclk1Freq,
		       (unsigned int)(CM_CMU->CKSWR & CMU_CKSWR_CKSW));
	} else {
		printk("[TEST] clock-tree: PASS (SYSCLK=%u HCLK=%u PCLK1=%u)\n",
		       freq.u32SysclkFreq, freq.u32HclkFreq, freq.u32Pclk1Freq);
	}

	pa9_func = (*(volatile uint16_t *)GPIO_PFSR(0U, 9U)) & 0x3fU;
	pa10_func = (*(volatile uint16_t *)GPIO_PFSR(0U, 10U)) & 0x3fU;

	if (pa9_func != 32U || pa10_func != 33U) {
		failures++;
		printk("[TEST] pinctrl-usart1: FAIL (PA9=%u PA10=%u)\n",
		       pa9_func, pa10_func);
	} else {
		printk("[TEST] pinctrl-usart1: PASS (PA9=%u PA10=%u)\n",
		       pa9_func, pa10_func);
	}

	return failures;
}

int hc32_run_timer_selftests(void)
{
	struct counter_alarm_cfg alarm0 = {
		.callback = tmra_alarm_test_cb,
		.user_data = NULL,
	};
	struct counter_alarm_cfg alarm1 = {
		.callback = tmra_alarm_test_cb,
		.user_data = NULL,
	};
	struct counter_top_cfg top_cfg = {
		.callback = tmra_top_test_cb,
		.user_data = NULL,
	};
	uint32_t freq_hz;
	uint32_t top;
	uint32_t tick_us;
	uint32_t late_guard_ticks;
	int ret;
	int failures = 0;

	if (!device_is_ready(tmra_counter)) {
		printk("[TEST] tmra-counter: FAIL (device not ready)\n");
		return 1;
	}

	freq_hz = counter_get_frequency(tmra_counter);
	top = counter_get_top_value(tmra_counter);

	if (freq_hz == 0U || top != 0xFFFFU) {
		failures++;
		printk("[TEST] tmra-counter-info: FAIL (freq=%u top=%u channels=%u)\n",
		       freq_hz, top, counter_get_num_of_channels(tmra_counter));
	} else {
		printk("[TEST] tmra-counter-info: PASS (freq=%u top=%u channels=%u)\n",
		       freq_hz, top, counter_get_num_of_channels(tmra_counter));
	}

	(void)counter_stop(tmra_counter);
	(void)counter_cancel_channel_alarm(tmra_counter, 0);
	(void)counter_cancel_channel_alarm(tmra_counter, 1);
	top_cfg.ticks = counter_get_max_top_value(tmra_counter);
	top_cfg.callback = NULL;
	top_cfg.user_data = NULL;
	top_cfg.flags = 0U;
	(void)counter_set_top_value(tmra_counter, &top_cfg);
	(void)counter_reset(tmra_counter);

	tmra_test_reset_state();
	ret = counter_start(tmra_counter);
	if (ret == 0) {
		alarm0.ticks = 256U;
		alarm0.flags = 0U;
		ret = counter_set_channel_alarm(tmra_counter, 0, &alarm0);
	}

	if (ret != 0 ||
	    k_sem_take(&tmra_test_sem, K_MSEC(TMRA_SELFTEST_TIMEOUT_MS)) != 0 ||
	    tmra_alarm_count != 1U || tmra_alarm_order[0] != 0U) {
		failures++;
		printk("[TEST] tmra-alarm-rel: FAIL (ret=%d count=%u first=%u)\n",
		       ret, tmra_alarm_count, tmra_alarm_order[0]);
	} else {
		printk("[TEST] tmra-alarm-rel: PASS (chan=%u)\n",
		       tmra_alarm_order[0]);
	}

	(void)counter_stop(tmra_counter);
	(void)counter_cancel_channel_alarm(tmra_counter, 0);
	(void)counter_cancel_channel_alarm(tmra_counter, 1);
	(void)counter_reset(tmra_counter);

	if (counter_get_num_of_channels(tmra_counter) >= 2U) {
		tmra_test_reset_state();
		ret = counter_start(tmra_counter);
		if (ret == 0) {
			top_cfg.ticks = 1024U;
			top_cfg.callback = tmra_top_test_cb;
			top_cfg.user_data = NULL;
			top_cfg.flags = 0U;
			ret = counter_set_top_value(tmra_counter, &top_cfg);
		}
		if (ret == 0) {
			alarm0.ticks = 200U;
			alarm0.flags = 0U;
			ret = counter_set_channel_alarm(tmra_counter, 0, &alarm0);
		}
		if (ret == 0) {
			alarm1.ticks = 400U;
			alarm1.flags = 0U;
			ret = counter_set_channel_alarm(tmra_counter, 1, &alarm1);
		}

		if (ret != 0 ||
		    k_sem_take(&tmra_test_sem, K_MSEC(TMRA_SELFTEST_TIMEOUT_MS)) != 0 ||
		    k_sem_take(&tmra_test_sem, K_MSEC(TMRA_SELFTEST_TIMEOUT_MS)) != 0 ||
		    k_sem_take(&tmra_test_sem, K_MSEC(TMRA_SELFTEST_TIMEOUT_MS)) != 0 ||
		    tmra_alarm_count != 2U || tmra_top_count != 1U ||
		    tmra_alarm_order[0] != 0U || tmra_alarm_order[1] != 1U) {
			failures++;
			printk("[TEST] tmra-top+alarm: FAIL (ret=%d alarms=%u top=%u order=%u,%u)\n",
			       ret, tmra_alarm_count, tmra_top_count,
			       tmra_alarm_order[0], tmra_alarm_order[1]);
		} else {
			printk("[TEST] tmra-top+alarm: PASS (order=%u,%u top=%u)\n",
			       tmra_alarm_order[0], tmra_alarm_order[1], tmra_top_count);
		}
	}

	(void)counter_stop(tmra_counter);
	(void)counter_cancel_channel_alarm(tmra_counter, 0);
	(void)counter_cancel_channel_alarm(tmra_counter, 1);
	(void)counter_reset(tmra_counter);
	top_cfg.ticks = counter_get_max_top_value(tmra_counter);
	top_cfg.callback = NULL;
	top_cfg.user_data = NULL;
	top_cfg.flags = 0U;
	(void)counter_set_top_value(tmra_counter, &top_cfg);

	tmra_test_reset_state();
	late_guard_ticks = counter_us_to_ticks(tmra_counter, TMRA_LATE_GUARD_US);
	if (late_guard_ticks == 0U) {
		late_guard_ticks = 1U;
	}

	ret = counter_set_guard_period(tmra_counter, late_guard_ticks,
				       COUNTER_GUARD_PERIOD_LATE_TO_SET);
	if (ret == 0) {
		ret = counter_start(tmra_counter);
	}
	if (ret == 0) {
		tick_us = counter_ticks_to_us(tmra_counter, 1U);
		if (tick_us == 0U) {
			tick_us = 1U;
		}
		k_busy_wait((2U * tick_us) + TMRA_LATE_TEST_WAIT_US);
		alarm0.ticks = 0U;
		alarm0.flags = COUNTER_ALARM_CFG_ABSOLUTE |
			       COUNTER_ALARM_CFG_EXPIRE_WHEN_LATE;
		ret = counter_set_channel_alarm(tmra_counter, 0, &alarm0);
	}

	if (ret != -ETIME ||
	    k_sem_take(&tmra_test_sem, K_MSEC(TMRA_SELFTEST_TIMEOUT_MS)) != 0 ||
	    tmra_alarm_count != 1U) {
		failures++;
		printk("[TEST] tmra-late-alarm: FAIL (ret=%d count=%u)\n",
		       ret, tmra_alarm_count);
	} else {
		printk("[TEST] tmra-late-alarm: PASS\n");
	}

	(void)counter_stop(tmra_counter);
	(void)counter_cancel_channel_alarm(tmra_counter, 0);
	(void)counter_cancel_channel_alarm(tmra_counter, 1);
	top_cfg.ticks = counter_get_max_top_value(tmra_counter);
	top_cfg.callback = NULL;
	top_cfg.user_data = NULL;
	top_cfg.flags = 0U;
	(void)counter_set_top_value(tmra_counter, &top_cfg);
	(void)counter_reset(tmra_counter);

	return failures;
}

int hc32_run_watchdog_selftests(void)
{
	struct wdt_timeout_cfg wdt_cfg = {
		.window = {
			.min = 0U,
			.max = HC32_WDT_SELFTEST_MAX_MS,
		},
		.callback = NULL,
		.flags = WDT_FLAG_RESET_SOC,
	};
	struct wdt_timeout_cfg swdt_cfg = {
		.window = {
			.min = 0U,
			.max = HC32_SWDT_SELFTEST_MAX_MS,
		},
		.callback = NULL,
		.flags = WDT_FLAG_RESET_SOC,
	};
	int failures = 0;

	if (!device_is_ready(hc32_wdt)) {
		printk("[TEST] wdt-device: FAIL (device not ready)\n");
		failures++;
	}

	if (!device_is_ready(hc32_swdt)) {
		printk("[TEST] swdt-device: FAIL (device not ready)\n");
		failures++;
	}

	if (failures != 0) {
		return failures;
	}

	hc32_watchdog_stop_keepalive();

	failures += hc32_watchdog_setup_once(hc32_wdt, &wdt_cfg,
						     &hc32_wdt_active,
						     "wdt-setup");
	failures += hc32_watchdog_setup_once(hc32_swdt, &swdt_cfg,
						     &hc32_swdt_active,
						     "swdt-setup");

	if (hc32_wdt_active) {
		failures += hc32_watchdog_exercise("wdt-window", hc32_wdt);
	}

	if (hc32_swdt_active) {
		failures += hc32_watchdog_exercise("swdt-iwdg", hc32_swdt);
	}

	hc32_watchdog_start_keepalive();

	return failures;
}

int hc32_run_power_selftests(void)
{
	stc_clock_freq_t freq;
	int ret;
	int failures = 0;

	ret = hc32_power_force_sleep_ms(20U);
	if (ret != 0) {
		failures++;
		printk("[TEST] pm-sleep: FAIL (%d)\n", ret);
	} else {
		printk("[TEST] pm-sleep: PASS\n");
	}

	ret = hc32_clock_set_mode(HC32_CLOCK_MODE_LOW_SPEED);
	if ((ret == 0) && device_is_ready(hc32_console)) {
		ret = hc32_uart_reconfigure(hc32_console);
	}

	if ((ret != 0) ||
	    (CLK_GetClockFreq(&freq) != LL_OK) ||
	    (SystemCoreClock != 8000000U) ||
	    (freq.u32SysclkFreq != 8000000U) ||
	    ((CM_CMU->CKSWR & CMU_CKSWR_CKSW) != CLK_SYSCLK_SRC_MRC)) {
		failures++;
		printk("[TEST] clock-low-speed: FAIL (ret=%d sys=%u src=%u)\n",
		       ret, SystemCoreClock,
		       (unsigned int)(CM_CMU->CKSWR & CMU_CKSWR_CKSW));
	} else {
		printk("[TEST] clock-low-speed: PASS (SYSCLK=%u)\n",
		       freq.u32SysclkFreq);
	}

	ret = hc32_clock_set_mode(HC32_CLOCK_MODE_HIGH_PERFORMANCE);
	if ((ret == 0) && device_is_ready(hc32_console)) {
		ret = hc32_uart_reconfigure(hc32_console);
	}

	if ((ret != 0) ||
	    (CLK_GetClockFreq(&freq) != LL_OK) ||
	    (SystemCoreClock != 200000000U) ||
	    (freq.u32SysclkFreq != 200000000U) ||
	    ((CM_CMU->CKSWR & CMU_CKSWR_CKSW) != CLK_SYSCLK_SRC_PLL)) {
		failures++;
		printk("[TEST] clock-high-speed: FAIL (ret=%d sys=%u src=%u)\n",
		       ret, SystemCoreClock,
		       (unsigned int)(CM_CMU->CKSWR & CMU_CKSWR_CKSW));
	} else {
		printk("[TEST] clock-high-speed: PASS (SYSCLK=%u)\n",
		       freq.u32SysclkFreq);
	}

	ret = hc32_power_force_stop_ms(30U);
	if (ret != 0) {
		failures++;
		printk("[TEST] pm-stop: FAIL (%d)\n", ret);
	} else {
		printk("[TEST] pm-stop: PASS\n");
	}

	return failures;
}
