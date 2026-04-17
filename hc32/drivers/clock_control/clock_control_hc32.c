/*
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT hdsc_hc32_clock

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/timer/system_timer.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <hc32_clock.h>
#include <soc.h>

#include <hc32_ll_clk.h>
#include <hc32_ll_efm.h>
#include <hc32_ll_fcg.h>
#include <hc32_ll_gpio.h>
#include <hc32_ll_pwc.h>
#include <hc32_ll_sram.h>

enum hc32_fcg_bus {
	HC32_FCG_BUS0 = 0,
	HC32_FCG_BUS1 = 1,
	HC32_FCG_BUS2 = 2,
	HC32_FCG_BUS3 = 3,
};

enum hc32_clock_rate_sel {
	HC32_CLOCK_RATE_SYSCLK = 0x10,
	HC32_CLOCK_RATE_HCLK = 0x11,
	HC32_CLOCK_RATE_PCLK0 = 0x12,
	HC32_CLOCK_RATE_PCLK1 = 0x13,
	HC32_CLOCK_RATE_PCLK2 = 0x14,
	HC32_CLOCK_RATE_PCLK3 = 0x15,
	HC32_CLOCK_RATE_PCLK4 = 0x16,
	HC32_CLOCK_RATE_EXCLK = 0x17,
};

#define HC32_CLOCK_ID(bus, bit)  ((((bus) & 0xffU) << 8) | ((bit) & 0xffU))
#define HC32_CLOCK_BUS(id)       (((id) >> 8) & 0xffU)
#define HC32_CLOCK_BIT(id)       ((id) & 0xffU)

struct hc32_clock_config {
	uintptr_t base;
	uint32_t sysclk_hz;
	uint32_t hxt_hz;
	uint32_t lxt_hz;
	uint32_t mrc_hz;
	uint8_t pll_m;
	uint8_t pll_n;
	uint8_t pll_p;
	uint8_t pll_q;
	uint8_t pll_r;
};

struct hc32_clock_data {
	stc_clock_freq_t freqs;
};

static K_MUTEX_DEFINE(hc32_clock_lock);

static inline uint32_t hc32_gate_mask(uint32_t id)
{
	return BIT(HC32_CLOCK_BIT(id));
}

static int hc32_clock_refresh_rates(const struct device *dev)
{
	struct hc32_clock_data *data = dev->data;
	int ret;

	SystemCoreClockUpdate();
	ret = CLK_GetClockFreq(&data->freqs);
	if (ret != LL_OK) {
		return -EIO;
	}

	return 0;
}

static void hc32_clock_gate_cmd(uint8_t bus, uint32_t mask, en_functional_state_t state)
{
	switch (bus) {
	case HC32_FCG_BUS0:
		FCG_Fcg0PeriphClockCmd(mask, state);
		break;
	case HC32_FCG_BUS1:
		FCG_Fcg1PeriphClockCmd(mask, state);
		break;
	case HC32_FCG_BUS2:
		FCG_Fcg2PeriphClockCmd(mask, state);
		break;
	case HC32_FCG_BUS3:
		FCG_Fcg3PeriphClockCmd(mask, state);
		break;
	default:
		break;
	}
}

static int hc32_clock_on(const struct device *dev, clock_control_subsys_t sys)
{
	uint32_t id = (uint32_t)(uintptr_t)sys;
	uint8_t bus = HC32_CLOCK_BUS(id);

	if (bus > HC32_FCG_BUS3) {
		return -EINVAL;
	}

	hc32_clock_gate_cmd(bus, hc32_gate_mask(id), ENABLE);
	return 0;
}

static int hc32_clock_off(const struct device *dev, clock_control_subsys_t sys)
{
	uint32_t id = (uint32_t)(uintptr_t)sys;
	uint8_t bus = HC32_CLOCK_BUS(id);

	if (bus > HC32_FCG_BUS3) {
		return -EINVAL;
	}

	hc32_clock_gate_cmd(bus, hc32_gate_mask(id), DISABLE);
	return 0;
}

static uint32_t hc32_clock_rate_for_selector(const struct hc32_clock_data *data,
					     uint8_t selector)
{
	switch (selector) {
	case HC32_FCG_BUS0:
	case HC32_CLOCK_RATE_HCLK:
		return data->freqs.u32HclkFreq;
	case HC32_FCG_BUS1:
	case HC32_FCG_BUS2:
	case HC32_CLOCK_RATE_PCLK1:
		return data->freqs.u32Pclk1Freq;
	case HC32_FCG_BUS3:
	case HC32_CLOCK_RATE_PCLK4:
		return data->freqs.u32Pclk4Freq;
	case HC32_CLOCK_RATE_SYSCLK:
		return data->freqs.u32SysclkFreq;
	case HC32_CLOCK_RATE_PCLK0:
		return data->freqs.u32Pclk0Freq;
	case HC32_CLOCK_RATE_PCLK2:
		return data->freqs.u32Pclk2Freq;
	case HC32_CLOCK_RATE_PCLK3:
		return data->freqs.u32Pclk3Freq;
	case HC32_CLOCK_RATE_EXCLK:
		return data->freqs.u32ExclkFreq;
	default:
		return 0U;
	}
}

static int hc32_clock_get_rate(const struct device *dev, clock_control_subsys_t sys,
			       uint32_t *rate)
{
	const struct hc32_clock_data *data = dev->data;

	if (rate == NULL) {
		return -EINVAL;
	}

	if (sys == CLOCK_CONTROL_SUBSYS_ALL) {
		*rate = data->freqs.u32SysclkFreq;
		return 0;
	}

	*rate = hc32_clock_rate_for_selector(data,
					     HC32_CLOCK_BUS((uint32_t)(uintptr_t)sys));
	return (*rate == 0U) ? -EINVAL : 0;
}

static const struct device *hc32_clock_device(void)
{
	return DEVICE_DT_GET(DT_NODELABEL(clk));
}

static void hc32_clock_publish_system_timer_hz(void)
{
#if defined(CONFIG_CORTEX_M_SYSTICK) && defined(CONFIG_SYSTEM_CLOCK_HW_CYCLES_PER_SEC_RUNTIME_UPDATE)
	z_sys_clock_hw_cycles_per_sec_update((uint32_t)SystemCoreClock);
#endif
}

static int hc32_clock_enable_hxt(const struct hc32_clock_config *cfg)
{
	stc_clock_xtal_init_t xtal_cfg;

	GPIO_AnalogCmd(GPIO_PORT_C, GPIO_PIN_00 | GPIO_PIN_01, ENABLE);

	(void)CLK_XtalStructInit(&xtal_cfg);
	xtal_cfg.u8State = CLK_XTAL_ON;
	xtal_cfg.u8Mode = CLK_XTAL_MD_OSC;
	xtal_cfg.u8Drv = (cfg->hxt_hz >= 20000000U) ? CLK_XTAL_DRV_HIGH : CLK_XTAL_DRV_ULOW;
	xtal_cfg.u8StableTime = CLK_XTAL_STB_2MS;

	if (CLK_XtalInit(&xtal_cfg) != LL_OK) {
		return -EIO;
	}

	while (CLK_GetStableStatus(CLK_STB_FLAG_XTAL) != SET) {
	}

	return 0;
}

static void hc32_clock_enable_lxt(const struct hc32_clock_config *cfg)
{
	stc_clock_xtal32_init_t xtal32_cfg;

	if (cfg->lxt_hz == 0U) {
		return;
	}

	GPIO_AnalogCmd(GPIO_PORT_C, GPIO_PIN_14 | GPIO_PIN_15, ENABLE);
	xtal32_cfg.u8State = CLK_XTAL32_ON;
	xtal32_cfg.u8Drv = CLK_XTAL32_DRV_MID;
	xtal32_cfg.u8Filter = CLK_XTAL32_FILTER_ALL_MD;
	(void)CLK_Xtal32Init(&xtal32_cfg);
}

static int hc32_clock_enable_pll(const struct hc32_clock_config *cfg)
{
	stc_clock_pll_init_t pll_cfg;

	(void)CLK_PLLStructInit(&pll_cfg);
	pll_cfg.u8PLLState = CLK_PLL_ON;
	pll_cfg.PLLCFGR = 0U;
	pll_cfg.PLLCFGR_f.PLLM = cfg->pll_m - 1U;
	pll_cfg.PLLCFGR_f.PLLN = cfg->pll_n - 1U;
	pll_cfg.PLLCFGR_f.PLLP = cfg->pll_p - 1U;
	pll_cfg.PLLCFGR_f.PLLQ = cfg->pll_q - 1U;
	pll_cfg.PLLCFGR_f.PLLR = cfg->pll_r - 1U;
	pll_cfg.PLLCFGR_f.PLLSRC = CLK_PLL_SRC_XTAL;

	if (CLK_PLLInit(&pll_cfg) != LL_OK) {
		return -EIO;
	}

	while (CLK_GetStableStatus(CLK_STB_FLAG_PLL) != SET) {
	}

	return 0;
}

static int hc32_clock_apply_low_tree(const struct device *dev)
{
	const struct hc32_clock_config *cfg = dev->config;

	if ((CM_CMU->CKSWR & CMU_CKSWR_CKSW) == CLK_SYSCLK_SRC_MRC) {
		return hc32_clock_refresh_rates(dev);
	}

	if (CLK_MrcCmd(ENABLE) != LL_OK) {
		return -EIO;
	}

	CLK_SetSysClockSrc(CLK_SYSCLK_SRC_MRC);

#if defined(HC32F460)
	if (PWC_HighPerformanceToHighSpeed() != LL_OK) {
		return -EIO;
	}
#elif defined(HC32F4A0)
	if (PWC_HighSpeedToLowSpeed() != LL_OK) {
		return -EIO;
	}
#endif

	CLK_SetClockDiv(CLK_BUS_CLK_ALL,
		       CLK_HCLK_DIV1 | CLK_EXCLK_DIV1 | CLK_PCLK0_DIV1 |
		       CLK_PCLK1_DIV1 | CLK_PCLK2_DIV1 | CLK_PCLK3_DIV1 |
		       CLK_PCLK4_DIV1);

	(void)EFM_SetWaitCycle(EFM_WAIT_CYCLE0);
	GPIO_SetReadWaitCycle(GPIO_RD_WAIT0);
	SRAM_SetWaitCycle(SRAM_SRAM_ALL, SRAM_WAIT_CYCLE0, SRAM_WAIT_CYCLE0);
#if defined(HC32F460)
	SRAM_SetWaitCycle(SRAM_SRAMH, SRAM_WAIT_CYCLE0, SRAM_WAIT_CYCLE0);
#endif

	if (CLK_PLLCmd(DISABLE) != LL_OK) {
		return -EIO;
	}

	if (CLK_XtalCmd(DISABLE) != LL_OK) {
		return -EIO;
	}

	ARG_UNUSED(cfg);

	return hc32_clock_refresh_rates(dev);
}

static int hc32_clock_apply_high_tree_from_low(const struct device *dev)
{
	const struct hc32_clock_config *cfg = dev->config;
	int ret;

	ret = hc32_clock_enable_hxt(cfg);
	if (ret < 0) {
		return ret;
	}

	ret = hc32_clock_enable_pll(cfg);
	if (ret < 0) {
		return ret;
	}

	SRAM_SetWaitCycle(SRAM_SRAM_ALL, SRAM_WAIT_CYCLE1, SRAM_WAIT_CYCLE1);
#if defined(HC32F460)
	SRAM_SetWaitCycle(SRAM_SRAMH, SRAM_WAIT_CYCLE0, SRAM_WAIT_CYCLE0);
#endif
	(void)EFM_SetWaitCycle(EFM_WAIT_CYCLE5);
	GPIO_SetReadWaitCycle(GPIO_RD_WAIT3);

#if defined(HC32F460)
	if (PWC_HighSpeedToHighPerformance() != LL_OK) {
		return -EIO;
	}
#elif defined(HC32F4A0)
	if (PWC_LowSpeedToHighSpeed() != LL_OK) {
		return -EIO;
	}
#endif

	CLK_SetClockDiv(CLK_BUS_CLK_ALL,
		       CLK_HCLK_DIV1 | CLK_EXCLK_DIV2 | CLK_PCLK0_DIV1 |
		       CLK_PCLK1_DIV2 | CLK_PCLK2_DIV4 | CLK_PCLK3_DIV4 |
		       CLK_PCLK4_DIV2);
	CLK_SetSysClockSrc(CLK_SYSCLK_SRC_PLL);
	EFM_CacheRamReset(ENABLE);
	EFM_CacheRamReset(DISABLE);
#if defined(HC32F460)
	EFM_CacheCmd(ENABLE);
#elif defined(HC32F4A0)
	EFM_ICacheCmd(ENABLE);
	EFM_DCacheCmd(ENABLE);
#endif

	return hc32_clock_refresh_rates(dev);
}

static int hc32_clock_apply_default_tree(const struct device *dev)
{
	const struct hc32_clock_config *cfg = dev->config;
	int ret;

	CLK_SetClockDiv(CLK_BUS_CLK_ALL,
		       CLK_HCLK_DIV1 | CLK_EXCLK_DIV2 | CLK_PCLK0_DIV1 |
		       CLK_PCLK1_DIV2 | CLK_PCLK2_DIV4 | CLK_PCLK3_DIV4 |
		       CLK_PCLK4_DIV2);

	ret = hc32_clock_enable_hxt(cfg);
	if (ret < 0) {
		return ret;
	}

	hc32_clock_enable_lxt(cfg);

	ret = hc32_clock_enable_pll(cfg);
	if (ret < 0) {
		return ret;
	}

#if defined(HC32F460)
	SRAM_SetWaitCycle(SRAM_SRAMH, SRAM_WAIT_CYCLE0, SRAM_WAIT_CYCLE0);
	SRAM_SetWaitCycle(SRAM_SRAM12 | SRAM_SRAM3 | SRAM_SRAMR,
			  SRAM_WAIT_CYCLE1, SRAM_WAIT_CYCLE1);
#elif defined(HC32F4A0)
	SRAM_SetWaitCycle(SRAM_SRAMH, SRAM_WAIT_CYCLE0, SRAM_WAIT_CYCLE0);
	SRAM_SetWaitCycle(SRAM_SRAM123 | SRAM_SRAM4 | SRAM_SRAMB,
			  SRAM_WAIT_CYCLE1, SRAM_WAIT_CYCLE1);
#endif
	(void)EFM_SetWaitCycle(EFM_WAIT_CYCLE5);
	GPIO_SetReadWaitCycle(GPIO_RD_WAIT3);
#if defined(HC32F460)
	(void)PWC_HighSpeedToHighPerformance();
#elif defined(HC32F4A0)
	(void)PWC_LowSpeedToHighSpeed();
#endif
	CLK_SetSysClockSrc(CLK_SYSCLK_SRC_PLL);
	EFM_CacheRamReset(ENABLE);
	EFM_CacheRamReset(DISABLE);
#if defined(HC32F460)
	EFM_CacheCmd(ENABLE);
#elif defined(HC32F4A0)
	EFM_ICacheCmd(ENABLE);
	EFM_DCacheCmd(ENABLE);
#endif

	return hc32_clock_refresh_rates(dev);
}

static DEVICE_API(clock_control, hc32_clock_api) = {
	.on = hc32_clock_on,
	.off = hc32_clock_off,
	.get_rate = hc32_clock_get_rate,
};

static int hc32_clock_init(const struct device *dev)
{
	const struct hc32_clock_config *cfg = dev->config;
	uint32_t pll_out_hz;

	pll_out_hz = (cfg->hxt_hz / cfg->pll_m) * cfg->pll_n / cfg->pll_p;
	if (pll_out_hz != cfg->sysclk_hz) {
		return -EINVAL;
	}

	return hc32_clock_apply_default_tree(dev);
}

int hc32_clock_set_mode(enum hc32_clock_mode mode)
{
	const struct device *dev = hc32_clock_device();
	int ret;

	if (!device_is_ready(dev)) {
		return -ENODEV;
	}

	k_mutex_lock(&hc32_clock_lock, K_FOREVER);

	switch (mode) {
	case HC32_CLOCK_MODE_HIGH_PERFORMANCE:
		if (hc32_clock_get_mode() == HC32_CLOCK_MODE_HIGH_PERFORMANCE) {
			ret = 0;
		} else {
			ret = hc32_clock_apply_high_tree_from_low(dev);
		}
		break;
	case HC32_CLOCK_MODE_LOW_SPEED:
		if (hc32_clock_get_mode() == HC32_CLOCK_MODE_LOW_SPEED) {
			ret = 0;
		} else {
			ret = hc32_clock_apply_low_tree(dev);
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret == 0) {
		hc32_clock_publish_system_timer_hz();
	}

	k_mutex_unlock(&hc32_clock_lock);

	return ret;
}

enum hc32_clock_mode hc32_clock_get_mode(void)
{
	return ((CM_CMU->CKSWR & CMU_CKSWR_CKSW) == CLK_SYSCLK_SRC_MRC) ?
		HC32_CLOCK_MODE_LOW_SPEED : HC32_CLOCK_MODE_HIGH_PERFORMANCE;
}

uint32_t hc32_clock_get_sysclk_hz(void)
{
	const struct device *dev = hc32_clock_device();
	const struct hc32_clock_data *data;

	if (!device_is_ready(dev)) {
		return SystemCoreClock;
	}

	data = dev->data;
	return data->freqs.u32SysclkFreq;
}

#define HC32_CLOCK_INIT(inst)                                                   \
	static struct hc32_clock_data hc32_clock_data_##inst;                   \
	static const struct hc32_clock_config hc32_clock_config_##inst = {      \
		.base = DT_INST_REG_ADDR(inst),                                  \
		.sysclk_hz = DT_INST_PROP(inst, clock_frequency),                \
		.hxt_hz = DT_INST_PROP(inst, hxt_frequency),                     \
		.lxt_hz = DT_INST_PROP(inst, lxt_frequency),                     \
		.mrc_hz = DT_INST_PROP(inst, mrc_frequency),                     \
		.pll_m = DT_INST_PROP(inst, pll_m),                              \
		.pll_n = DT_INST_PROP(inst, pll_n),                              \
		.pll_p = DT_INST_PROP(inst, pll_p),                              \
		.pll_q = DT_INST_PROP(inst, pll_q),                              \
		.pll_r = DT_INST_PROP(inst, pll_r),                              \
	};                                                                        \
	DEVICE_DT_INST_DEFINE(inst, hc32_clock_init, NULL,                        \
			      &hc32_clock_data_##inst,                           \
			      &hc32_clock_config_##inst, PRE_KERNEL_1,           \
			      CONFIG_CLOCK_CONTROL_INIT_PRIORITY,                \
			      &hc32_clock_api);

DT_INST_FOREACH_STATUS_OKAY(HC32_CLOCK_INIT)
