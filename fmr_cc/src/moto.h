/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file moto.h
 * @brief Dual-wheel motor Modbus RTU driver header.
 *
 * USART6, 115200 bps, 8N1, RS485 half-duplex.
 * DE=PD6 (active-high), RE=PD5 (active-high in Zephyr, active-low on wire).
 * Uses a dedicated thread + k_msgq for command dispatch and response handling.
 *
 * Modbus register map (from original moto.c):
 *   0x2088 (WRITE_N_REG)  — set left+right speed (2 registers, big-endian)
 *   0x200E (WRITE_1_REG)  — clear fault (value 0x0006)
 *   0x20AB (READ_N_REG)   — read left+right actual speed (2 registers)
 */

#ifndef MOTO_H
#define MOTO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the motor controller (USART6 + RS485 GPIOs + thread).
 * @return 0 on success.
 */
int moto_init(void);

/** @brief Enqueue a set-speed Modbus command (both wheels). */
void moto_cmd_set_all_speed(int16_t r_speed, int16_t l_speed);

/** @brief Enqueue a clear-fault Modbus command. */
void moto_cmd_clr_err(void);

/** @brief Enqueue a read-speed Modbus query (result published to app_msg). */
void moto_cmd_get_all_speed(void);

/** @brief Get the last received left-wheel speed. */
int16_t moto_get_l_speed(void);

/** @brief Get the last received right-wheel speed. */
int16_t moto_get_r_speed(void);

#ifdef __cplusplus
}
#endif

#endif /* MOTO_H */
