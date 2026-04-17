/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file usb_uplink.c
 * @brief USB uplink — Zephyr port of original usb_uplink.c.
 *
 * When CONFIG_APP_FMR_USB_UPLINK=y:
 *   - Subscribes to MSG_MOTO_STATUS, MSG_SERVO_STATUS, MSG_LIFT_STATUS,
 *     MSG_IMU_FRAME, MSG_BAT_VOLT.
 *   - Every 200 ms transmits a 0x39 0x93 status frame to the Zephyr console
 *     via printk (hex-encoded).  TODO: Replace printk with USB CDC class
 *     (usb_dc_cdc) once the HC32 USB HS driver is available.
 *   - Parses incoming 0x39 0x93 command frames from the console UART and
 *     dispatches lift / servo commands.
 *
 * Wire format is byte-for-byte identical to the original so a PC-side tool
 * written for the original firmware will work once USB CDC is added.
 *
 * TODO: USB CDC — waiting for HC32 USB HS driver in Zephyr.
 *       See /mnt/d/embed/HC32/FMR_CC_SW/v1_2/src/usb_uplink.c for original.
 */

#include "usb_uplink.h"

#ifdef CONFIG_APP_FMR_USB_UPLINK

#include "app_msg.h"
#include "crc16_modbus.h"
#include "imu5115.h"
#include "moto.h"
#include "servo.h"
#include "comm_485_lift.h"
#include "battery.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(usb_uplink, LOG_LEVEL_INF);

/* Uplink TX period */
#define UPLOAD_PERIOD_MS      200
#define UPLOAD_IMU_PERIOD_MS  50

/* Shared latest-value cache (updated by subscriber thread) */
static struct {
	int16_t  l_speed, r_speed;
	uint16_t bat_mv;
	int16_t  servo_pos[3];
	int32_t  lift_pos;
	imu_dat_t imu;
} g_state;

static K_MSGQ_DEFINE(up_msgq, sizeof(struct app_msg), 8, 4);

/* ---------------------------------------------------------------------------
 * Frame packer (matches original packet_data())
 * Header: 0x39 0x93 CMD LEN_H LEN_L DATA CRC_H CRC_L
 * CRC16-Modbus over DATA only.
 * ---------------------------------------------------------------------------
 */
static int pack_frame(uint8_t cmd, const uint8_t *data, uint16_t data_len,
		      uint8_t *dst, uint16_t dst_len)
{
	if (dst_len < (uint16_t)(data_len + 7)) {
		return 0;
	}
	dst[0] = 0x39;
	dst[1] = 0x93;
	dst[2] = cmd;
	dst[3] = (uint8_t)((data_len >> 8) & 0xff);
	dst[4] = (uint8_t)(data_len & 0xff);
	memcpy(&dst[5], data, data_len);
	uint16_t crc = calc_modbus_crc16(data, data_len);

	dst[5 + data_len] = (uint8_t)((crc >> 8) & 0xff);
	dst[6 + data_len] = (uint8_t)(crc & 0xff);
	return data_len + 7;
}

/*
 * TODO: Replace with usb_dc_cdc_write() once USB CDC driver is available.
 * Currently prints the frame as hex bytes to the console for diagnostics.
 */
static void send_frame(const uint8_t *buf, int len)
{
	for (int i = 0; i < len; i++) {
		printk("%02x", buf[i]);
	}
	printk("\n");
}

/* ---------------------------------------------------------------------------
 * RX command parser (matches original __execute_rx_data())
 * ---------------------------------------------------------------------------
 */
enum rx_state {
	RX_S0 = 0, /* looking for 0x39 */
	RX_S1,     /* 0x93 */
	RX_S2,     /* CMD */
	RX_S3,     /* LEN_H */
	RX_S4,     /* LEN_L */
	RX_S5,     /* DATA */
	RX_S6,     /* CRC_H */
	RX_S7,     /* CRC_L */
};

static struct {
	enum rx_state state;
	uint8_t  cmd;
	uint8_t  dat[USBUL_RX_BUFFER_SIZE];
	uint32_t dat_size;
	uint16_t data_len;
	uint16_t crc;
} rx;

static void dispatch_rx_command(uint8_t cmd, const uint8_t *dat, uint16_t len)
{
	switch (cmd) {
	case USBLINK_PACK_CMD_LIFT_SET_POS:
		if (len >= 4) {
			int32_t pos = ((int32_t)dat[0] << 24) | ((int32_t)dat[1] << 16) |
				      ((int32_t)dat[2] << 8) | dat[3];
			comm_485_1_cmd_set_lift_pos(pos);
		}
		break;
	case USBLINK_PACK_CMD_LIFT_CALIBRATE:
		comm_485_1_cmd_calibrate();
		break;
	case USBLINK_PACK_CMD_SERVO_SET_POS_1:
	case USBLINK_PACK_CMD_SERVO_SET_POS_2:
	case USBLINK_PACK_CMD_SERVO_SET_POS_3:
		if (len >= 2) {
			uint8_t id  = cmd - USBLINK_PACK_CMD_SERVO_SET_POS_1 + 1;
			int16_t pos = (int16_t)((dat[0] << 8) | dat[1]);

			servo_cmd_set_pos_by_id(id, pos, 0, 0);
		}
		break;
	default:
		LOG_WRN("usb_uplink: unknown cmd 0x%02x", cmd);
		break;
	}
}

static void process_rx_byte(uint8_t b)
{
	switch (rx.state) {
	case RX_S0:
		if (b == 0x39) {
			rx.state    = RX_S1;
			rx.cmd      = USBLINK_PACK_CMD_IDEL;
			rx.dat_size = 0;
		}
		break;
	case RX_S1:
		rx.state = (b == 0x93) ? RX_S2 : RX_S0;
		break;
	case RX_S2:
		rx.cmd   = b;
		rx.state = RX_S3;
		break;
	case RX_S3:
		rx.data_len = (uint16_t)b << 8;
		rx.state    = RX_S4;
		break;
	case RX_S4:
		rx.data_len |= b;
		if (rx.data_len == 0) {
			rx.state = RX_S6;
		} else if (rx.data_len > USBUL_RX_BUFFER_SIZE) {
			rx.state = RX_S0; /* too large, discard */
		} else {
			rx.state = RX_S5;
		}
		break;
	case RX_S5:
		rx.dat[rx.dat_size++] = b;
		if (rx.dat_size >= rx.data_len) {
			rx.state = RX_S6;
		}
		break;
	case RX_S6:
		rx.crc   = (uint16_t)b << 8;
		rx.state = RX_S7;
		break;
	case RX_S7: {
		rx.crc |= b;
		rx.state = RX_S0;
		uint16_t exp_crc = calc_modbus_crc16(rx.dat, rx.dat_size);

		if (exp_crc == rx.crc) {
			dispatch_rx_command(rx.cmd, rx.dat, rx.dat_size);
		} else {
			LOG_WRN("usb_uplink: CRC error");
		}
		break;
	}
	default:
		rx.state = RX_S0;
		break;
	}
}

/* ---------------------------------------------------------------------------
 * Transmit helpers
 * ---------------------------------------------------------------------------
 */
void usbul_send_status(void)
{
	uint8_t data[16];
	uint8_t pkt[7 + 16];

	memcpy(&data[0],  &g_state.l_speed,      2);
	memcpy(&data[2],  &g_state.r_speed,       2);
	memcpy(&data[4],  &g_state.bat_mv,        2);
	memcpy(&data[6],  &g_state.servo_pos[0],  2);
	memcpy(&data[8],  &g_state.servo_pos[1],  2);
	memcpy(&data[10], &g_state.servo_pos[2],  2);
	memcpy(&data[12], &g_state.lift_pos,      4);

	int len = pack_frame(USBLINK_PACK_CMD_UPLOAD_REP, data, sizeof(data),
			     pkt, sizeof(pkt));
	if (len > 0) {
		send_frame(pkt, len);
	}
}

void usbul_send_imu(void)
{
	uint8_t data[24];
	uint8_t pkt[7 + 24];

	memcpy(&data[0],  &g_state.imu.gx, 4);
	memcpy(&data[4],  &g_state.imu.gy, 4);
	memcpy(&data[8],  &g_state.imu.gz, 4);
	memcpy(&data[12], &g_state.imu.ax, 4);
	memcpy(&data[16], &g_state.imu.ay, 4);
	memcpy(&data[20], &g_state.imu.az, 4);

	int len = pack_frame(USBLINK_PACK_CMD_UPLOAD_IMU_REP, data, sizeof(data),
			     pkt, sizeof(pkt));
	if (len > 0) {
		send_frame(pkt, len);
	}
}

/* ---------------------------------------------------------------------------
 * Uplink thread
 * ---------------------------------------------------------------------------
 */
#define UPLINK_STACK_SIZE 1536
K_THREAD_STACK_DEFINE(uplink_stack, UPLINK_STACK_SIZE);
static struct k_thread uplink_thread_data;

static struct k_work_delayable status_work;
static struct k_work_delayable imu_work;

static void status_work_fn(struct k_work *w)
{
	usbul_send_status();
	k_work_reschedule((struct k_work_delayable *)w, K_MSEC(UPLOAD_PERIOD_MS));
}

static void imu_work_fn(struct k_work *w)
{
	usbul_send_imu();
	k_work_reschedule((struct k_work_delayable *)w, K_MSEC(UPLOAD_IMU_PERIOD_MS));
}

static void uplink_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

	struct app_msg msg;

	while (1) {
		k_msgq_get(&up_msgq, &msg, K_FOREVER);

		switch (msg.type) {
		case MSG_MOTO_STATUS:
			g_state.l_speed = msg.moto.l_speed;
			g_state.r_speed = msg.moto.r_speed;
			break;
		case MSG_SERVO_STATUS:
			if (msg.servo.id >= 1 && msg.servo.id <= 3) {
				g_state.servo_pos[msg.servo.id - 1] = msg.servo.pos;
			}
			break;
		case MSG_LIFT_STATUS:
			g_state.lift_pos = msg.lift.pos;
			break;
		case MSG_IMU_FRAME:
			g_state.imu.gx = msg.imu.gx;
			g_state.imu.gy = msg.imu.gy;
			g_state.imu.gz = msg.imu.gz;
			g_state.imu.ax = msg.imu.ax;
			g_state.imu.ay = msg.imu.ay;
			g_state.imu.az = msg.imu.az;
			break;
		case MSG_BAT_VOLT:
			g_state.bat_mv = msg.bat.voltage_mv;
			break;
		default:
			break;
		}
	}
}

int usb_uplink_init(void)
{
	/* Subscribe to all status messages */
	app_msg_subscribe(&up_msgq,
			  BIT(MSG_MOTO_STATUS) | BIT(MSG_SERVO_STATUS) |
			  BIT(MSG_LIFT_STATUS) | BIT(MSG_IMU_FRAME)    |
			  BIT(MSG_BAT_VOLT));

	k_thread_create(&uplink_thread_data, uplink_stack, UPLINK_STACK_SIZE,
			uplink_thread_fn, NULL, NULL, NULL,
			K_PRIO_PREEMPT(11), 0, K_NO_WAIT);
	k_thread_name_set(&uplink_thread_data, "usb_uplink");

	k_work_init_delayable(&status_work, status_work_fn);
	k_work_init_delayable(&imu_work, imu_work_fn);
	k_work_reschedule(&status_work, K_MSEC(UPLOAD_PERIOD_MS));
	k_work_reschedule(&imu_work,    K_MSEC(UPLOAD_IMU_PERIOD_MS));

	LOG_INF("usb_uplink init OK (console output; TODO: USB CDC)");
	return 0;
}

#else /* !CONFIG_APP_FMR_USB_UPLINK */

int  usb_uplink_init(void) { return 0; }
void usbul_send_status(void) {}
void usbul_send_imu(void) {}

#endif /* CONFIG_APP_FMR_USB_UPLINK */
