/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file comm_485_lift.h
 * @brief Lift controller RS485 driver header.
 *
 * USART5, 115200 bps, 8N1, RS485 half-duplex.
 * DE=PA10 (active-high), RE=PA9 (active-high in Zephyr, active-low on wire).
 * Device ID: 0x01.
 *
 * Custom frame format (from original comm_485_1.c):
 *   TX: 0x39 0x93 | ID(1) | CMD(1) | LEN(1) | DATA(LEN) | CRC_H | CRC_L
 *   RX: same layout; CRC16-Modbus covers bytes [ID..DATA_end]
 *
 * Supported commands:
 *   0x03  set position (DATA=int32 big-endian, response CMD=0x83)
 *   0x05  read position (response 4 bytes)
 *   0x07  calibrate zero (response CMD=0x87)
 *   0x08  go to zero (response CMD=0x88)
 *   0x09  read max travel (response 4 bytes)
 */

#ifndef COMM_485_LIFT_H
#define COMM_485_LIFT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the lift RS485 driver (USART5 + thread).
 * @return 0 on success.
 */
int comm_485_lift_init(void);

/** @brief Enqueue a set-position command. */
void comm_485_1_cmd_set_lift_pos(int32_t pos);

/** @brief Enqueue a read-position query. */
void comm_485_1_cmd_get_lift_pos(void);

/** @brief Enqueue a zero-calibration command. */
void comm_485_1_cmd_calibrate(void);

/** @brief Enqueue a go-to-zero command. */
void comm_485_1_cmd_go_zero(void);

/** @brief Enqueue a read-max-travel query. */
void comm_485_1_cmd_get_lift_max(void);

/** @brief Get the last received lift position. */
uint32_t comm_485_1_get_lift_pos(void);

/** @brief Get the last received max-travel value. */
uint32_t comm_485_1_get_lift_calibrate_value(void);

#ifdef __cplusplus
}
#endif

#endif /* COMM_485_LIFT_H */
