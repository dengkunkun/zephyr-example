/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file imu5115.c
 * @brief IMU5115 parser — Zephyr port of original imu5115.c.
 *
 * USART1, 115200 8N1.  IRQ-driven byte collector feeds a state machine
 * that assembles 34-byte frames (0xBD 0xDB 0x0A header) and validates the
 * trailing XOR checksum.  On success the decoded imu_dat_t is updated and
 * a MSG_IMU_FRAME message is published.
 */

#include "imu5115.h"
#include "app_msg.h"
#include "doraemon_pack.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(imu5115, LOG_LEVEL_INF);

#define IMU_FRAME_LEN   34
#define IMU_HEAD1       0xBDU
#define IMU_HEAD2       0xDBU
#define IMU_HEAD3       0x0AU

enum imu_rx_state {
	IMU_ST_HEAD1 = 0,
	IMU_ST_HEAD2,
	IMU_ST_HEAD3,
	IMU_ST_DATA,
	IMU_ST_CRC,
};

/* Ring buffer for raw bytes from ISR */
#define RX_BUF 256
static uint8_t rx_ring[RX_BUF];
static uint32_t rx_head, rx_tail;
static struct k_spinlock rx_lock;

static volatile imu_dat_t imu_dat;
static struct k_work parse_work;

static const struct device *usart1_dev = DEVICE_DT_GET(DT_NODELABEL(usart1));

static uint8_t xor_sum(const uint8_t *dat, uint32_t len)
{
	uint8_t s = 0;

	while (len--) {
		s ^= *dat++;
	}
	return s;
}

static void uart_cb(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	if (!uart_irq_update(dev)) {
		return;
	}

	while (uart_irq_rx_ready(dev)) {
		uint8_t c;

		if (uart_fifo_read(dev, &c, 1) != 1) {
			break;
		}
		k_spinlock_key_t key = k_spin_lock(&rx_lock);
		uint32_t next = (rx_tail + 1) % RX_BUF;

		if (next != rx_head) {
			rx_ring[rx_tail] = c;
			rx_tail = next;
		}
		k_spin_unlock(&rx_lock, key);
	}
	k_work_submit(&parse_work);
}

static void parse_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	static enum imu_rx_state state = IMU_ST_HEAD1;
	static uint8_t  dat[IMU_FRAME_LEN];
	static uint8_t  rx_dat_len;

	k_spinlock_key_t key;
	uint8_t byte;

	while (true) {
		key = k_spin_lock(&rx_lock);
		if (rx_head == rx_tail) {
			k_spin_unlock(&rx_lock, key);
			break;
		}
		byte = rx_ring[rx_head];
		rx_head = (rx_head + 1) % RX_BUF;
		k_spin_unlock(&rx_lock, key);

		switch (state) {
		case IMU_ST_HEAD1:
			if (byte == IMU_HEAD1) {
				dat[0] = byte;
				state  = IMU_ST_HEAD2;
			}
			break;

		case IMU_ST_HEAD2:
			if (byte == IMU_HEAD2) {
				dat[1] = byte;
				state  = IMU_ST_HEAD3;
			} else {
				state = IMU_ST_HEAD1;
			}
			break;

		case IMU_ST_HEAD3:
			if (byte == IMU_HEAD3) {
				dat[2]     = byte;
				rx_dat_len = 3;
				state      = IMU_ST_DATA;
			} else {
				state = IMU_ST_HEAD1;
			}
			break;

		case IMU_ST_DATA:
			dat[rx_dat_len++] = byte;
			if (rx_dat_len >= IMU_FRAME_LEN - 1) {
				state = IMU_ST_CRC;
			}
			break;

		case IMU_ST_CRC:
			dat[rx_dat_len] = byte;
			if (xor_sum(dat, 33) == dat[33]) {
				imu_dat_t tmp;

				tmp.gx        = dp_u8_2_u32_lsb(&dat[3]);
				tmp.gy        = dp_u8_2_u32_lsb(&dat[7]);
				tmp.gz        = dp_u8_2_u32_lsb(&dat[11]);
				tmp.ax        = dp_u8_2_u32_lsb(&dat[15]);
				tmp.ay        = dp_u8_2_u32_lsb(&dat[19]);
				tmp.az        = dp_u8_2_u32_lsb(&dat[23]);
				tmp.frame_seq = dp_u8_2_u16_lsb(&dat[31]);
				tmp.tstamp    = k_uptime_get();

				memcpy((void *)&imu_dat, &tmp, sizeof(tmp));

				struct app_msg msg = {
					.type = MSG_IMU_FRAME,
					.imu  = {
						.gx        = tmp.gx,
						.gy        = tmp.gy,
						.gz        = tmp.gz,
						.ax        = tmp.ax,
						.ay        = tmp.ay,
						.az        = tmp.az,
						.frame_seq = tmp.frame_seq,
					},
				};
				app_msg_publish(&msg);
			} else {
				LOG_WRN("IMU XOR error");
			}
			rx_dat_len = 0;
			state = IMU_ST_HEAD1;
			break;
		}
	}
}

int imu_init(void)
{
	if (!device_is_ready(usart1_dev)) {
		LOG_ERR("USART1 not ready");
		return -ENODEV;
	}

	k_work_init(&parse_work, parse_work_fn);
	uart_irq_callback_user_data_set(usart1_dev, uart_cb, NULL);
	uart_irq_rx_enable(usart1_dev);

	LOG_INF("imu5115 init OK (USART1)");
	return 0;
}

void imu_get_dat(imu_dat_t *dat)
{
	memcpy(dat, (const void *)&imu_dat, sizeof(*dat));
}
