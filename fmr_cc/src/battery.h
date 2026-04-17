/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file battery.h
 * @brief Battery voltage measurement header.
 *
 * TODO: ADC support via Zephyr ADC API is not yet implemented.
 * Original hardware: PA0, ADC1 CH0, 12-bit, averaged 8×.
 * Conversion: adc_raw * 625 / 1024 * 11 (voltage divider ×11).
 * See /mnt/d/embed/HC32/FMR_CC_SW/v1_2/src/battery.c for the original.
 *
 * When CONFIG_APP_FMR_BATTERY is not set, a stub returning 0 is used.
 */

#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the battery ADC module.
 * @return 0 on success, or stub 0.
 */
int battery_init(void);

/**
 * @brief Get the last sampled battery voltage in millivolts.
 *
 * TODO: Not yet implemented; always returns 0 until the Zephyr ADC driver
 * for HC32F4A0 ADC1 is available.
 *
 * @return Battery voltage in mV, or 0 if not implemented.
 */
uint32_t battery_get_adc_value(void);

#ifdef __cplusplus
}
#endif

#endif /* BATTERY_H */
