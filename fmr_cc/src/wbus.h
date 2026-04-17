/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file wbus.h
 * @brief WBUS/SBUS RC receiver driver header.
 *
 * USART2, 100 kbps, 8E2, signal-inverted (SBUS standard).
 * Decodes up to WBUS_CH_NUM_MAX channels normalised to ±100.
 * Publishes MSG_RC_FRAME to app_msg bus when a complete frame is received.
 *
 * Only compiled when CONFIG_APP_FMR_WBUS=y.
 */

#ifndef WBUS_H
#define WBUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WBUS_CH_NUM_MAX   10

/** Status codes returned by wbus_get_statu() */
#define WBUS_STATU_NORMAL    0
#define WBUS_STATU_NO_SIGNAL 1
#define WBUS_STATU_NO_DEVICE 2

/** Sentinel returned by wbus_getch() for invalid channel */
#define WBUS_CH_ERROR  INT16_MIN

/**
 * @brief Initialise the WBUS receiver (starts IRQ-driven UART2).
 * @return 0 on success.
 */
int wbus_init(void);

/**
 * @brief Get the latest normalised value for a channel.
 *
 * @param ch Channel number 1-based (1..WBUS_CH_NUM_MAX).
 * @return Value ±100, or WBUS_CH_ERROR on invalid channel.
 */
int16_t wbus_getch(int8_t ch);

/**
 * @brief Get receiver status.
 * @return WBUS_STATU_NORMAL / NO_SIGNAL / NO_DEVICE.
 */
uint8_t wbus_get_statu(void);

#ifdef __cplusplus
}
#endif

#endif /* WBUS_H */
