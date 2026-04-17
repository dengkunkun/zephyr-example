/*
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT hdsc_hc32_wdt

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <soc.h>

#include <hc32_ll_wdt.h>

LOG_MODULE_REGISTER(wdt_hc32_wdt, CONFIG_WDT_LOG_LEVEL);

#define HC32_CLOCK_ID(bus, bit) ((((bus) & 0xffU) << 8) | ((bit) & 0xffU))
#define HC32_INTC_SEL_ADDR(n)   ((uintptr_t)&CM_INTC->SEL0 + ((uintptr_t)(n) * sizeof(uint32_t)))

#define HC32_WDT_SUPPORTED_FLAGS (WDT_FLAG_RESET_NONE | WDT_FLAG_RESET_SOC)

struct hc32_wdt_count_period {
	uint32_t cycles;
	uint32_t sel;
};

struct hc32_wdt_clock_div {
	uint32_t factor;
	uint32_t sel;
};

struct hc32_wdt_refresh_range {
	uint8_t elapsed_min_pct;
	uint8_t elapsed_max_pct;
	uint32_t sel;
};

struct hc32_wdt_selection {
	uint32_t count_period_sel;
	uint32_t clock_div_sel;
	uint32_t refresh_range_sel;
};

struct hc32_wdt_config {
	const struct device *clock_dev;
	uint16_t clock_id;
	uint16_t int_src;
	uint8_t irq_num;
	void (*irq_config_func)(const struct device *dev);
};

struct hc32_wdt_data {
	struct hc32_wdt_selection selection;
	wdt_callback_t callback;
	bool timeout_installed;
	bool started;
	uint8_t flags;
};

static const struct hc32_wdt_count_period hc32_wdt_count_periods[] = {
	{ 256U, WDT_CNT_PERIOD256 },
	{ 4096U, WDT_CNT_PERIOD4096 },
	{ 16384U, WDT_CNT_PERIOD16384 },
	{ 65536U, WDT_CNT_PERIOD65536 },
};

static const struct hc32_wdt_clock_div hc32_wdt_clock_divs[] = {
	{ 4U, WDT_CLK_DIV4 },
	{ 64U, WDT_CLK_DIV64 },
	{ 128U, WDT_CLK_DIV128 },
	{ 256U, WDT_CLK_DIV256 },
	{ 512U, WDT_CLK_DIV512 },
	{ 1024U, WDT_CLK_DIV1024 },
	{ 2048U, WDT_CLK_DIV2048 },
	{ 8192U, WDT_CLK_DIV8192 },
};

static const struct hc32_wdt_refresh_range hc32_wdt_refresh_ranges[] = {
	{   0U,  25U, WDT_RANGE_75TO100PCT },
	{   0U,  50U, WDT_RANGE_50TO100PCT },
	{   0U,  75U, WDT_RANGE_25TO100PCT },
	{   0U, 100U, WDT_RANGE_0TO100PCT },
	{  25U,  50U, WDT_RANGE_50TO75PCT },
	{  25U,  75U, WDT_RANGE_25TO75PCT },
	{  25U, 100U, WDT_RANGE_0TO75PCT },
	{  50U,  75U, WDT_RANGE_25TO50PCT },
	{  50U, 100U, WDT_RANGE_0TO50PCT },
	{  75U, 100U, WDT_RANGE_0TO25PCT },
};

static void hc32_wdt_configure_debug_pause(bool enabled)
{
	if (enabled) {
		CM_DBGC->MCUSTPCTL |= DBGC_MCUSTPCTL_WDTSTP;
	} else {
		CM_DBGC->MCUSTPCTL &= ~DBGC_MCUSTPCTL_WDTSTP;
	}
}

static int hc32_wdt_get_pclk3(const struct device *dev, uint32_t *rate)
{
	const struct hc32_wdt_config *cfg = dev->config;

	if (!device_is_ready(cfg->clock_dev)) {
		return -ENODEV;
	}

	return clock_control_get_rate(cfg->clock_dev,
				      (clock_control_subsys_t)(uintptr_t)cfg->clock_id,
				      rate);
}

static int hc32_wdt_choose_window(uint32_t pclk_hz,
				  const struct wdt_timeout_cfg *cfg,
				  struct hc32_wdt_selection *selection)
{
	uint64_t req_min_us = (uint64_t)cfg->window.min * 1000ULL;
	uint64_t req_max_us = (uint64_t)cfg->window.max * 1000ULL;
	uint64_t best_slack = UINT64_MAX;
	bool found = false;

	for (size_t i = 0; i < ARRAY_SIZE(hc32_wdt_count_periods); i++) {
		for (size_t j = 0; j < ARRAY_SIZE(hc32_wdt_clock_divs); j++) {
			uint64_t total_us =
				((uint64_t)hc32_wdt_count_periods[i].cycles *
				 (uint64_t)hc32_wdt_clock_divs[j].factor *
				 1000000ULL) / pclk_hz;

			for (size_t k = 0; k < ARRAY_SIZE(hc32_wdt_refresh_ranges); k++) {
				uint64_t actual_min_us =
					(total_us * hc32_wdt_refresh_ranges[k].elapsed_min_pct) / 100ULL;
				uint64_t actual_max_us =
					(total_us * hc32_wdt_refresh_ranges[k].elapsed_max_pct) / 100ULL;
				uint64_t slack;

				if (actual_min_us > req_min_us || actual_max_us < req_max_us) {
					continue;
				}

				slack = (req_min_us - actual_min_us) +
					(actual_max_us - req_max_us);
				if (slack >= best_slack) {
					continue;
				}

				best_slack = slack;
				selection->count_period_sel = hc32_wdt_count_periods[i].sel;
				selection->clock_div_sel = hc32_wdt_clock_divs[j].sel;
				selection->refresh_range_sel = hc32_wdt_refresh_ranges[k].sel;
				found = true;
			}
		}
	}

	return found ? 0 : -EINVAL;
}

static void hc32_wdt_isr(const struct device *dev)
{
	struct hc32_wdt_data *data = dev->data;
	uint32_t flags = 0U;

	k_busy_wait(30U);

	if (WDT_GetStatus(WDT_FLAG_UDF) == SET) {
		flags |= WDT_FLAG_UDF;
	}
	if (WDT_GetStatus(WDT_FLAG_REFRESH) == SET) {
		flags |= WDT_FLAG_REFRESH;
	}

	if (flags != 0U) {
		(void)WDT_ClearStatus(flags);
	}

	if (data->callback != NULL) {
		data->callback(dev, 0);
	}
}

static int hc32_wdt_setup(const struct device *dev, uint8_t options)
{
	const struct hc32_wdt_config *cfg = dev->config;
	struct hc32_wdt_data *data = dev->data;
	stc_wdt_init_t init = {
		.u32CountPeriod = data->selection.count_period_sel,
		.u32ClockDiv = data->selection.clock_div_sel,
		.u32RefreshRange = data->selection.refresh_range_sel,
		.u32LPMCount = (options & WDT_OPT_PAUSE_IN_SLEEP) ?
			       WDT_LPM_CNT_STOP : WDT_LPM_CNT_CONT,
		.u32ExceptionType = (data->flags & WDT_FLAG_RESET_MASK) == WDT_FLAG_RESET_SOC ?
				    WDT_EXP_TYPE_RST : WDT_EXP_TYPE_INT,
	};
	int ret;

	if (!data->timeout_installed) {
		return -EINVAL;
	}

	if (data->started) {
		return -EBUSY;
	}

	hc32_wdt_configure_debug_pause((options & WDT_OPT_PAUSE_HALTED_BY_DBG) != 0U);

	irq_disable(cfg->irq_num);
	sys_write32(0U, HC32_INTC_SEL_ADDR(cfg->irq_num));
	NVIC_ClearPendingIRQ((IRQn_Type)cfg->irq_num);
	(void)WDT_ClearStatus(WDT_FLAG_ALL);

	if ((data->flags & WDT_FLAG_RESET_MASK) == WDT_FLAG_RESET_NONE) {
		sys_write32(cfg->int_src, HC32_INTC_SEL_ADDR(cfg->irq_num));
		irq_enable(cfg->irq_num);
	}

	ret = WDT_Init(&init);
	if (ret != LL_OK) {
		return -EIO;
	}

	WDT_FeedDog();
	data->started = true;

	return 0;
}

static int hc32_wdt_disable(const struct device *dev)
{
	struct hc32_wdt_data *data = dev->data;

	if (data->started) {
		return -EPERM;
	}

	data->timeout_installed = false;
	data->callback = NULL;
	data->flags = 0U;
	return 0;
}

static int hc32_wdt_install_timeout(const struct device *dev,
				    const struct wdt_timeout_cfg *cfg)
{
	struct hc32_wdt_data *data = dev->data;
	uint32_t pclk_hz;
	int ret;

	if (cfg->window.max == 0U || cfg->window.min > cfg->window.max) {
		return -EINVAL;
	}

	if ((cfg->flags & ~HC32_WDT_SUPPORTED_FLAGS) != 0U) {
		return -ENOTSUP;
	}

	if (data->started) {
		return -EBUSY;
	}

	if (data->timeout_installed) {
		return -ENOMEM;
	}

	if ((cfg->flags & WDT_FLAG_RESET_MASK) == WDT_FLAG_RESET_NONE) {
		if (cfg->callback == NULL) {
			return -ENOTSUP;
		}
	} else if (cfg->callback != NULL) {
		return -ENOTSUP;
	}

	ret = hc32_wdt_get_pclk3(dev, &pclk_hz);
	if (ret != 0) {
		return ret;
	}

	ret = hc32_wdt_choose_window(pclk_hz, cfg, &data->selection);
	if (ret != 0) {
		return ret;
	}

	data->callback = cfg->callback;
	data->flags = cfg->flags;
	data->timeout_installed = true;

	return 0;
}

static int hc32_wdt_feed(const struct device *dev, int channel_id)
{
	struct hc32_wdt_data *data = dev->data;

	ARG_UNUSED(dev);

	if (channel_id != 0) {
		return -EINVAL;
	}

	if (!data->started) {
		return -EFAULT;
	}

	WDT_FeedDog();
	return 0;
}

static DEVICE_API(wdt, hc32_wdt_api) = {
	.setup = hc32_wdt_setup,
	.disable = hc32_wdt_disable,
	.install_timeout = hc32_wdt_install_timeout,
	.feed = hc32_wdt_feed,
};

static int hc32_wdt_init(const struct device *dev)
{
	const struct hc32_wdt_config *cfg = dev->config;

	if (!device_is_ready(cfg->clock_dev)) {
		return -ENODEV;
	}

	cfg->irq_config_func(dev);
	irq_disable(cfg->irq_num);
	sys_write32(0U, HC32_INTC_SEL_ADDR(cfg->irq_num));
	(void)WDT_ClearStatus(WDT_FLAG_ALL);

	return 0;
}

#define HC32_WDT_INIT(inst)                                                       \
	static void hc32_wdt_irq_config_##inst(const struct device *dev)          \
	{                                                                       \
		ARG_UNUSED(dev);                                                \
		IRQ_CONNECT(DT_INST_IRQN(inst), DT_INST_IRQ(inst, priority),    \
			    hc32_wdt_isr, DEVICE_DT_INST_GET(inst), 0);          \
	}                                                                       \
	static struct hc32_wdt_data hc32_wdt_data_##inst;                        \
	static const struct hc32_wdt_config hc32_wdt_config_##inst = {           \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),           \
		.clock_id = HC32_CLOCK_ID(DT_INST_CLOCKS_CELL(inst, bus),        \
					  DT_INST_CLOCKS_CELL(inst, bit)),       \
		.int_src = DT_INST_PROP(inst, int_src),                          \
		.irq_num = DT_INST_IRQN(inst),                                   \
		.irq_config_func = hc32_wdt_irq_config_##inst,                   \
	};                                                                      \
	DEVICE_DT_INST_DEFINE(inst, hc32_wdt_init, NULL,                        \
			      &hc32_wdt_data_##inst,                           \
			      &hc32_wdt_config_##inst, POST_KERNEL,           \
			      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,            \
			      &hc32_wdt_api);

DT_INST_FOREACH_STATUS_OKAY(HC32_WDT_INIT)
