/*
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT hdsc_hc32_tmra_counter

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <soc.h>

#include <hc32_ll_tmra.h>

LOG_MODULE_REGISTER(counter_hc32_tmra, CONFIG_COUNTER_LOG_LEVEL);

#define HC32_TMRA_MAX_TOP       0xFFFFU
#define HC32_TMRA_NUM_CHANNELS  8U

#define HC32_CLOCK_ID(bus, bit) ((((bus) & 0xffU) << 8) | ((bit) & 0xffU))
#define HC32_INTC_SEL_ADDR(n)   ((uintptr_t)&CM_INTC->SEL0 + ((uintptr_t)(n) * sizeof(uint32_t)))

#define HC32_TMRA_DDL_DIV_1     TMRA_CLK_DIV1
#define HC32_TMRA_DDL_DIV_2     TMRA_CLK_DIV2
#define HC32_TMRA_DDL_DIV_4     TMRA_CLK_DIV4
#define HC32_TMRA_DDL_DIV_8     TMRA_CLK_DIV8
#define HC32_TMRA_DDL_DIV_16    TMRA_CLK_DIV16
#define HC32_TMRA_DDL_DIV_32    TMRA_CLK_DIV32
#define HC32_TMRA_DDL_DIV_64    TMRA_CLK_DIV64
#define HC32_TMRA_DDL_DIV_128   TMRA_CLK_DIV128
#define HC32_TMRA_DDL_DIV_256   TMRA_CLK_DIV256
#define HC32_TMRA_DDL_DIV_512   TMRA_CLK_DIV512
#define HC32_TMRA_DDL_DIV_1024  TMRA_CLK_DIV1024
#define HC32_TMRA_DDL_DIV(div)  UTIL_CAT(HC32_TMRA_DDL_DIV_, div)

struct hc32_tmra_alarm_data {
	counter_alarm_callback_t callback;
	void *user_data;
};

struct hc32_tmra_data {
	counter_top_callback_t top_cb;
	void *top_user_data;
	struct hc32_tmra_alarm_data alarms[HC32_TMRA_NUM_CHANNELS];
	atomic_t sw_pending;
	uint32_t freq;
	uint32_t guard_period;
};

struct hc32_tmra_config {
	struct counter_config_info info;
	CM_TMRA_TypeDef *tmra;
	uint16_t clock_id;
	const struct device *clock_dev;
	uint16_t clock_div;
	uint16_t prescaler;
	uint16_t int_src_ovf;
	uint16_t int_src_cmp;
	uint8_t ovf_irq;
	uint8_t cmp_irq;
	uint8_t ovf_prio;
	uint8_t cmp_prio;
	void (*irq_config_func)(const struct device *dev);
};

static const uint32_t hc32_tmra_compare_ints[HC32_TMRA_NUM_CHANNELS] = {
	TMRA_INT_CMP_CH1,
	TMRA_INT_CMP_CH2,
	TMRA_INT_CMP_CH3,
	TMRA_INT_CMP_CH4,
	TMRA_INT_CMP_CH5,
	TMRA_INT_CMP_CH6,
	TMRA_INT_CMP_CH7,
	TMRA_INT_CMP_CH8,
};

static const uint32_t hc32_tmra_compare_flags[HC32_TMRA_NUM_CHANNELS] = {
	TMRA_FLAG_CMP_CH1,
	TMRA_FLAG_CMP_CH2,
	TMRA_FLAG_CMP_CH3,
	TMRA_FLAG_CMP_CH4,
	TMRA_FLAG_CMP_CH5,
	TMRA_FLAG_CMP_CH6,
	TMRA_FLAG_CMP_CH7,
	TMRA_FLAG_CMP_CH8,
};

static uint32_t hc32_tmra_get_top_value(const struct device *dev)
{
	const struct hc32_tmra_config *cfg = dev->config;

	return TMRA_GetPeriodValue(cfg->tmra);
}

static int hc32_tmra_start(const struct device *dev)
{
	const struct hc32_tmra_config *cfg = dev->config;

	TMRA_Start(cfg->tmra);
	return 0;
}

static int hc32_tmra_stop(const struct device *dev)
{
	const struct hc32_tmra_config *cfg = dev->config;

	TMRA_Stop(cfg->tmra);
	return 0;
}

static int hc32_tmra_get_value(const struct device *dev, uint32_t *ticks)
{
	const struct hc32_tmra_config *cfg = dev->config;

	if (ticks == NULL) {
		return -EINVAL;
	}

	*ticks = TMRA_GetCountValue(cfg->tmra);
	return 0;
}

static int hc32_tmra_reset(const struct device *dev)
{
	const struct hc32_tmra_config *cfg = dev->config;

	TMRA_SetCountValue(cfg->tmra, 0U);
	return 0;
}

static int hc32_tmra_set_value(const struct device *dev, uint32_t ticks)
{
	const struct hc32_tmra_config *cfg = dev->config;

	if (ticks > hc32_tmra_get_top_value(dev)) {
		return -EINVAL;
	}

	TMRA_SetCountValue(cfg->tmra, ticks);
	return 0;
}

static uint32_t hc32_tmra_ticks_add(uint32_t val1, uint32_t val2, uint32_t top)
{
	uint32_t to_top;

	if (likely(IS_BIT_MASK(top))) {
		return (val1 + val2) & top;
	}

	to_top = top - val1;
	return (val2 <= to_top) ? (val1 + val2) : (val2 - to_top - 1U);
}

static uint32_t hc32_tmra_ticks_sub(uint32_t val, uint32_t old, uint32_t top)
{
	if (likely(IS_BIT_MASK(top))) {
		return (val - old) & top;
	}

	return (val >= old) ? (val - old) : (val + top + 1U - old);
}

static uint32_t hc32_tmra_ticks_dec(uint32_t val, uint32_t top)
{
	return (val == 0U) ? top : (val - 1U);
}

static void hc32_tmra_set_pending_alarm(const struct device *dev, uint8_t chan)
{
	const struct hc32_tmra_config *cfg = dev->config;
	struct hc32_tmra_data *data = dev->data;

	atomic_or(&data->sw_pending, BIT(chan));
	NVIC_SetPendingIRQ((IRQn_Type)cfg->cmp_irq);
}

static int hc32_tmra_program_alarm_locked(const struct device *dev, uint8_t chan,
					  const struct counter_alarm_cfg *alarm_cfg)
{
	const struct hc32_tmra_config *cfg = dev->config;
	struct hc32_tmra_data *data = dev->data;
	uint32_t top = hc32_tmra_get_top_value(dev);
	uint32_t val = alarm_cfg->ticks;
	uint32_t flags = alarm_cfg->flags;
	bool absolute = (flags & COUNTER_ALARM_CFG_ABSOLUTE) != 0U;
	bool irq_on_late;
	uint32_t max_rel_val;
	uint32_t now;
	uint32_t diff;
	int err = 0;

	now = TMRA_GetCountValue(cfg->tmra);

	TMRA_IntCmd(cfg->tmra, hc32_tmra_compare_ints[chan], DISABLE);
	TMRA_SetCompareValue(cfg->tmra, chan, now);
	TMRA_ClearStatus(cfg->tmra, hc32_tmra_compare_flags[chan]);

	if (absolute) {
		max_rel_val = top - data->guard_period;
		irq_on_late = (flags & COUNTER_ALARM_CFG_EXPIRE_WHEN_LATE) != 0U;
	} else {
		irq_on_late = val < (top / 2U);
		max_rel_val = irq_on_late ? (top / 2U) : top;
		val = hc32_tmra_ticks_add(now, val, top);
	}

	TMRA_SetCompareValue(cfg->tmra, chan, val);
	diff = hc32_tmra_ticks_sub(hc32_tmra_ticks_dec(val, top),
				   TMRA_GetCountValue(cfg->tmra), top);

	if (diff > max_rel_val) {
		err = -ETIME;
		if (irq_on_late) {
			hc32_tmra_set_pending_alarm(dev, chan);
		} else {
			data->alarms[chan].callback = NULL;
			data->alarms[chan].user_data = NULL;
		}
	} else {
		TMRA_ClearStatus(cfg->tmra, hc32_tmra_compare_flags[chan]);
		TMRA_IntCmd(cfg->tmra, hc32_tmra_compare_ints[chan], ENABLE);
	}

	return err;
}

static int hc32_tmra_set_alarm(const struct device *dev, uint8_t chan,
			       const struct counter_alarm_cfg *alarm_cfg)
{
	struct hc32_tmra_data *data = dev->data;
	unsigned int key;
	int err;

	if ((alarm_cfg == NULL) || (alarm_cfg->callback == NULL)) {
		return -EINVAL;
	}

	if (alarm_cfg->ticks > hc32_tmra_get_top_value(dev)) {
		return -EINVAL;
	}

	key = irq_lock();

	if (data->alarms[chan].callback != NULL) {
		irq_unlock(key);
		return -EBUSY;
	}

	data->alarms[chan].callback = alarm_cfg->callback;
	data->alarms[chan].user_data = alarm_cfg->user_data;
	err = hc32_tmra_program_alarm_locked(dev, chan, alarm_cfg);

	irq_unlock(key);
	return err;
}

static int hc32_tmra_cancel_alarm(const struct device *dev, uint8_t chan)
{
	const struct hc32_tmra_config *cfg = dev->config;
	struct hc32_tmra_data *data = dev->data;
	unsigned int key;

	key = irq_lock();
	TMRA_IntCmd(cfg->tmra, hc32_tmra_compare_ints[chan], DISABLE);
	TMRA_ClearStatus(cfg->tmra, hc32_tmra_compare_flags[chan]);
	atomic_and(&data->sw_pending, ~BIT(chan));
	data->alarms[chan].callback = NULL;
	data->alarms[chan].user_data = NULL;
	irq_unlock(key);

	return 0;
}

static int hc32_tmra_set_top_value(const struct device *dev,
				   const struct counter_top_cfg *top_cfg)
{
	const struct hc32_tmra_config *cfg = dev->config;
	struct hc32_tmra_data *data = dev->data;
	unsigned int key;
	uint32_t now;
	int err = 0;

	if (top_cfg == NULL) {
		return -EINVAL;
	}

	key = irq_lock();

	for (uint8_t i = 0U; i < HC32_TMRA_NUM_CHANNELS; i++) {
		if (data->alarms[i].callback != NULL) {
			irq_unlock(key);
			return -EBUSY;
		}
	}

	TMRA_IntCmd(cfg->tmra, TMRA_INT_OVF, DISABLE);
	TMRA_ClearStatus(cfg->tmra, TMRA_FLAG_OVF);

	now = TMRA_GetCountValue(cfg->tmra);
	TMRA_SetPeriodValue(cfg->tmra, top_cfg->ticks);
	data->top_cb = top_cfg->callback;
	data->top_user_data = top_cfg->user_data;

	if ((top_cfg->flags & COUNTER_TOP_CFG_DONT_RESET) == 0U) {
		TMRA_SetCountValue(cfg->tmra, 0U);
	} else if (now >= top_cfg->ticks) {
		err = -ETIME;
		if ((top_cfg->flags & COUNTER_TOP_CFG_RESET_WHEN_LATE) != 0U) {
			TMRA_SetCountValue(cfg->tmra, 0U);
		}
	}

	if (top_cfg->callback != NULL) {
		TMRA_ClearStatus(cfg->tmra, TMRA_FLAG_OVF);
		TMRA_IntCmd(cfg->tmra, TMRA_INT_OVF, ENABLE);
	}

	irq_unlock(key);
	return err;
}

static uint32_t hc32_tmra_get_pending_int(const struct device *dev)
{
	const struct hc32_tmra_config *cfg = dev->config;
	const struct hc32_tmra_data *data = dev->data;

	if (atomic_get(&data->sw_pending) != 0) {
		return 1U;
	}

	if (NVIC_GetPendingIRQ((IRQn_Type)cfg->ovf_irq) != 0 ||
	    NVIC_GetPendingIRQ((IRQn_Type)cfg->cmp_irq) != 0) {
		return 1U;
	}

	if ((data->top_cb != NULL) &&
	    (TMRA_GetStatus(cfg->tmra, TMRA_FLAG_OVF) == SET)) {
		return 1U;
	}

	for (uint8_t i = 0U; i < HC32_TMRA_NUM_CHANNELS; i++) {
		if ((data->alarms[i].callback != NULL) &&
		    (TMRA_GetStatus(cfg->tmra, hc32_tmra_compare_flags[i]) == SET)) {
			return 1U;
		}
	}

	return 0U;
}

static uint32_t hc32_tmra_get_guard_period(const struct device *dev, uint32_t flags)
{
	const struct hc32_tmra_data *data = dev->data;

	ARG_UNUSED(flags);
	return data->guard_period;
}

static int hc32_tmra_set_guard_period(const struct device *dev, uint32_t guard,
				      uint32_t flags)
{
	struct hc32_tmra_data *data = dev->data;

	ARG_UNUSED(flags);

	if (guard >= hc32_tmra_get_top_value(dev)) {
		return -EINVAL;
	}

	data->guard_period = guard;
	return 0;
}

static uint32_t hc32_tmra_get_freq(const struct device *dev)
{
	const struct hc32_tmra_data *data = dev->data;

	return data->freq;
}

static void hc32_tmra_alarm_irq_handle(const struct device *dev, uint8_t chan)
{
	const struct hc32_tmra_config *cfg = dev->config;
	struct hc32_tmra_data *data = dev->data;
	counter_alarm_callback_t cb;
	void *user_data;
	uint32_t ticks;

	TMRA_IntCmd(cfg->tmra, hc32_tmra_compare_ints[chan], DISABLE);
	atomic_and(&data->sw_pending, ~BIT(chan));

	cb = data->alarms[chan].callback;
	user_data = data->alarms[chan].user_data;
	ticks = TMRA_GetCompareValue(cfg->tmra, chan);
	data->alarms[chan].callback = NULL;
	data->alarms[chan].user_data = NULL;

	if (cb != NULL) {
		cb(dev, chan, ticks, user_data);
	}
}

static void hc32_tmra_ovf_isr(const void *arg)
{
	const struct device *dev = arg;
	const struct hc32_tmra_config *cfg = dev->config;
	struct hc32_tmra_data *data = dev->data;
	counter_top_callback_t cb = data->top_cb;

	TMRA_ClearStatus(cfg->tmra, TMRA_FLAG_OVF);

	if (cb != NULL) {
		cb(dev, data->top_user_data);
	}
}

static void hc32_tmra_cmp_isr(const void *arg)
{
	const struct device *dev = arg;
	const struct hc32_tmra_config *cfg = dev->config;
	struct hc32_tmra_data *data = dev->data;
	atomic_val_t sw_pending = atomic_get(&data->sw_pending);

	for (uint8_t i = 0U; i < HC32_TMRA_NUM_CHANNELS; i++) {
		bool hw_pending = TMRA_GetStatus(cfg->tmra, hc32_tmra_compare_flags[i]) == SET;
		bool sw_alarm = (sw_pending & BIT(i)) != 0;

		if (!hw_pending && !sw_alarm) {
			continue;
		}

		if (hw_pending) {
			TMRA_ClearStatus(cfg->tmra, hc32_tmra_compare_flags[i]);
		}

		hc32_tmra_alarm_irq_handle(dev, i);
	}
}

static int hc32_tmra_init(const struct device *dev)
{
	const struct hc32_tmra_config *cfg = dev->config;
	struct hc32_tmra_data *data = dev->data;
	stc_tmra_init_t tmra_init;
	int ret;
	uint32_t pclk_hz;

	if (!device_is_ready(cfg->clock_dev)) {
		LOG_ERR("clock device not ready");
		return -ENODEV;
	}

	ret = clock_control_on(cfg->clock_dev,
			       (clock_control_subsys_t)(uintptr_t)cfg->clock_id);
	if (ret < 0) {
		LOG_ERR("clock enable failed (%d)", ret);
		return ret;
	}

	ret = clock_control_get_rate(cfg->clock_dev,
				     (clock_control_subsys_t)(uintptr_t)cfg->clock_id,
				     &pclk_hz);
	if (ret < 0) {
		LOG_ERR("clock rate failed (%d)", ret);
		return ret;
	}

	data->freq = pclk_hz / cfg->prescaler;
	data->guard_period = 0U;
	atomic_clear(&data->sw_pending);

	(void)TMRA_StructInit(&tmra_init);
	tmra_init.u8CountSrc = TMRA_CNT_SRC_SW;
	tmra_init.sw_count.u8ClockDiv = cfg->clock_div;
	tmra_init.sw_count.u8CountMode = TMRA_MD_SAWTOOTH;
	tmra_init.sw_count.u8CountDir = TMRA_DIR_UP;
	tmra_init.u32PeriodValue = HC32_TMRA_MAX_TOP;

	if (TMRA_Init(cfg->tmra, &tmra_init) != LL_OK) {
		LOG_ERR("TMRA init failed");
		return -EIO;
	}

	for (uint8_t i = 0U; i < HC32_TMRA_NUM_CHANNELS; i++) {
		TMRA_SetFunc(cfg->tmra, i, TMRA_FUNC_CMP);
		TMRA_IntCmd(cfg->tmra, hc32_tmra_compare_ints[i], DISABLE);
		TMRA_SetCompareValue(cfg->tmra, i, HC32_TMRA_MAX_TOP);
	}

	TMRA_IntCmd(cfg->tmra, TMRA_INT_OVF, DISABLE);
	TMRA_ClearStatus(cfg->tmra, TMRA_FLAG_ALL);
	TMRA_Stop(cfg->tmra);

	cfg->irq_config_func(dev);

	return 0;
}

static DEVICE_API(counter, hc32_tmra_api) = {
	.start = hc32_tmra_start,
	.stop = hc32_tmra_stop,
	.get_value = hc32_tmra_get_value,
	.reset = hc32_tmra_reset,
	.set_value = hc32_tmra_set_value,
	.set_alarm = hc32_tmra_set_alarm,
	.cancel_alarm = hc32_tmra_cancel_alarm,
	.set_top_value = hc32_tmra_set_top_value,
	.get_pending_int = hc32_tmra_get_pending_int,
	.get_top_value = hc32_tmra_get_top_value,
	.get_guard_period = hc32_tmra_get_guard_period,
	.set_guard_period = hc32_tmra_set_guard_period,
	.get_freq = hc32_tmra_get_freq,
};

#define HC32_TMRA_IRQ_CONFIG(inst)							\
	static void hc32_tmra_irq_config_##inst(const struct device *dev)		\
	{										\
		const struct hc32_tmra_config *cfg = dev->config;			\
											\
		sys_write32(cfg->int_src_ovf, HC32_INTC_SEL_ADDR(cfg->ovf_irq));	\
		sys_write32(cfg->int_src_cmp, HC32_INTC_SEL_ADDR(cfg->cmp_irq));	\
		irq_connect_dynamic(cfg->ovf_irq, cfg->ovf_prio,			\
				    hc32_tmra_ovf_isr, dev, 0);			\
		irq_connect_dynamic(cfg->cmp_irq, cfg->cmp_prio,			\
				    hc32_tmra_cmp_isr, dev, 0);			\
		irq_enable(cfg->ovf_irq);						\
		irq_enable(cfg->cmp_irq);						\
	}

#define HC32_TMRA_COUNTER_INIT(inst)							\
	BUILD_ASSERT(DT_INST_PROP(inst, clock_prescaler) == 1 ||			\
		     DT_INST_PROP(inst, clock_prescaler) == 2 ||			\
		     DT_INST_PROP(inst, clock_prescaler) == 4 ||			\
		     DT_INST_PROP(inst, clock_prescaler) == 8 ||			\
		     DT_INST_PROP(inst, clock_prescaler) == 16 ||			\
		     DT_INST_PROP(inst, clock_prescaler) == 32 ||			\
		     DT_INST_PROP(inst, clock_prescaler) == 64 ||			\
		     DT_INST_PROP(inst, clock_prescaler) == 128 ||			\
		     DT_INST_PROP(inst, clock_prescaler) == 256 ||			\
		     DT_INST_PROP(inst, clock_prescaler) == 512 ||			\
		     DT_INST_PROP(inst, clock_prescaler) == 1024,			\
		     "Unsupported TMRA prescaler");					\
	BUILD_ASSERT(DT_INST_IRQ_HAS_NAME(inst, ovf), "TMRA requires ovf IRQ");	\
	BUILD_ASSERT(DT_INST_IRQ_HAS_NAME(inst, cmp), "TMRA requires cmp IRQ");	\
											\
	static struct hc32_tmra_data hc32_tmra_data_##inst;				\
	HC32_TMRA_IRQ_CONFIG(inst);							\
	static const struct hc32_tmra_config hc32_tmra_cfg_##inst = {			\
		.info = {								\
			.max_top_value = HC32_TMRA_MAX_TOP,				\
			.flags = COUNTER_CONFIG_INFO_COUNT_UP,			\
			.channels = HC32_TMRA_NUM_CHANNELS,				\
		},									\
		.tmra = (CM_TMRA_TypeDef *)DT_INST_REG_ADDR(inst),			\
		.clock_id = HC32_CLOCK_ID(DT_INST_CLOCKS_CELL(inst, bus),		\
					  DT_INST_CLOCKS_CELL(inst, bit)),		\
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),		\
		.clock_div = HC32_TMRA_DDL_DIV(DT_INST_PROP(inst, clock_prescaler)),	\
		.prescaler = DT_INST_PROP(inst, clock_prescaler),			\
		.int_src_ovf = DT_INST_PROP(inst, int_src_ovf),			\
		.int_src_cmp = DT_INST_PROP(inst, int_src_cmp),			\
		.ovf_irq = DT_INST_IRQ_BY_NAME(inst, ovf, irq),			\
		.cmp_irq = DT_INST_IRQ_BY_NAME(inst, cmp, irq),			\
		.ovf_prio = DT_INST_IRQ_BY_NAME(inst, ovf, priority),		\
		.cmp_prio = DT_INST_IRQ_BY_NAME(inst, cmp, priority),		\
		.irq_config_func = hc32_tmra_irq_config_##inst,			\
	};										\
	DEVICE_DT_INST_DEFINE(inst, hc32_tmra_init, NULL,				\
			      &hc32_tmra_data_##inst, &hc32_tmra_cfg_##inst,		\
			      PRE_KERNEL_1, CONFIG_COUNTER_INIT_PRIORITY,		\
			      &hc32_tmra_api);

DT_INST_FOREACH_STATUS_OKAY(HC32_TMRA_COUNTER_INIT)
