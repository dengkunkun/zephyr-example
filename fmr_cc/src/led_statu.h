/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file led_statu.h
 * @brief 74HC595 shift-register LED status controller.
 *
 * Replaces exio_595.c + led_statu_ctrl.c.  Uses GPIO bit-bang on the three
 * shift-register lines (SCK, RCK, DAT) exposed as DT node labels led_sck,
 * led_rck, led_dat defined in the board DTS.  A k_work_delayable fires every
 * 100 ms to update the shift register, implementing on/off/blink per-channel.
 *
 * Only compiled when CONFIG_APP_FMR_LED_595=y.
 *
 * LED channel numbering matches original led_statu_ctrl.c (1-based, max 8).
 */

#ifndef LED_STATU_H
#define LED_STATU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** LED status modes */
enum led_statu_mode {
	LED_STATU_OFF    = 0,
	LED_STATU_ON     = 1,
	LED_STATU_BLINK  = 2,
};

/**
 * @brief Initialise the LED 595 controller.
 * @return 0 on success, negative on GPIO error.
 */
int led_statu_init(void);

/**
 * @brief Set the status mode for a single LED channel.
 *
 * @param ch   Channel number 1-8.
 * @param mode LED_STATU_OFF / LED_STATU_ON / LED_STATU_BLINK.
 */
void lsc_ctrl(uint8_t ch, enum led_statu_mode mode);

/**
 * @brief Set the blink period for a channel.
 *
 * @param ch     Channel number 1-8.
 * @param period Period in units of 100 ms ticks (0 = solid on).
 */
void lsc_set_period(uint8_t ch, uint8_t period);

/**
 * @brief Force an immediate flush to the shift register (optional).
 */
void lsc_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* LED_STATU_H */
