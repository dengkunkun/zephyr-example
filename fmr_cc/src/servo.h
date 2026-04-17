/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file servo.h
 * @brief Feetech FT servo driver header.
 *
 * USART7, 115200 bps, 8N1, RS485 half-duplex.
 * DE=PE4 (active-high), RE=PE3 (active-high in Zephyr, active-low on wire).
 * Protocol: 0xFF 0xFF | ID | LEN | CMD | DATA... | ~CHECKSUM
 *
 * Supports up to SERVO_NUM (3) servos.  Servo IDs are 1-based.
 * Position limits (from original servo.c):
 *   Servo 1 (camera)     : 1500..2700
 *   Servo 2/3 (other)    : 2047..3118
 */

#ifndef SERVO_H
#define SERVO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the Feetech servo driver (USART7 + RS485 GPIOs + thread).
 * @return 0 on success.
 */
int servo_init(void);

/**
 * @brief Enqueue a set-position command for one servo.
 *
 * @param id    Servo ID (1-3).
 * @param pos   Target position (encoder counts; clamped to per-servo limits).
 * @param time  Move time in ms (0 = use speed limit instead).
 * @param speed Speed limit (0 = no limit).
 */
void servo_cmd_set_pos_by_id(uint8_t id, int16_t pos, uint16_t time, int16_t speed);

/**
 * @brief Enqueue read-position queries for all three servos.
 *
 * Results are stored internally and available via servo_get_pos_by_id().
 */
void servo_cmd_get_all_pos(void);

/**
 * @brief Get the last received position for a servo.
 * @param id Servo ID (1-3).
 * @return Position in encoder counts.
 */
int16_t servo_get_pos_by_id(uint8_t id);

#ifdef __cplusplus
}
#endif

#endif /* SERVO_H */
