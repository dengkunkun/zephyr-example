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
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/version.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <hc32_clock.h>
#include <hc32_power.h>
#include <hc32_uart.h>
#include <soc.h>

#include <hc32_ll_clk.h>
#include <hc32_ll_swdt.h>
#include <hc32_ll_wdt.h>

/* GPIO base for direct register reads */
#define GPIO_BASE  0x40053800UL
#define GPIO_PIDR(port) (GPIO_BASE + 0x0000U + (uint32_t)(port) * 0x10U)

int hc32_run_core_selftests(void);
int hc32_run_timer_selftests(void);
int hc32_run_watchdog_selftests(void);
int hc32_run_power_selftests(void);

static const struct device *const tmra_counter = DEVICE_DT_GET(DT_ALIAS(counter0));
static const struct device *const hc32_wdt = DEVICE_DT_GET(DT_NODELABEL(wdt0));
static const struct device *const hc32_swdt = DEVICE_DT_GET(DT_NODELABEL(swdt0));
#if DT_HAS_CHOSEN(zephyr_shell_uart)
static const struct device *const hc32_shell_uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));
#elif DT_HAS_CHOSEN(zephyr_console)
static const struct device *const hc32_shell_uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
#endif

static const struct gpio_dt_spec leds[] = {
	GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios),
};

static const char *clock_src_name(uint32_t src)
{
	switch (src) {
	case CLK_SYSCLK_SRC_HRC:
		return "HRC";
	case CLK_SYSCLK_SRC_MRC:
		return "MRC";
	case CLK_SYSCLK_SRC_LRC:
		return "LRC";
	case CLK_SYSCLK_SRC_XTAL:
		return "HXT";
	case CLK_SYSCLK_SRC_XTAL32:
		return "LXT";
	case CLK_SYSCLK_SRC_PLL:
		return "PLL";
	default:
		return "unknown";
	}
}

static const char *clock_mode_name(enum hc32_clock_mode mode)
{
	return (mode == HC32_CLOCK_MODE_LOW_SPEED) ? "low-speed" : "high-performance";
}

static int parse_duration_ms(const struct shell *sh, const char *arg, uint32_t *value)
{
	char *end;
	unsigned long parsed;

	parsed = strtoul(arg, &end, 0);
	if ((arg[0] == '\0') || (*end != '\0') || (parsed == 0UL) ||
	    (parsed > UINT32_MAX)) {
		shell_error(sh, "Invalid duration '%s'", arg);
		return -EINVAL;
	}

	*value = (uint32_t)parsed;
	return 0;
}

static int shell_print_clock_info(const struct shell *sh)
{
	stc_clock_freq_t freq;
	uint32_t src = CM_CMU->CKSWR & CMU_CKSWR_CKSW;

	if (CLK_GetClockFreq(&freq) != LL_OK) {
		shell_error(sh, "Failed to read clock tree");
		return -EIO;
	}

	shell_print(sh, "Clock source: %s", clock_src_name(src));
	shell_print(sh, "SYSCLK=%u HCLK=%u EXCLK=%u", freq.u32SysclkFreq,
		    freq.u32HclkFreq, freq.u32ExclkFreq);
	shell_print(sh, "PCLK0=%u PCLK1=%u PCLK2=%u PCLK3=%u PCLK4=%u",
		    freq.u32Pclk0Freq, freq.u32Pclk1Freq, freq.u32Pclk2Freq,
		    freq.u32Pclk3Freq, freq.u32Pclk4Freq);

	return 0;
}

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

	return shell_print_clock_info(sh);
}

SHELL_CMD_REGISTER(sysinfo, NULL, "Show system information", cmd_sysinfo);

static int cmd_clock_info(const struct shell *sh, size_t argc, char **argv)
{
	return shell_print_clock_info(sh);
}

static int cmd_clock_speed(const struct shell *sh, size_t argc, char **argv)
{
	enum hc32_clock_mode mode;
	int ret;

	if (strcmp(argv[1], "low") == 0) {
		mode = HC32_CLOCK_MODE_LOW_SPEED;
	} else if (strcmp(argv[1], "high") == 0) {
		mode = HC32_CLOCK_MODE_HIGH_PERFORMANCE;
	} else {
		shell_error(sh, "Usage: clock speed <low|high>");
		return -EINVAL;
	}

	ret = hc32_clock_set_mode(mode);
	if (ret != 0) {
		shell_error(sh, "Clock switch failed: %d", ret);
		return ret;
	}

#if DT_HAS_CHOSEN(zephyr_shell_uart) || DT_HAS_CHOSEN(zephyr_console)
	if (device_is_ready(hc32_shell_uart)) {
		ret = hc32_uart_reconfigure(hc32_shell_uart);
		if (ret != 0) {
			shell_error(sh, "UART reconfigure failed: %d", ret);
			return ret;
		}
	}
#endif

	shell_print(sh, "Clock mode: %s", clock_mode_name(mode));
	return shell_print_clock_info(sh);
}

static int cmd_power_info(const struct shell *sh, size_t argc, char **argv)
{
	struct hc32_power_status status;
	const struct hc32_power_boot_status *boot = hc32_power_boot_status_get();

	hc32_power_get_status(&status);

	shell_print(sh, "mode=%s sysclk=%u stop-lock=%s",
		    clock_mode_name(status.clock_mode),
		    hc32_clock_get_sysclk_hz(),
		    status.stop_locked ? "on" : "off");
	shell_print(sh, "entries: sleep=%u stop=%u last-lpm=%llu us",
		    status.suspend_idle_entries,
		    status.suspend_ram_entries,
		    (unsigned long long)status.last_lpm_us);
	shell_print(sh,
		    "boot: por=%s pin=%s bor=%s wdt=%s swdt=%s pd=%s wkt=%s",
		    boot->power_on_reset ? "yes" : "no",
		    boot->pin_reset ? "yes" : "no",
		    boot->brown_out_reset ? "yes" : "no",
		    boot->wdt_reset ? "yes" : "no",
		    boot->swdt_reset ? "yes" : "no",
		    boot->power_down_reset ? "yes" : "no",
		    boot->wakeup_timer ? "yes" : "no");
	return 0;
}

static int cmd_power_sleep(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t duration_ms;
	int ret = parse_duration_ms(sh, argv[1], &duration_ms);

	if (ret != 0) {
		return ret;
	}

	ret = hc32_power_force_sleep_ms(duration_ms);
	if (ret != 0) {
		shell_error(sh, "Sleep test failed: %d", ret);
		return ret;
	}

	shell_print(sh, "Sleep complete: %u ms", duration_ms);
	return 0;
}

static int cmd_power_stop(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t duration_ms;
	int ret = parse_duration_ms(sh, argv[1], &duration_ms);

	if (ret != 0) {
		return ret;
	}

	ret = hc32_power_force_stop_ms(duration_ms);
	if (ret != 0) {
		shell_error(sh, "Stop test failed: %d", ret);
		return ret;
	}

	shell_print(sh, "Stop complete: %u ms", duration_ms);
	return 0;
}

static int cmd_power_hibernate(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t duration_ms;
	int ret = parse_duration_ms(sh, argv[1], &duration_ms);

	if (ret != 0) {
		return ret;
	}

	shell_print(sh, "Entering power-down for %u ms; board will reboot", duration_ms);
	k_msleep(20);
	ret = hc32_power_enter_hibernate_ms(duration_ms);
	shell_error(sh, "Power-down entry failed: %d", ret);
	return ret;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_clock,
	SHELL_CMD(info, NULL, "Show clock tree information", cmd_clock_info),
	SHELL_CMD_ARG(speed, NULL, "Switch CPU speed: speed <low|high>", cmd_clock_speed, 2, 0),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(clock, &sub_clock, "Clock inspection commands", NULL);

static int cmd_test_core(const struct shell *sh, size_t argc, char **argv)
{
	int failures = hc32_run_core_selftests();

	if (failures != 0) {
		shell_error(sh, "Core self-tests failed: %d", failures);
		return -EIO;
	}

	shell_print(sh, "Core self-tests passed");
	return 0;
}

static int cmd_counter_info(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t value;
	int ret;

	if (!device_is_ready(tmra_counter)) {
		shell_error(sh, "TMRA counter device not ready");
		return -ENODEV;
	}

	ret = counter_get_value(tmra_counter, &value);
	if (ret != 0) {
		shell_error(sh, "Counter read failed: %d", ret);
		return ret;
	}

	shell_print(sh, "Device: %s", tmra_counter->name);
	shell_print(sh, "freq=%u top=%u max_top=%u channels=%u",
		    counter_get_frequency(tmra_counter),
		    counter_get_top_value(tmra_counter),
		    counter_get_max_top_value(tmra_counter),
		    counter_get_num_of_channels(tmra_counter));
	shell_print(sh, "value=%u pending=%u guard=%u", value,
		    counter_get_pending_int(tmra_counter),
		    counter_get_guard_period(tmra_counter,
					     COUNTER_GUARD_PERIOD_LATE_TO_SET));

	return 0;
}

static int cmd_counter_start(const struct shell *sh, size_t argc, char **argv)
{
	int ret = counter_start(tmra_counter);

	if (ret != 0) {
		shell_error(sh, "Counter start failed: %d", ret);
		return ret;
	}

	shell_print(sh, "TMRA counter started");
	return 0;
}

static int cmd_counter_stop(const struct shell *sh, size_t argc, char **argv)
{
	int ret = counter_stop(tmra_counter);

	if (ret != 0) {
		shell_error(sh, "Counter stop failed: %d", ret);
		return ret;
	}

	shell_print(sh, "TMRA counter stopped");
	return 0;
}

static int cmd_test_timer(const struct shell *sh, size_t argc, char **argv)
{
	int failures = hc32_run_timer_selftests();

	if (failures != 0) {
		shell_error(sh, "Timer self-tests failed: %d", failures);
		return -EIO;
	}

	shell_print(sh, "Timer self-tests passed");
	return 0;
}

static uint32_t hc32_watchdog_status_bits(const struct device *dev)
{
	uint32_t status = 0U;

	if (dev == hc32_wdt) {
		if (WDT_GetStatus(WDT_FLAG_UDF) == SET) {
			status |= WDT_FLAG_UDF;
		}
		if (WDT_GetStatus(WDT_FLAG_REFRESH) == SET) {
			status |= WDT_FLAG_REFRESH;
		}
	} else {
		if (SWDT_GetStatus(SWDT_FLAG_UDF) == SET) {
			status |= SWDT_FLAG_UDF;
		}
		if (SWDT_GetStatus(SWDT_FLAG_REFRESH) == SET) {
			status |= SWDT_FLAG_REFRESH;
		}
	}

	return status;
}

static uint16_t hc32_watchdog_count(const struct device *dev)
{
	return (dev == hc32_wdt) ? WDT_GetCountValue() : SWDT_GetCountValue();
}

static bool hc32_swdt_count_observable(void)
{
	return CM_SWDT->SR != 0U;
}

static int cmd_watchdog_info(const struct shell *sh, size_t argc, char **argv)
{
	if (!device_is_ready(hc32_wdt) || !device_is_ready(hc32_swdt)) {
		shell_error(sh, "Watchdog devices are not ready");
		return -ENODEV;
	}

	shell_print(sh, "wdt: count=%u flags=0x%02x cr=0x%08x dbg-stop=%s",
		    hc32_watchdog_count(hc32_wdt),
		    hc32_watchdog_status_bits(hc32_wdt),
		    CM_WDT->CR,
		    (CM_DBGC->MCUSTPCTL & DBGC_MCUSTPCTL_WDTSTP) ? "on" : "off");
	shell_print(sh,
		    "swdt: count=%u flags=0x%02x dbg-stop=%s state=%s",
		    hc32_watchdog_count(hc32_swdt),
		    hc32_watchdog_status_bits(hc32_swdt),
		    (CM_DBGC->MCUSTPCTL & DBGC_MCUSTPCTL_SWDTSTP) ? "on" : "off",
		    hc32_swdt_count_observable() ? "active" : "inactive");
	return 0;
}

static int cmd_watchdog_feed(const struct shell *sh, size_t argc, char **argv)
{
	int ret = 0;

	if (strcmp(argv[1], "wdt") == 0 || strcmp(argv[1], "all") == 0) {
		ret = wdt_feed(hc32_wdt, 0);
		if (ret != 0) {
			shell_error(sh, "WDT feed failed: %d", ret);
			return ret;
		}
	}

	if (strcmp(argv[1], "swdt") == 0 || strcmp(argv[1], "all") == 0) {
		ret = wdt_feed(hc32_swdt, 0);
		if (ret != 0) {
			shell_error(sh, "SWDT feed failed: %d", ret);
			return ret;
		}
	}

	if (strcmp(argv[1], "wdt") != 0 &&
	    strcmp(argv[1], "swdt") != 0 &&
	    strcmp(argv[1], "all") != 0) {
		shell_error(sh, "Usage: watchdog feed <wdt|swdt|all>");
		return -EINVAL;
	}

	shell_print(sh, "Fed %s watchdog(s)", argv[1]);
	return 0;
}

static int cmd_test_watchdog(const struct shell *sh, size_t argc, char **argv)
{
	int failures = hc32_run_watchdog_selftests();

	if (failures != 0) {
		shell_error(sh, "Watchdog self-tests failed: %d", failures);
		return -EIO;
	}

	shell_print(sh, "Watchdog self-tests passed");
	return 0;
}

static int cmd_test_power(const struct shell *sh, size_t argc, char **argv)
{
	int failures = hc32_run_power_selftests();

	if (failures != 0) {
		shell_error(sh, "Power self-tests failed: %d", failures);
		return -EIO;
	}

	shell_print(sh, "Power self-tests passed");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_test,
	SHELL_CMD(core, NULL, "Run clocks/pinctrl self-tests", cmd_test_core),
	SHELL_CMD(timer, NULL, "Run TMRA counter self-tests", cmd_test_timer),
	SHELL_CMD(watchdog, NULL, "Run watchdog self-tests", cmd_test_watchdog),
	SHELL_CMD(power, NULL, "Run power-management self-tests", cmd_test_power),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(test, &sub_test, "Driver self-test commands", NULL);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_counter,
	SHELL_CMD(info, NULL, "Show TMRA counter information", cmd_counter_info),
	SHELL_CMD(start, NULL, "Start TMRA counter", cmd_counter_start),
	SHELL_CMD(stop, NULL, "Stop TMRA counter", cmd_counter_stop),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(counter, &sub_counter, "TMRA counter commands", NULL);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_watchdog,
	SHELL_CMD(info, NULL, "Show watchdog counters and flags", cmd_watchdog_info),
	SHELL_CMD_ARG(feed, NULL, "Feed wdt|swdt|all", cmd_watchdog_feed, 2, 0),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(watchdog, &sub_watchdog, "Watchdog commands", NULL);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_power,
	SHELL_CMD(info, NULL, "Show PM state and boot-reset status", cmd_power_info),
	SHELL_CMD_ARG(sleep, NULL, "Enter suspend-to-idle for <ms>", cmd_power_sleep, 2, 0),
	SHELL_CMD_ARG(stop, NULL, "Enter stop mode for <ms>", cmd_power_stop, 2, 0),
	SHELL_CMD_ARG(hibernate, NULL, "Enter power-down for <ms> then reboot", cmd_power_hibernate, 2, 0),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(power, &sub_power, "Power-management commands", NULL);
