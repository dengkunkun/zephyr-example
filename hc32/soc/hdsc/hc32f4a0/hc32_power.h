/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_ARM_HDSC_HC32F4A0_HC32_POWER_H_
#define ZEPHYR_SOC_ARM_HDSC_HC32F4A0_HC32_POWER_H_

#include <stdbool.h>
#include <stdint.h>

#include <hc32_clock.h>

struct hc32_power_boot_status {
	bool power_on_reset;
	bool pin_reset;
	bool brown_out_reset;
	bool wdt_reset;
	bool swdt_reset;
	bool power_down_reset;
	bool wakeup_timer;
};

struct hc32_power_status {
	enum hc32_clock_mode clock_mode;
	bool stop_locked;
	uint32_t suspend_idle_entries;
	uint32_t suspend_ram_entries;
	uint64_t last_lpm_us;
	bool last_lpm_expired;
};

const struct hc32_power_boot_status *hc32_power_boot_status_get(void);
void hc32_power_get_status(struct hc32_power_status *status);
int hc32_power_force_sleep_ms(uint32_t duration_ms);
int hc32_power_force_stop_ms(uint32_t duration_ms);
int hc32_power_enter_hibernate_ms(uint32_t duration_ms);

#endif /* ZEPHYR_SOC_ARM_HDSC_HC32F4A0_HC32_POWER_H_ */
