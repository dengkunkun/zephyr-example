/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/timer/system_timer.h>
#include <zephyr/drivers/timer/system_timer_lpm.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/policy.h>
#include <zephyr/sys/util.h>

#include <hc32_clock.h>
#include <hc32_power.h>
#include <hc32_uart.h>
#include <soc.h>

#include <hc32_ll_clk.h>
#include <hc32_ll_interrupts.h>
#include <hc32f4a0_ll_interrupts_share.h>
#include <hc32_ll_pwc.h>
#include <hc32_ll_rmu.h>

#define HC32_WKTM_IRQn        INT130_IRQn
#define HC32_WKTM_INT_SRC     INT_SRC_WKTM_PRD
#define HC32_WKT_CMP_MAX      0x0FFFU
#define HC32_WKT_64HZ         64U
#define HC32_WKT_LRC_HZ       LRC_VALUE
#define HC32_PD_WKT_MAX_US    DIV_ROUND_UP((uint64_t)HC32_WKT_CMP_MAX * USEC_PER_SEC, HC32_WKT_LRC_HZ)

static const struct pm_state_info hc32_suspend_idle_state = {
	.state = PM_STATE_SUSPEND_TO_IDLE,
	.substate_id = 0,
};

static const struct pm_state_info hc32_suspend_ram_state = {
	.state = PM_STATE_SUSPEND_TO_RAM,
	.substate_id = 0,
};

static struct hc32_power_boot_status hc32_boot_status;
static atomic_t hc32_suspend_idle_entries;
static atomic_t hc32_suspend_ram_entries;
static volatile bool hc32_lpm_armed;
static volatile bool hc32_lpm_expired;
static volatile uint64_t hc32_lpm_last_us;
static volatile bool hc32_lpm_last_expired;
static volatile bool hc32_hibernate_requested;
static volatile uint16_t hc32_hibernate_cmp;
static volatile int hc32_hibernate_ret;
K_SEM_DEFINE(hc32_hibernate_done, 0, 1);

#if DT_HAS_CHOSEN(zephyr_shell_uart)
static const struct device *const hc32_pm_uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));
#elif DT_HAS_CHOSEN(zephyr_console)
static const struct device *const hc32_pm_uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
#endif

static uint16_t hc32_wkt_cmp_from_us(uint64_t us, uint32_t hz)
{
	uint64_t cmp = DIV_ROUND_UP(us * hz, USEC_PER_SEC);

	if (cmp == 0U) {
		cmp = 1U;
	}

	if (cmp > HC32_WKT_CMP_MAX) {
		cmp = HC32_WKT_CMP_MAX;
	}

	return (uint16_t)cmp;
}

static uint64_t hc32_wkt_us_from_cmp(uint16_t cmp, uint32_t hz)
{
	return DIV_ROUND_UP((uint64_t)cmp * USEC_PER_SEC, hz);
}

static void hc32_wkt_disable_clear(void)
{
	PWC_WKT_Cmd(DISABLE);
	PWC_WKT_ClearStatus();
}

static void hc32_wkt_arm(uint64_t max_lpm_time_us)
{
	uint16_t cmp;
	uint32_t hz;
	uint16_t src;

	if (max_lpm_time_us == 0U) {
		hc32_lpm_armed = false;
		hc32_lpm_expired = false;
		hc32_lpm_last_us = 0U;
		hc32_lpm_last_expired = false;
		hc32_wkt_disable_clear();
		return;
	}

	if (max_lpm_time_us <= hc32_wkt_us_from_cmp(HC32_WKT_CMP_MAX, HC32_WKT_LRC_HZ)) {
		hz = HC32_WKT_LRC_HZ;
		src = PWC_WKT_CLK_SRC_RTCLRC;
	} else {
		hz = HC32_WKT_64HZ;
		src = PWC_WKT_CLK_SRC_64HZ;
	}

	cmp = hc32_wkt_cmp_from_us(max_lpm_time_us, hz);

	hc32_lpm_last_us = hc32_wkt_us_from_cmp(cmp, hz);
	hc32_lpm_expired = false;
	hc32_lpm_armed = true;

	PWC_WKT_Cmd(DISABLE);
	PWC_WKT_ClearStatus();
	PWC_WKT_Config(src, cmp);
	PWC_WKT_Cmd(ENABLE);
}

static uint64_t hc32_lpm_finish(void)
{
	uint64_t elapsed_us = 0U;
	bool expired = hc32_lpm_expired || (PWC_WKT_GetStatus() == SET);

	if (hc32_lpm_armed && expired) {
		elapsed_us = hc32_lpm_last_us;
	}

	hc32_lpm_last_expired = expired;
	hc32_wkt_disable_clear();
	hc32_lpm_armed = false;
	hc32_lpm_expired = false;

	return elapsed_us;
}

static void hc32_power_capture_boot_status(void)
{
	hc32_boot_status.power_on_reset = (RMU_GetStatus(RMU_FLAG_PWR_ON) == SET);
	hc32_boot_status.pin_reset = (RMU_GetStatus(RMU_FLAG_PIN) == SET);
	hc32_boot_status.brown_out_reset = (RMU_GetStatus(RMU_FLAG_BROWN_OUT) == SET);
	hc32_boot_status.wdt_reset = (RMU_GetStatus(RMU_FLAG_WDT) == SET);
	hc32_boot_status.swdt_reset = (RMU_GetStatus(RMU_FLAG_SWDT) == SET);
	hc32_boot_status.power_down_reset = (RMU_GetStatus(RMU_FLAG_PWR_DOWN) == SET);
	hc32_boot_status.wakeup_timer = (PWC_PD_GetWakeupStatus(PWC_PD_WKUP_FLAG_WKTM) == SET);

	PWC_PD_ClearWakeupStatus(PWC_PD_WKUP_FLAG_ALL);
	RMU_ClearStatus();
}

static void hc32_power_configure_stop_mode(void)
{
	stc_pwc_stop_mode_config_t stop_cfg;

	(void)PWC_STOP_StructInit(&stop_cfg);
	stop_cfg.u8StopDrv = PWC_STOP_DRV_HIGH;
	stop_cfg.u16Clock = PWC_STOP_CLK_KEEP;
	stop_cfg.u16FlashWait = PWC_STOP_FLASH_WAIT_ON;
	(void)PWC_STOP_Config(&stop_cfg);
	INTC_WakeupSrcCmd(INTC_STOP_WKUP_WKTM, ENABLE);
}

static int hc32_power_enter_pd_cmp(uint16_t cmp)
{
	stc_pwc_pd_mode_config_t pd_cfg;
	uint32_t nvic_iser[5];
	uint32_t systick_ctrl = SysTick->CTRL;
	int32_t pd_ret;
	uint8_t i;

	(void)PWC_PD_StructInit(&pd_cfg);
	pd_cfg.u8IOState = PWC_PD_IO_KEEP1;
	pd_cfg.u8Mode = PWC_PD_MD1;
	pd_cfg.u8VcapCtrl = PWC_PD_VCAP_0P047UF;

	if (PWC_PD_Config(&pd_cfg) != LL_OK) {
		return -EIO;
	}

	PWC_PD_ClearWakeupStatus(PWC_PD_WKUP_FLAG_ALL);
	PWC_PD_WakeupCmd(PWC_PD_WKUP_WKTM, ENABLE);
	PWC_WKT_Cmd(DISABLE);
	PWC_WKT_ClearStatus();
	(void)CLK_LrcCmd(ENABLE);
	PWC_WKT_Config(PWC_WKT_CLK_SRC_RTCLRC, cmp);
	(void)INTC_ShareIrqCmd(HC32_WKTM_INT_SRC, ENABLE);
	NVIC_ClearPendingIRQ(HC32_WKTM_IRQn);
	NVIC_SetPriority(HC32_WKTM_IRQn, 1);
	SysTick->CTRL = 0U;
	SysTick->VAL = 0U;

	for (i = 0U; i < ARRAY_SIZE(nvic_iser); i++) {
		nvic_iser[i] = NVIC->ISER[i];
		NVIC->ICER[i] = 0xFFFFFFFFU;
		NVIC->ICPR[i] = 0xFFFFFFFFU;
	}
	NVIC_EnableIRQ(HC32_WKTM_IRQn);
	NVIC_ClearPendingIRQ(HC32_WKTM_IRQn);

	__DSB();
	__ISB();
#ifdef SCB_ICSR_STTNS_Msk
	SCB->ICSR = (SCB->ICSR & SCB_ICSR_STTNS_Msk) |
		    SCB_ICSR_PENDSTCLR_Msk |
		    SCB_ICSR_PENDSVCLR_Msk;
#else
	SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
#endif
	__DSB();
	__ISB();

	PWC_WKT_Cmd(ENABLE);
	pd_ret = PWC_PD_Enter();
	SysTick->VAL = 0U;
	SysTick->CTRL = systick_ctrl;
	for (i = 0U; i < ARRAY_SIZE(nvic_iser); i++) {
		NVIC->ISER[i] = nvic_iser[i];
	}

	return (pd_ret == LL_ERR_TIMEOUT) ? -ETIMEDOUT : -EIO;
}

static void hc32_wkt_isr(const void *arg)
{
	ARG_UNUSED(arg);

	if (PWC_WKT_GetStatus() == SET) {
		hc32_lpm_expired = true;
		PWC_WKT_ClearStatus();
	}
}

static void hc32_pm_notifier_entry(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(substate_id);

	if (state == PM_STATE_SUSPEND_TO_IDLE) {
		atomic_inc(&hc32_suspend_idle_entries);
	} else if (state == PM_STATE_SUSPEND_TO_RAM) {
		atomic_inc(&hc32_suspend_ram_entries);
	}
}

static struct pm_notifier hc32_pm_notifier = {
	.substate_entry = hc32_pm_notifier_entry,
	.substate_exit = NULL,
	.report_substate = true,
};

static int hc32_power_init(void)
{
	if (CLK_LrcCmd(ENABLE) != LL_OK) {
		return -EIO;
	}

	(void)INTC_ShareIrqCmd(HC32_WKTM_INT_SRC, ENABLE);
	IRQ_CONNECT(HC32_WKTM_IRQn, 1, hc32_wkt_isr, NULL, 0);
	NVIC_ClearPendingIRQ(HC32_WKTM_IRQn);
	irq_enable(HC32_WKTM_IRQn);

	hc32_wkt_disable_clear();
	hc32_power_capture_boot_status();
	hc32_power_configure_stop_mode();
	pm_notifier_register(&hc32_pm_notifier);
	pm_policy_state_lock_get(PM_STATE_SUSPEND_TO_RAM, PM_ALL_SUBSTATES);

	return 0;
}

SYS_INIT(hc32_power_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

void z_sys_clock_lpm_enter(uint64_t max_lpm_time_us)
{
	hc32_wkt_arm(max_lpm_time_us);
}

uint64_t z_sys_clock_lpm_exit(void)
{
	return hc32_lpm_finish();
}

void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(substate_id);

	__enable_irq();
	__set_BASEPRI(0U);

	switch (state) {
	case PM_STATE_RUNTIME_IDLE:
		SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
		__DSB();
		__WFI();
		__ISB();
		break;
	case PM_STATE_SUSPEND_TO_IDLE:
		SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
		PWC_SLEEP_Enter(PWC_SLEEP_WFI);
		break;
	case PM_STATE_SUSPEND_TO_RAM:
		if (hc32_hibernate_requested) {
			hc32_hibernate_ret = hc32_power_enter_pd_cmp(hc32_hibernate_cmp);
			hc32_hibernate_requested = false;
			k_sem_give(&hc32_hibernate_done);
		} else {
			SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
			PWC_STOP_Enter(PWC_STOP_WFI);
		}
		break;
	default:
		break;
	}
}

void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(state);
	ARG_UNUSED(substate_id);

	SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
#if DT_HAS_CHOSEN(zephyr_shell_uart) || DT_HAS_CHOSEN(zephyr_console)
	if ((state == PM_STATE_SUSPEND_TO_IDLE || state == PM_STATE_SUSPEND_TO_RAM) &&
	    device_is_ready(hc32_pm_uart)) {
		(void)hc32_uart_reconfigure(hc32_pm_uart);
	}
#endif
	irq_unlock(0);
	__ISB();
}

const struct hc32_power_boot_status *hc32_power_boot_status_get(void)
{
	return &hc32_boot_status;
}

void hc32_power_get_status(struct hc32_power_status *status)
{
	if (status == NULL) {
		return;
	}

	status->clock_mode = hc32_clock_get_mode();
	status->stop_locked = pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_RAM,
							     PM_ALL_SUBSTATES);
	status->suspend_idle_entries = (uint32_t)atomic_get(&hc32_suspend_idle_entries);
	status->suspend_ram_entries = (uint32_t)atomic_get(&hc32_suspend_ram_entries);
	status->last_lpm_us = hc32_lpm_last_us;
	status->last_lpm_expired = hc32_lpm_last_expired;
}

int hc32_power_force_sleep_ms(uint32_t duration_ms)
{
#ifdef CONFIG_PM
	uint32_t before;

	if (duration_ms == 0U) {
		return -EINVAL;
	}

	before = (uint32_t)atomic_get(&hc32_suspend_idle_entries);
	if (!pm_state_force(0, &hc32_suspend_idle_state)) {
		return -EINVAL;
	}

	k_msleep(duration_ms);
	return ((uint32_t)atomic_get(&hc32_suspend_idle_entries) > before) ? 0 : -EIO;
#else
	ARG_UNUSED(duration_ms);
	return -ENOSYS;
#endif
}

int hc32_power_force_stop_ms(uint32_t duration_ms)
{
	uint64_t elapsed_us;
	int ret = 0;

	if (duration_ms == 0U) {
		return -EINVAL;
	}

#if DT_HAS_CHOSEN(zephyr_shell_uart) || DT_HAS_CHOSEN(zephyr_console)
	if (device_is_ready(hc32_pm_uart)) {
		(void)hc32_uart_pm_control(hc32_pm_uart, true);
	}
#endif

	hc32_wkt_arm((uint64_t)duration_ms * USEC_PER_MSEC);
	atomic_inc(&hc32_suspend_ram_entries);
	__enable_irq();
	__set_BASEPRI(0U);
	SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
	PWC_STOP_Enter(PWC_STOP_WFI);
	SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
	__ISB();
	elapsed_us = hc32_lpm_finish();
	ret = (elapsed_us != 0U) ? 0 : -EIO;

#if DT_HAS_CHOSEN(zephyr_shell_uart) || DT_HAS_CHOSEN(zephyr_console)
	if (device_is_ready(hc32_pm_uart)) {
		if (ret == 0) {
			int uart_ret = hc32_uart_reconfigure(hc32_pm_uart);

			if (uart_ret != 0) {
				ret = uart_ret;
			}
		}
		(void)hc32_uart_pm_control(hc32_pm_uart, false);
	}
#endif
	return ret;
}

int hc32_power_enter_hibernate_ms(uint32_t duration_ms)
{
#ifdef CONFIG_PM
	uint16_t cmp;
	uint64_t duration_us;

	if (duration_ms == 0U) {
		return -EINVAL;
	}

	duration_us = (uint64_t)duration_ms * USEC_PER_MSEC;
	if (duration_us > HC32_PD_WKT_MAX_US) {
		return -ERANGE;
	}

	cmp = hc32_wkt_cmp_from_us(duration_us, HC32_WKT_LRC_HZ);

#if DT_HAS_CHOSEN(zephyr_shell_uart) || DT_HAS_CHOSEN(zephyr_console)
	if (device_is_ready(hc32_pm_uart)) {
		(void)hc32_uart_pm_control(hc32_pm_uart, true);
	}
#endif

	while (k_sem_take(&hc32_hibernate_done, K_NO_WAIT) == 0) {
	}

	hc32_hibernate_cmp = cmp;
	hc32_hibernate_ret = -ETIMEDOUT;
	hc32_hibernate_requested = true;

	if (!pm_state_force(0, &hc32_suspend_ram_state)) {
		hc32_hibernate_requested = false;
#if DT_HAS_CHOSEN(zephyr_shell_uart) || DT_HAS_CHOSEN(zephyr_console)
		if (device_is_ready(hc32_pm_uart)) {
			(void)hc32_uart_pm_control(hc32_pm_uart, false);
		}
#endif
		return -EIO;
	}

	k_yield();
	if (k_sem_take(&hc32_hibernate_done, K_MSEC(duration_ms + 500U)) != 0) {
		hc32_hibernate_requested = false;
		return -ETIMEDOUT;
	}

#if DT_HAS_CHOSEN(zephyr_shell_uart) || DT_HAS_CHOSEN(zephyr_console)
	if (device_is_ready(hc32_pm_uart)) {
		(void)hc32_uart_reconfigure(hc32_pm_uart);
		(void)hc32_uart_pm_control(hc32_pm_uart, false);
	}
#endif
	return hc32_hibernate_ret;
#else /* !CONFIG_PM */
	ARG_UNUSED(duration_ms);
	return -ENOSYS;
#endif
}
