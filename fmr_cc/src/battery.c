/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file battery.c
 * @brief Battery voltage measurement stub.
 *
 * TODO: Implement ADC sampling using the Zephyr ADC API when a driver for
 * HC32F4A0 ADC1 (PA0, channel 0) becomes available.  Original bare-metal
 * implementation is in /mnt/d/embed/HC32/FMR_CC_SW/v1_2/src/battery.c.
 *
 * Original conversion formula:
 *   voltage_mv = adc_raw * 625 / 1024 * 11
 * where adc_raw is a 12-bit value averaged over 8 samples.
 */

#include "battery.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(battery, LOG_LEVEL_INF);

#ifdef CONFIG_APP_FMR_BATTERY

/*
 * TODO: ADC support.
 *
 * Steps to implement:
 * 1. Add &adc1 node to the overlay with zephyr,adc-dt-spec pointing to
 *    PA0 (channel 0) once the HC32 ADC driver exposes the DT API.
 * 2. Use ADC_DT_SPEC_GET() + adc_sequence_init_dt() to configure.
 * 3. Call adc_read() from a k_work_delayable every ~1 s.
 * 4. Apply: voltage_mv = raw * 625 / 1024 * 11
 * 5. Publish MSG_BAT_VOLT to app_msg.
 */

static volatile uint32_t adc_value;

int battery_init(void)
{
	/* TODO: configure HC32 ADC1 via Zephyr ADC API */
	LOG_WRN("battery ADC not yet implemented (TODO)");
	adc_value = 0;
	return 0;
}

uint32_t battery_get_adc_value(void)
{
	return adc_value;
}

#else /* !CONFIG_APP_FMR_BATTERY */

int battery_init(void)
{
	return 0;
}

uint32_t battery_get_adc_value(void)
{
	return 0;
}

#endif /* CONFIG_APP_FMR_BATTERY */
