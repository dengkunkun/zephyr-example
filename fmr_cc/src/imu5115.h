/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file imu5115.h
 * @brief IMU5115 inertial measurement unit driver header.
 *
 * USART1, 115200 bps, 8N1, receive-only.
 * Frame: 0xBD 0xDB 0x0A + 24 bytes data + 2-byte seq + 1-byte XOR CRC = 34 bytes.
 * Publishes MSG_IMU_FRAME to app_msg bus on each valid frame.
 */

#ifndef IMU5115_H
#define IMU5115_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Decoded IMU data */
typedef struct {
	uint32_t gx, gy, gz;      /**< Angular velocity (raw) */
	uint32_t ax, ay, az;      /**< Linear acceleration (raw) */
	uint16_t frame_seq;       /**< Frame sequence counter */
	int64_t  tstamp;          /**< k_uptime_get() at reception */
} imu_dat_t;

/**
 * @brief Initialise the IMU5115 receiver (USART1).
 * @return 0 on success.
 */
int imu_init(void);

/**
 * @brief Copy the latest IMU data into the caller's buffer.
 * @param dat Output buffer.
 */
void imu_get_dat(imu_dat_t *dat);

#ifdef __cplusplus
}
#endif

#endif /* IMU5115_H */
