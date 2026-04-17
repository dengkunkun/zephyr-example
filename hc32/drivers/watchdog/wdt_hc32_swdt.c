/*
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT hdsc_hc32_swdt

#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>
#include <soc.h>

#include <hc32_ll_swdt.h>

LOG_MODULE_REGISTER(wdt_hc32_swdt, CONFIG_WDT_LOG_LEVEL);

#define HC32_INTC_SEL_ADDR(n) ((uintptr_t)&CM_INTC->SEL0 + ((uintptr_t)(n) * sizeof(uint32_t)))
#define HC32_SWDT_SUPPORTED_FLAGS (WDT_FLAG_RESET_NONE | WDT_FLAG_RESET_SOC)

struct hc32_swdt_config {
	uint16_t int_src;
	uint8_t irq_num;
	uint32_t timeout_ms;
	bool reset_on_timeout;
	bool run_in_sleep;
	void (*irq_config_func)(const struct device *dev);
};

struct hc32_swdt_data {
	wdt_callback_t callback;
	bool timeout_installed;
	bool started;
	uint8_t flags;
};

static void hc32_swdt_configure_debug_pause(bool enabled)
{
	if (enabled) {
		CM_DBGC->MCUSTPCTL |= DBGC_MCUSTPCTL_SWDTSTP;
	} else {
		CM_DBGC->MCUSTPCTL &= ~DBGC_MCUSTPCTL_SWDTSTP;
	}
}

static void hc32_swdt_isr(const struct device *dev)
{
	struct hc32_swdt_data *data = dev->data;
	uint32_t flags = 0U;

	if (SWDT_GetStatus(SWDT_FLAG_UDF) == SET) {
		flags |= SWDT_FLAG_UDF;
	}
	if (SWDT_GetStatus(SWDT_FLAG_REFRESH) == SET) {
		flags |= SWDT_FLAG_REFRESH;
	}

	if (flags != 0U) {
		(void)SWDT_ClearStatus(flags);
	}

	if (data->callback != NULL) {
		data->callback(dev, 0);
	}
}

static int hc32_swdt_setup(const struct device *dev, uint8_t options)
{
	const struct hc32_swdt_config *cfg = dev->config;
	struct hc32_swdt_data *data = dev->data;
	bool pause_in_sleep = (options & WDT_OPT_PAUSE_IN_SLEEP) != 0U;
	bool actual_pause_in_sleep = !cfg->run_in_sleep;

	if (!data->timeout_installed) {
		return -EINVAL;
	}

	if (data->started) {
		return -EBUSY;
	}

	if (pause_in_sleep != actual_pause_in_sleep) {
		return -ENOTSUP;
	}

	hc32_swdt_configure_debug_pause((options & WDT_OPT_PAUSE_HALTED_BY_DBG) != 0U);

	irq_disable(cfg->irq_num);
	sys_write32(0U, HC32_INTC_SEL_ADDR(cfg->irq_num));
	NVIC_ClearPendingIRQ((IRQn_Type)cfg->irq_num);
	(void)SWDT_ClearStatus(SWDT_FLAG_ALL);

	if (!cfg->reset_on_timeout) {
		sys_write32(cfg->int_src, HC32_INTC_SEL_ADDR(cfg->irq_num));
		irq_enable(cfg->irq_num);
	}

	SWDT_FeedDog();
	data->started = true;

	return 0;
}

static int hc32_swdt_disable(const struct device *dev)
{
	ARG_UNUSED(dev);

	return -EPERM;
}

static int hc32_swdt_install_timeout(const struct device *dev,
				     const struct wdt_timeout_cfg *cfg)
{
	const struct hc32_swdt_config *config = dev->config;
	struct hc32_swdt_data *data = dev->data;

	if (cfg->window.min != 0U || cfg->window.max == 0U) {
		return -ENOTSUP;
	}

	if ((cfg->flags & ~HC32_SWDT_SUPPORTED_FLAGS) != 0U) {
		return -ENOTSUP;
	}

	if (data->started) {
		return -EBUSY;
	}

	if (data->timeout_installed) {
		return -ENOMEM;
	}

	if (cfg->window.max != config->timeout_ms) {
		return -EINVAL;
	}

	if (config->reset_on_timeout) {
		if (cfg->callback != NULL ||
		    (cfg->flags & WDT_FLAG_RESET_MASK) != WDT_FLAG_RESET_SOC) {
			return -ENOTSUP;
		}
	} else {
		if (cfg->callback == NULL ||
		    (cfg->flags & WDT_FLAG_RESET_MASK) != WDT_FLAG_RESET_NONE) {
			return -ENOTSUP;
		}
		data->callback = cfg->callback;
	}

	data->flags = cfg->flags;
	data->timeout_installed = true;
	return 0;
}

static int hc32_swdt_feed(const struct device *dev, int channel_id)
{
	struct hc32_swdt_data *data = dev->data;

	ARG_UNUSED(dev);

	if (channel_id != 0) {
		return -EINVAL;
	}

	if (!data->started) {
		return -EFAULT;
	}

	SWDT_FeedDog();
	return 0;
}

static DEVICE_API(wdt, hc32_swdt_api) = {
	.setup = hc32_swdt_setup,
	.disable = hc32_swdt_disable,
	.install_timeout = hc32_swdt_install_timeout,
	.feed = hc32_swdt_feed,
};

static int hc32_swdt_init(const struct device *dev)
{
	const struct hc32_swdt_config *cfg = dev->config;

	cfg->irq_config_func(dev);
	irq_disable(cfg->irq_num);
	sys_write32(0U, HC32_INTC_SEL_ADDR(cfg->irq_num));
	(void)SWDT_ClearStatus(SWDT_FLAG_ALL);

	return 0;
}

#define HC32_SWDT_TIMEOUT_US(inst)                                              \
	(((uint64_t)DT_INST_PROP(inst, counter_cycles) *                        \
	  (uint64_t)DT_INST_PROP(inst, clock_divider) * 1000000ULL) / SWDTLRC_VALUE)

#define HC32_SWDT_INIT(inst)                                                      \
	static void hc32_swdt_irq_config_##inst(const struct device *dev)        \
	{                                                                       \
		ARG_UNUSED(dev);                                                \
		IRQ_CONNECT(DT_INST_IRQN(inst), DT_INST_IRQ(inst, priority),    \
			    hc32_swdt_isr, DEVICE_DT_INST_GET(inst), 0);         \
	}                                                                       \
	static struct hc32_swdt_data hc32_swdt_data_##inst;                      \
	static const struct hc32_swdt_config hc32_swdt_config_##inst = {         \
		.int_src = DT_INST_PROP(inst, int_src),                          \
		.irq_num = DT_INST_IRQN(inst),                                   \
		.timeout_ms = (uint32_t)((HC32_SWDT_TIMEOUT_US(inst) + 999ULL) / 1000ULL), \
		.reset_on_timeout = DT_INST_PROP(inst, reset_on_timeout),        \
		.run_in_sleep = DT_INST_PROP(inst, run_in_sleep),                \
		.irq_config_func = hc32_swdt_irq_config_##inst,                  \
	};                                                                      \
	DEVICE_DT_INST_DEFINE(inst, hc32_swdt_init, NULL,                       \
			      &hc32_swdt_data_##inst,                          \
			      &hc32_swdt_config_##inst, POST_KERNEL,          \
			      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,           \
			      &hc32_swdt_api);

DT_INST_FOREACH_STATUS_OKAY(HC32_SWDT_INIT)
