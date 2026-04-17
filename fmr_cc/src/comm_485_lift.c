/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file comm_485_lift.c
 * @brief Lift RS485 driver — Zephyr port of original comm_485_1.c.
 *
 * USART5 (PA11/PA8), 115200 8N1, RS485 half-duplex.
 * DE = lift_de (PA10, active-high), RE = lift_re (PA9, active-high).
 *
 * Frame format (custom, NOT Modbus):
 *   TX/RX: 0x39 0x93 | ID(1) | CMD(1) | LEN(1) | DATA(LEN) | CRC16_H | CRC16_L
 *   CRC16-Modbus covers bytes starting from ID through end of DATA.
 *
 * The RX state machine parses incoming frames using the same byte-level
 * sequence as the original _execute_rx_data() function.
 */

#include "comm_485_lift.h"

#ifdef CONFIG_APP_FMR_LIFT

#include "app_msg.h"
#include "crc16_modbus.h"
#include "doraemon_pack.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(comm_485_lift, LOG_LEVEL_INF);

/* ---------------------------------------------------------------------------
 * Hardware resources
 * ---------------------------------------------------------------------------
 */
static const struct device *usart5_dev = DEVICE_DT_GET(DT_NODELABEL(usart5));

static const struct gpio_dt_spec de_gpio =
	GPIO_DT_SPEC_GET(DT_NODELABEL(lift_de), gpios);
static const struct gpio_dt_spec re_gpio =
	GPIO_DT_SPEC_GET(DT_NODELABEL(lift_re), gpios);

/* ---------------------------------------------------------------------------
 * Protocol constants (from original comm_485_1.h/.c)
 * ---------------------------------------------------------------------------
 */
#define LIFT_FRAME_HEAD1   0x39U
#define LIFT_FRAME_HEAD2   0x93U
#define LIFT_DEVICE_ID     0x01U

#define LIFT_CMD_SET_POS      0x03U
#define LIFT_CMD_GET_POS      0x05U
#define LIFT_CMD_CALIBRATE    0x07U
#define LIFT_CMD_GO_ZERO      0x08U
#define LIFT_CMD_GET_MAX      0x09U

#define LIFT_TASK_MAX      10
#define LIFT_TX_BUF_MAX    32
#define LIFT_RX_BUF_MAX    32
#define LIFT_TIMEOUT_MS    150
#define LIFT_RETRY_MAX     3

/* ---------------------------------------------------------------------------
 * RS485 direction
 * ---------------------------------------------------------------------------
 */
static inline void rs485_tx_mode(void)
{
	gpio_pin_set_dt(&de_gpio, 1);
	gpio_pin_set_dt(&re_gpio, 0);
}

static inline void rs485_rx_mode(void)
{
	gpio_pin_set_dt(&de_gpio, 0);
	gpio_pin_set_dt(&re_gpio, 1);
}

/* ---------------------------------------------------------------------------
 * RX ring buffer
 * ---------------------------------------------------------------------------
 */
#define RX_RING 128
static uint8_t rx_ring[RX_RING];
static uint32_t rx_head, rx_tail;
static struct k_spinlock rx_lock;
static K_SEM_DEFINE(rx_sem, 0, 1);

static void uart_cb(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	if (!uart_irq_update(dev)) {
		return;
	}

	bool got = false;

	while (uart_irq_rx_ready(dev)) {
		uint8_t c;

		if (uart_fifo_read(dev, &c, 1) != 1) {
			break;
		}
		k_spinlock_key_t key = k_spin_lock(&rx_lock);
		uint32_t next = (rx_tail + 1) % RX_RING;

		if (next != rx_head) {
			rx_ring[rx_tail] = c;
			rx_tail = next;
			got = true;
		}
		k_spin_unlock(&rx_lock, key);
	}
	if (got) {
		k_sem_give(&rx_sem);
	}
}

static int rx_drain(uint8_t *dst, int max)
{
	k_spinlock_key_t key = k_spin_lock(&rx_lock);
	int n = 0;

	while (rx_head != rx_tail && n < max) {
		dst[n++] = rx_ring[rx_head];
		rx_head  = (rx_head + 1) % RX_RING;
	}
	k_spin_unlock(&rx_lock, key);
	return n;
}

/* ---------------------------------------------------------------------------
 * Frame builder: 0x39 0x93 ID CMD LEN DATA... CRC_H CRC_L
 * CRC covers bytes from [ID] through [DATA end].
 * ---------------------------------------------------------------------------
 */
static int build_frame(uint8_t *out, uint8_t id, uint8_t cmd,
		       const uint8_t *data, uint8_t data_len)
{
	out[0] = LIFT_FRAME_HEAD1;
	out[1] = LIFT_FRAME_HEAD2;
	out[2] = id;
	out[3] = cmd;
	out[4] = data_len;
	if (data_len > 0) {
		memcpy(&out[5], data, data_len);
	}
	/* CRC over [id, cmd, len, data...] i.e. from out[2] */
	uint16_t crc = calc_modbus_crc16(&out[2], 3 + data_len);

	out[5 + data_len] = (uint8_t)(crc >> 8);
	out[6 + data_len] = (uint8_t)(crc & 0xff);
	return 7 + data_len;
}

/* ---------------------------------------------------------------------------
 * Lift state
 * ---------------------------------------------------------------------------
 */
static volatile uint32_t lift_pos;
static volatile uint32_t lift_calibrate;

/* ---------------------------------------------------------------------------
 * Task queue
 * ---------------------------------------------------------------------------
 */
typedef int8_t (*lift_handler_t)(const uint8_t *dat, uint16_t len);

struct lift_task {
	uint8_t  cmd_buf[LIFT_TX_BUF_MAX];
	uint8_t  cmd_len;
	uint8_t  rsp_cmd;    /**< expected response CMD byte (0=any/use handler) */
	lift_handler_t handler;
};

K_MSGQ_DEFINE(lift_task_q, sizeof(struct lift_task), LIFT_TASK_MAX, 4);

/* ---------------------------------------------------------------------------
 * RX frame parser (mirrors original _execute_rx_data byte-state machine)
 * Output: decoded [id, cmd, len, data...] into rq_buf, returns len or -1.
 * ---------------------------------------------------------------------------
 */
enum rs485_rx_state {
	RS485_RX_HEAD1 = 0,
	RS485_RX_HEAD2,
	RS485_RX_ID,
	RS485_RX_CMD,
	RS485_RX_LEN,
	RS485_RX_DATA,
	RS485_RX_CRCH,
	RS485_RX_CRCL,
};

static int parse_rx_frame(const uint8_t *src, int src_len,
			  uint8_t *out, uint16_t *out_len)
{
	static enum rs485_rx_state state = RS485_RX_HEAD1;
	static uint8_t rq[LIFT_RX_BUF_MAX];
	static uint16_t pkt_len, data_pos;
	static uint16_t crc;

	for (int i = 0; i < src_len; i++) {
		uint8_t b = src[i];

		switch (state) {
		case RS485_RX_HEAD1:
			if (b == LIFT_FRAME_HEAD1) {
				state = RS485_RX_HEAD2;
				data_pos = 0;
			}
			break;
		case RS485_RX_HEAD2:
			state = (b == LIFT_FRAME_HEAD2) ? RS485_RX_ID : RS485_RX_HEAD1;
			break;
		case RS485_RX_ID:
			rq[0] = b;
			state = RS485_RX_CMD;
			break;
		case RS485_RX_CMD:
			rq[1] = b;
			state = RS485_RX_LEN;
			break;
		case RS485_RX_LEN:
			rq[2]   = b;
			pkt_len = b;
			state   = (pkt_len == 0) ? RS485_RX_CRCH : RS485_RX_DATA;
			break;
		case RS485_RX_DATA:
			rq[3 + data_pos] = b;
			data_pos++;
			if (data_pos >= pkt_len) {
				state = RS485_RX_CRCH;
			}
			break;
		case RS485_RX_CRCH:
			crc   = (uint16_t)b << 8;
			state = RS485_RX_CRCL;
			break;
		case RS485_RX_CRCL:
			crc |= b;
			state = RS485_RX_HEAD1;
			if (calc_modbus_crc16(rq, pkt_len + 3) == crc) {
				memcpy(out, rq, pkt_len + 3);
				*out_len = pkt_len + 3;
				return 1;
			}
			LOG_WRN("lift: CRC error");
			return -1;
		}
	}
	return 0; /* incomplete */
}

/* ---------------------------------------------------------------------------
 * Response handlers
 * ---------------------------------------------------------------------------
 */
static int8_t get_pos_handler(const uint8_t *p, uint16_t len)
{
	if (len < 7) {
		return 1;
	}
	/* p[0]=id, p[1]=cmd(0x85), p[2]=len(4), p[3..6]=pos big-endian */
	lift_pos = ((uint32_t)p[3] << 24) | ((uint32_t)p[4] << 16) |
		   ((uint32_t)p[5] << 8) | p[6];

	struct app_msg msg = {
		.type     = MSG_LIFT_STATUS,
		.lift.pos = (int32_t)lift_pos,
	};
	app_msg_publish(&msg);
	return 0;
}

static int8_t get_max_handler(const uint8_t *p, uint16_t len)
{
	if (len < 7) {
		return 1;
	}
	lift_calibrate = ((uint32_t)p[3] << 24) | ((uint32_t)p[4] << 16) |
			 ((uint32_t)p[5] << 8) | p[6];
	return 0;
}

/* ---------------------------------------------------------------------------
 * Lift thread
 * ---------------------------------------------------------------------------
 */
#define LIFT_STACK_SIZE 1024
K_THREAD_STACK_DEFINE(lift_stack, LIFT_STACK_SIZE);
static struct k_thread lift_thread_data;

static void lift_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

	struct lift_task task;
	uint8_t rx_raw[LIFT_RX_BUF_MAX];
	uint8_t rx_frame[LIFT_RX_BUF_MAX];

	while (1) {
		k_msgq_get(&lift_task_q, &task, K_FOREVER);

		int retry = 0;

		do {
			rx_drain(rx_raw, sizeof(rx_raw)); /* flush stale */

			rs485_tx_mode();
			uart_tx(usart5_dev, task.cmd_buf, task.cmd_len, SYS_FOREVER_US);
			k_msleep(2);
			rs485_rx_mode();

			int64_t deadline = k_uptime_get() + LIFT_TIMEOUT_MS;
			int rx_len = 0;

			while (k_uptime_get() < deadline) {
				int r = k_sem_take(&rx_sem, K_MSEC(10));

				if (r == 0) {
					rx_len += rx_drain(&rx_raw[rx_len],
							   sizeof(rx_raw) - rx_len);
				}
			}

			if (rx_len == 0) {
				LOG_WRN("lift: no response (retry %d)", retry);
				retry++;
				continue;
			}

			uint16_t frame_len = 0;
			int pr = parse_rx_frame(rx_raw, rx_len, rx_frame, &frame_len);

			if (pr <= 0) {
				LOG_WRN("lift: parse error %d (retry %d)", pr, retry);
				retry++;
				continue;
			}

			int ok;

			if (task.handler) {
				ok = (task.handler(rx_frame, frame_len) == 0);
			} else {
				/* Expect response CMD = request CMD | 0x80 */
				ok = (rx_frame[1] == (task.rsp_cmd | 0x80));
			}

			if (ok) {
				break;
			}
			retry++;
		} while (retry < LIFT_RETRY_MAX);

		if (retry >= LIFT_RETRY_MAX) {
			LOG_ERR("lift: task failed after %d retries", LIFT_RETRY_MAX);
		}
	}
}

/* ---------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------------
 */
int comm_485_lift_init(void)
{
	int ret;

	if (!device_is_ready(usart5_dev)) {
		LOG_ERR("USART5 not ready");
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(&de_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret) {
		return ret;
	}
	ret = gpio_pin_configure_dt(&re_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret) {
		return ret;
	}
	rs485_rx_mode();

	uart_irq_callback_user_data_set(usart5_dev, uart_cb, NULL);
	uart_irq_rx_enable(usart5_dev);

	k_thread_create(&lift_thread_data, lift_stack, LIFT_STACK_SIZE,
			lift_thread_fn, NULL, NULL, NULL,
			K_PRIO_COOP(7), 0, K_NO_WAIT);
	k_thread_name_set(&lift_thread_data, "lift");

	LOG_INF("comm_485_lift init OK (USART5)");
	return 0;
}

void comm_485_1_cmd_set_lift_pos(int32_t pos)
{
	uint8_t dat[4];

	dat[0] = (uint8_t)((pos >> 24) & 0xff);
	dat[1] = (uint8_t)((pos >> 16) & 0xff);
	dat[2] = (uint8_t)((pos >> 8)  & 0xff);
	dat[3] = (uint8_t)(pos & 0xff);

	struct lift_task task = {0};

	task.cmd_len = build_frame(task.cmd_buf, LIFT_DEVICE_ID,
				   LIFT_CMD_SET_POS, dat, 4);
	task.rsp_cmd = LIFT_CMD_SET_POS;
	task.handler = NULL;
	k_msgq_put(&lift_task_q, &task, K_NO_WAIT);
}

void comm_485_1_cmd_get_lift_pos(void)
{
	struct lift_task task = {0};

	task.cmd_len = build_frame(task.cmd_buf, LIFT_DEVICE_ID,
				   LIFT_CMD_GET_POS, NULL, 0);
	task.rsp_cmd = 0;
	task.handler = get_pos_handler;
	k_msgq_put(&lift_task_q, &task, K_NO_WAIT);
}

void comm_485_1_cmd_calibrate(void)
{
	struct lift_task task = {0};

	task.cmd_len = build_frame(task.cmd_buf, LIFT_DEVICE_ID,
				   LIFT_CMD_CALIBRATE, NULL, 0);
	task.rsp_cmd = LIFT_CMD_CALIBRATE;
	task.handler = NULL;
	k_msgq_put(&lift_task_q, &task, K_NO_WAIT);
}

void comm_485_1_cmd_go_zero(void)
{
	struct lift_task task = {0};

	task.cmd_len = build_frame(task.cmd_buf, LIFT_DEVICE_ID,
				   LIFT_CMD_GO_ZERO, NULL, 0);
	task.rsp_cmd = LIFT_CMD_GO_ZERO;
	task.handler = NULL;
	k_msgq_put(&lift_task_q, &task, K_NO_WAIT);
}

void comm_485_1_cmd_get_lift_max(void)
{
	struct lift_task task = {0};

	task.cmd_len = build_frame(task.cmd_buf, LIFT_DEVICE_ID,
				   LIFT_CMD_GET_MAX, NULL, 0);
	task.rsp_cmd = 0;
	task.handler = get_max_handler;
	k_msgq_put(&lift_task_q, &task, K_NO_WAIT);
}

uint32_t comm_485_1_get_lift_pos(void)
{
	return lift_pos;
}

uint32_t comm_485_1_get_lift_calibrate_value(void)
{
	return lift_calibrate;
}

#else /* !CONFIG_APP_FMR_LIFT */

int      comm_485_lift_init(void)              { return 0; }
void     comm_485_1_cmd_set_lift_pos(int32_t p) { (void)p; }
void     comm_485_1_cmd_get_lift_pos(void)     {}
void     comm_485_1_cmd_calibrate(void)        {}
void     comm_485_1_cmd_go_zero(void)          {}
void     comm_485_1_cmd_get_lift_max(void)     {}
uint32_t comm_485_1_get_lift_pos(void)         { return 0; }
uint32_t comm_485_1_get_lift_calibrate_value(void) { return 0; }

#endif /* CONFIG_APP_FMR_LIFT */
