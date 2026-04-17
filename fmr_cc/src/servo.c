/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file servo.c
 * @brief Feetech FT servo driver — Zephyr port of original servo.c.
 *
 * USART7 (PE5/PE2), 115200 8N1, RS485 half-duplex.
 * DE = servo_de (PE4, active-high), RE = servo_re (PE3, active-high).
 *
 * FT protocol frame:
 *   0xFF 0xFF | ID(1) | LEN(1) | CMD(1) | DATA(N) | ~SUM(1)
 *   LEN  = N + 2  (includes CMD and checksum byte)
 *   SUM  = ~(ID + LEN + CMD + DATA[0..N-1]) & 0xFF
 *
 * Faithfulness notes:
 *   - Register 0x2A (42) = position+time+speed write (WRITE_DATA cmd 0x03)
 *   - Register 0x38 (56) = current position read (READ_DATA cmd 0x02, 2 bytes)
 *   - Position limits match original servo_status[] initialisation.
 */

#include "servo.h"

#ifdef CONFIG_APP_FMR_SERVO

#include "app_msg.h"
#include "doraemon_pack.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(servo, LOG_LEVEL_INF);

/* ---------------------------------------------------------------------------
 * Hardware resources
 * ---------------------------------------------------------------------------
 */
static const struct device *usart7_dev = DEVICE_DT_GET(DT_NODELABEL(usart7));

static const struct gpio_dt_spec de_gpio =
	GPIO_DT_SPEC_GET(DT_NODELABEL(servo_de), gpios);
static const struct gpio_dt_spec re_gpio =
	GPIO_DT_SPEC_GET(DT_NODELABEL(servo_re), gpios);

/* ---------------------------------------------------------------------------
 * FT protocol constants
 * ---------------------------------------------------------------------------
 */
#define FT_CMD_WRITE_DATA  0x03
#define FT_CMD_READ_DATA   0x02

#define SERVO_NUM          3
#define SERVO_CAM_POS_MAX  2700
#define SERVO_CAM_POS_MIN  1500
#define SERVO_ARM_POS_MAX  3118
#define SERVO_ARM_POS_MIN  2047

#define SERVO_TX_BUF_MAX   32
#define SERVO_RX_BUF_MAX   32
#define SERVO_TASK_MAX     12
#define SERVO_TIMEOUT_MS   150
#define SERVO_RETRY_MAX    3

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
 * Checksum
 * ---------------------------------------------------------------------------
 */
static uint8_t ft_checksum(const uint8_t *buf, int len)
{
	uint32_t s = 0;

	for (int i = 0; i < len; i++) {
		s += buf[i];
	}
	return ~((uint8_t)(s & 0xff));
}

/* ---------------------------------------------------------------------------
 * Frame builder
 * Build frame: FF FF ID LEN CMD DATA... ~SUM
 * Returns total frame length.
 * ---------------------------------------------------------------------------
 */
static int build_ft_frame(uint8_t *out, uint8_t id, uint8_t cmd,
			  const uint8_t *data, uint8_t data_len)
{
	out[0] = 0xFF;
	out[1] = 0xFF;
	out[2] = id;
	out[3] = data_len + 2; /* LEN = data + cmd + checksum */
	out[4] = cmd;
	memcpy(&out[5], data, data_len);
	/* checksum covers ID, LEN, CMD, DATA */
	out[5 + data_len] = ft_checksum(&out[2], 2 + 1 + data_len);
	return 6 + data_len;
}

/* ---------------------------------------------------------------------------
 * Servo status
 * ---------------------------------------------------------------------------
 */
struct servo_status {
	int16_t pos;
	int16_t pos_max;
	int16_t pos_min;
};

static struct servo_status s_status[SERVO_NUM] = {
	{ 0, SERVO_CAM_POS_MAX, SERVO_CAM_POS_MIN },
	{ 0, SERVO_ARM_POS_MAX, SERVO_ARM_POS_MIN },
	{ 0, SERVO_ARM_POS_MAX, SERVO_ARM_POS_MIN },
};

/* ---------------------------------------------------------------------------
 * Task queue
 * ---------------------------------------------------------------------------
 */
typedef int8_t (*servo_rsp_handler_t)(const uint8_t *dat, uint16_t len);

struct servo_task {
	uint8_t  cmd_buf[SERVO_TX_BUF_MAX];
	uint8_t  cmd_len;
	uint8_t  rsp_buf[SERVO_RX_BUF_MAX];
	uint8_t  rsp_len; /**< 0 = use handler */
	servo_rsp_handler_t handler;
};

K_MSGQ_DEFINE(servo_task_q, sizeof(struct servo_task), SERVO_TASK_MAX, 4);

/* ---------------------------------------------------------------------------
 * Response handler for read-position query
 * ---------------------------------------------------------------------------
 */
static int8_t read_pos_handler(const uint8_t *p, uint16_t len)
{
	if (len < 8) {
		return 1;
	}
	if (p[0] != 0xFF || p[1] != 0xFF) {
		LOG_WRN("servo: rsp header error");
		return 1;
	}
	if (p[3] != 4) {
		LOG_WRN("servo: rsp len error (expected 4, got %d)", p[3]);
		return 2;
	}
	uint8_t sum = ft_checksum(&p[2], 5);

	if (p[7] != sum) {
		LOG_WRN("servo: rsp checksum error");
		return 3;
	}
	uint16_t pos_raw = (uint16_t)p[5] | ((uint16_t)p[6] << 8);
	uint8_t  id      = p[2];

	if (id >= 1 && id <= SERVO_NUM) {
		s_status[id - 1].pos = *((int16_t *)(&pos_raw));

		struct app_msg msg = {
			.type        = MSG_SERVO_STATUS,
			.servo.id    = id,
			.servo.pos   = s_status[id - 1].pos,
		};
		app_msg_publish(&msg);
	}
	return 0;
}

/* ---------------------------------------------------------------------------
 * Servo thread
 * ---------------------------------------------------------------------------
 */
#define SERVO_STACK_SIZE 1024
K_THREAD_STACK_DEFINE(servo_stack, SERVO_STACK_SIZE);
static struct k_thread servo_thread_data;

static void servo_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

	struct servo_task task;
	uint8_t rx_buf[SERVO_RX_BUF_MAX];

	while (1) {
		k_msgq_get(&servo_task_q, &task, K_FOREVER);

		int retry = 0;

		do {
			rx_drain(rx_buf, sizeof(rx_buf));

			rs485_tx_mode();
			uart_tx(usart7_dev, task.cmd_buf, task.cmd_len, SYS_FOREVER_US);
			k_msleep(2);
			rs485_rx_mode();

			int64_t deadline = k_uptime_get() + SERVO_TIMEOUT_MS;
			int rx_len = 0;

			while (k_uptime_get() < deadline) {
				int r = k_sem_take(&rx_sem, K_MSEC(10));

				if (r == 0) {
					rx_len += rx_drain(&rx_buf[rx_len],
							   sizeof(rx_buf) - rx_len);
				}
			}

			if (rx_len == 0) {
				LOG_WRN("servo: no response (retry %d)", retry);
				retry++;
				continue;
			}

			int ok;

			if (task.handler) {
				ok = (task.handler(rx_buf, (uint16_t)rx_len) == 0);
			} else {
				ok = (rx_len >= task.rsp_len &&
				      memcmp(rx_buf, task.rsp_buf, task.rsp_len) == 0);
			}

			if (ok) {
				break;
			}
			LOG_WRN("servo: rsp mismatch (retry %d)", retry);
			retry++;
		} while (retry < SERVO_RETRY_MAX);

		if (retry >= SERVO_RETRY_MAX) {
			LOG_ERR("servo: task failed after %d retries", SERVO_RETRY_MAX);
		}
	}
}

/* ---------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------------
 */
int servo_init(void)
{
	int ret;

	if (!device_is_ready(usart7_dev)) {
		LOG_ERR("USART7 not ready");
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

	uart_irq_callback_user_data_set(usart7_dev, uart_cb, NULL);
	uart_irq_rx_enable(usart7_dev);

	k_thread_create(&servo_thread_data, servo_stack, SERVO_STACK_SIZE,
			servo_thread_fn, NULL, NULL, NULL,
			K_PRIO_COOP(7), 0, K_NO_WAIT);
	k_thread_name_set(&servo_thread_data, "servo");

	/* Initial position: centre (2047) for all servos — matches original */
	servo_cmd_set_pos_by_id(1, 2047, 0, 200);
	servo_cmd_set_pos_by_id(2, 2047, 0, 200);
	servo_cmd_set_pos_by_id(3, 2047, 0, 200);

	LOG_INF("servo init OK (USART7)");
	return 0;
}

void servo_cmd_set_pos_by_id(uint8_t id, int16_t pos, uint16_t time, int16_t speed)
{
	if (id < 1 || id > SERVO_NUM) {
		return;
	}

	struct servo_status *ss = &s_status[id - 1];

	if (pos > ss->pos_max) {
		pos = ss->pos_max;
	} else if (pos < ss->pos_min) {
		pos = ss->pos_min;
	}

	/* data layout: reg(0x2A) | pos_L | pos_H | time_L | time_H | spd_L | spd_H */
	uint8_t dat[7];

	dat[0] = 0x2A;
	dat[1] = (uint8_t)(*(uint16_t *)(&pos) & 0xff);
	dat[2] = (uint8_t)(*(uint16_t *)(&pos) >> 8);
	dat[3] = (uint8_t)(time & 0xff);
	dat[4] = (uint8_t)(time >> 8);
	dat[5] = (uint8_t)(*(uint16_t *)(&speed) & 0xff);
	dat[6] = (uint8_t)(*(uint16_t *)(&speed) >> 8);

	struct servo_task task = {0};

	task.cmd_len = build_ft_frame(task.cmd_buf, id, FT_CMD_WRITE_DATA, dat, 7);

	/* Expected ack: FF FF ID 02 00 ~sum */
	task.rsp_buf[0] = 0xFF;
	task.rsp_buf[1] = 0xFF;
	task.rsp_buf[2] = id;
	task.rsp_buf[3] = 0x02;
	task.rsp_buf[4] = 0x00;
	task.rsp_buf[5] = ft_checksum(&task.rsp_buf[2], 3);
	task.rsp_len    = 6;
	task.handler    = NULL;

	k_msgq_put(&servo_task_q, &task, K_NO_WAIT);
}

void servo_cmd_get_all_pos(void)
{
	/* Read 2 bytes from reg 0x38 (present position) for each servo */
	static const uint8_t read_dat[] = { 0x38, 0x02 };

	for (uint8_t id = 1; id <= SERVO_NUM; id++) {
		struct servo_task task = {0};

		task.cmd_len = build_ft_frame(task.cmd_buf, id,
					      FT_CMD_READ_DATA, read_dat, 2);
		task.rsp_len = 0;
		task.handler = read_pos_handler;
		k_msgq_put(&servo_task_q, &task, K_NO_WAIT);
	}
}

int16_t servo_get_pos_by_id(uint8_t id)
{
	if (id < 1 || id > SERVO_NUM) {
		return 0;
	}
	return s_status[id - 1].pos;
}

#else /* !CONFIG_APP_FMR_SERVO */

int     servo_init(void)                                         { return 0; }
void    servo_cmd_set_pos_by_id(uint8_t id, int16_t pos,
				uint16_t time, int16_t speed)
{
	(void)id; (void)pos; (void)time; (void)speed;
}
void    servo_cmd_get_all_pos(void)                              {}
int16_t servo_get_pos_by_id(uint8_t id)                         { (void)id; return 0; }

#endif /* CONFIG_APP_FMR_SERVO */
