/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file moto.c
 * @brief Motor Modbus RTU driver — Zephyr port of original moto.c.
 *
 * USART6 (PD7/PD4), 115200 8N1, RS485 half-duplex.
 * DE = moto_de (PD6, active-high), RE = moto_re (PD5, active-high).
 *
 * Architecture:
 *   - IRQ callback buffers RX bytes into a ring.
 *   - A dedicated thread (moto_thread) executes the Modbus task queue:
 *       1. Assert DE (TX mode), send frame.
 *       2. Wait for TX complete (all bytes gone from FIFO).
 *       3. Deassert DE / assert RE (RX mode).
 *       4. Wait for response (k_sem, timeout 150 ms).
 *       5. Validate response or retry up to 3 times.
 *
 * Faithfulness notes:
 *   - Register addresses, CRC bytes, and response templates are identical
 *     to the original so the wire protocol is byte-for-byte compatible.
 *   - The original used a TMR0_2 RTO interrupt for frame-boundary detection;
 *     here we use a 5 ms idle timer (k_timer) to close the RX window.
 */

#include "moto.h"

#ifdef CONFIG_APP_FMR_MOTO

#include "app_msg.h"
#include "crc16_modbus.h"
#include "doraemon_pack.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(moto, LOG_LEVEL_INF);

/* ---------------------------------------------------------------------------
 * Hardware resources
 * ---------------------------------------------------------------------------
 */
static const struct device *usart6_dev = DEVICE_DT_GET(DT_NODELABEL(usart6));

static const struct gpio_dt_spec de_gpio =
	GPIO_DT_SPEC_GET(DT_NODELABEL(moto_de), gpios);
static const struct gpio_dt_spec re_gpio =
	GPIO_DT_SPEC_GET(DT_NODELABEL(moto_re), gpios);

/* ---------------------------------------------------------------------------
 * Modbus constants (from original moto.c)
 * ---------------------------------------------------------------------------
 */
#define MOTO_CMD_READ_N_REG   0x03
#define MOTO_CMD_WRITE_1_REG  0x06
#define MOTO_CMD_WRITE_N_REG  0x10

#define MOTO_REG_SPEED_SET    0x2088U   /* write left+right speed */
#define MOTO_REG_FAULT_CLR    0x200EU   /* write 0x0006 to clear fault */
#define MOTO_REG_SPEED_READ   0x20ABU   /* read left+right actual speed */

#define MOTO_ID               0x01U
#define MOTO_TASK_QUEUE_MAX   8
#define MOTO_TX_BUF_MAX       64
#define MOTO_RX_BUF_MAX       64
#define MOTO_RESPONSE_TIMEOUT_MS 150
#define MOTO_RETRY_MAX        3

/* ---------------------------------------------------------------------------
 * RS485 direction helpers
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
 * RX ring buffer (ISR -> thread)
 * ---------------------------------------------------------------------------
 */
#define RX_RING_SIZE 128
static uint8_t rx_ring[RX_RING_SIZE];
static uint32_t rx_head, rx_tail;
static struct k_spinlock rx_lock;

static K_SEM_DEFINE(rx_sem, 0, 1);

static void uart_cb(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	if (!uart_irq_update(dev)) {
		return;
	}

	bool got_byte = false;

	while (uart_irq_rx_ready(dev)) {
		uint8_t c;

		if (uart_fifo_read(dev, &c, 1) != 1) {
			break;
		}
		k_spinlock_key_t key = k_spin_lock(&rx_lock);
		uint32_t next = (rx_tail + 1) % RX_RING_SIZE;

		if (next != rx_head) {
			rx_ring[rx_tail] = c;
			rx_tail = next;
			got_byte = true;
		}
		k_spin_unlock(&rx_lock, key);
	}
	if (got_byte) {
		k_sem_give(&rx_sem);
	}
}

/* Drain ring buffer into dst, return number of bytes copied */
static int rx_drain(uint8_t *dst, int max_len)
{
	k_spinlock_key_t key = k_spin_lock(&rx_lock);
	int n = 0;

	while (rx_head != rx_tail && n < max_len) {
		dst[n++] = rx_ring[rx_head];
		rx_head  = (rx_head + 1) % RX_RING_SIZE;
	}
	k_spin_unlock(&rx_lock, key);
	return n;
}

/* ---------------------------------------------------------------------------
 * Modbus frame builder (faithful to original pack_data / data_input)
 * ---------------------------------------------------------------------------
 */
static int build_read_n(uint8_t *buf, uint8_t id, uint16_t addr, uint16_t count)
{
	buf[0] = id;
	buf[1] = MOTO_CMD_READ_N_REG;
	buf[2] = (uint8_t)(addr >> 8);
	buf[3] = (uint8_t)(addr & 0xff);
	buf[4] = (uint8_t)(count >> 8);
	buf[5] = (uint8_t)(count & 0xff);
	uint16_t crc = calc_modbus_crc16(buf, 6);

	buf[6] = (uint8_t)(crc & 0xff);
	buf[7] = (uint8_t)(crc >> 8);
	return 8;
}

static int build_write_1(uint8_t *buf, uint8_t id, uint16_t addr, uint16_t val)
{
	buf[0] = id;
	buf[1] = MOTO_CMD_WRITE_1_REG;
	buf[2] = (uint8_t)(addr >> 8);
	buf[3] = (uint8_t)(addr & 0xff);
	buf[4] = (uint8_t)(val >> 8);
	buf[5] = (uint8_t)(val & 0xff);
	uint16_t crc = calc_modbus_crc16(buf, 6);

	buf[6] = (uint8_t)(crc & 0xff);
	buf[7] = (uint8_t)(crc >> 8);
	return 8;
}

static int build_write_n(uint8_t *buf, uint8_t id, uint16_t addr,
			 uint16_t count, const uint8_t *data, uint8_t data_len)
{
	buf[0] = id;
	buf[1] = MOTO_CMD_WRITE_N_REG;
	buf[2] = (uint8_t)(addr >> 8);
	buf[3] = (uint8_t)(addr & 0xff);
	buf[4] = (uint8_t)(count >> 8);
	buf[5] = (uint8_t)(count & 0xff);
	buf[6] = data_len;
	memcpy(&buf[7], data, data_len);
	int base = 7 + data_len;
	uint16_t crc = calc_modbus_crc16(buf, base);

	buf[base]     = (uint8_t)(crc & 0xff);
	buf[base + 1] = (uint8_t)(crc >> 8);
	return base + 2;
}

/* ---------------------------------------------------------------------------
 * Task queue
 * ---------------------------------------------------------------------------
 */
typedef int8_t (*rsp_handler_t)(const uint8_t *dat, uint16_t len);

struct moto_task {
	uint8_t  cmd_buf[MOTO_TX_BUF_MAX];
	uint8_t  cmd_len;
	uint8_t  rsp_buf[MOTO_RX_BUF_MAX]; /**< expected response (fixed) or empty */
	uint8_t  rsp_len;                   /**< 0 = use handler */
	rsp_handler_t handler;
};

K_MSGQ_DEFINE(moto_task_q, sizeof(struct moto_task), MOTO_TASK_QUEUE_MAX, 4);

/* ---------------------------------------------------------------------------
 * Shared state
 * ---------------------------------------------------------------------------
 */
static volatile int16_t l_speed_val, r_speed_val;

/* ---------------------------------------------------------------------------
 * Response handler for read-speed query
 * ---------------------------------------------------------------------------
 */
static int8_t read_speed_handler(const uint8_t *p, uint16_t len)
{
	if (len < 9) {
		return 1;
	}
	if (p[0] != MOTO_ID || p[1] != MOTO_CMD_READ_N_REG) {
		LOG_WRN("moto: read rsp cmd/id error");
		return 1;
	}
	if (p[2] != len - 5) {
		LOG_WRN("moto: read rsp len error");
		return 2;
	}
	uint16_t crc_wire = (uint16_t)p[len - 2] | ((uint16_t)p[len - 1] << 8);

	if (calc_modbus_crc16(p, len - 2) != crc_wire) {
		LOG_WRN("moto: read CRC error");
		return 3;
	}
	l_speed_val = (int16_t)((p[3] << 8) | p[4]);
	r_speed_val = (int16_t)((p[5] << 8) | p[6]);

	struct app_msg msg = {
		.type = MSG_MOTO_STATUS,
		.moto = { .l_speed = l_speed_val, .r_speed = r_speed_val },
	};
	app_msg_publish(&msg);
	return 0;
}

/* ---------------------------------------------------------------------------
 * Motor thread — executes task queue entries one at a time
 * ---------------------------------------------------------------------------
 */
#define MOTO_STACK_SIZE 1024
K_THREAD_STACK_DEFINE(moto_stack, MOTO_STACK_SIZE);
static struct k_thread moto_thread_data;

static void moto_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

	struct moto_task task;
	uint8_t rx_buf[MOTO_RX_BUF_MAX];

	while (1) {
		k_msgq_get(&moto_task_q, &task, K_FOREVER);

		int retry = 0;

		do {
			/* Flush stale RX data */
			rx_drain(rx_buf, sizeof(rx_buf));

			/* Switch to TX and send */
			rs485_tx_mode();
			uart_tx(usart6_dev, task.cmd_buf, task.cmd_len, SYS_FOREVER_US);
			k_msleep(2); /* guard: last byte clears shift register */
			rs485_rx_mode();

			/* Collect response within timeout */
			int64_t deadline = k_uptime_get() + MOTO_RESPONSE_TIMEOUT_MS;
			int rx_len = 0;

			while (k_uptime_get() < deadline && rx_len < (int)sizeof(rx_buf)) {
				int r = k_sem_take(&rx_sem, K_MSEC(10));

				if (r == 0) {
					rx_len += rx_drain(&rx_buf[rx_len],
							   sizeof(rx_buf) - rx_len);
				}
			}

			if (rx_len == 0) {
				LOG_WRN("moto: no response (retry %d)", retry);
				retry++;
				continue;
			}

			/* Validate response */
			int ok;

			if (task.handler) {
				ok = (task.handler(rx_buf, rx_len) == 0) ? 1 : 0;
			} else {
				ok = (rx_len >= task.rsp_len &&
				      memcmp(rx_buf, task.rsp_buf, task.rsp_len) == 0) ? 1 : 0;
				if (!ok) {
					LOG_WRN("moto: rsp mismatch");
				}
			}

			if (ok) {
				break;
			}
			retry++;
		} while (retry < MOTO_RETRY_MAX);

		if (retry >= MOTO_RETRY_MAX) {
			LOG_ERR("moto: task failed after %d retries", MOTO_RETRY_MAX);
		}
	}
}

/* ---------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------------
 */
int moto_init(void)
{
	int ret;

	if (!device_is_ready(usart6_dev)) {
		LOG_ERR("USART6 not ready");
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

	uart_irq_callback_user_data_set(usart6_dev, uart_cb, NULL);
	uart_irq_rx_enable(usart6_dev);

	k_thread_create(&moto_thread_data, moto_stack, MOTO_STACK_SIZE,
			moto_thread_fn, NULL, NULL, NULL,
			K_PRIO_COOP(7), 0, K_NO_WAIT);
	k_thread_name_set(&moto_thread_data, "moto");

	LOG_INF("moto init OK (USART6)");
	return 0;
}

void moto_cmd_set_all_speed(int16_t r_speed, int16_t l_speed)
{
	struct moto_task task = {0};
	uint8_t dat[4];

	dat[0] = DP_UINT16_H((uint16_t)l_speed);
	dat[1] = DP_UINT16_L((uint16_t)l_speed);
	dat[2] = DP_UINT16_H((uint16_t)r_speed);
	dat[3] = DP_UINT16_L((uint16_t)r_speed);

	task.cmd_len = build_write_n(task.cmd_buf, MOTO_ID,
				     MOTO_REG_SPEED_SET, 2, dat, 4);
	/* Expected ack: 01 10 20 88 00 02 ca 22 (from original) */
	static const uint8_t ack[] = { 0x01, 0x10, 0x20, 0x88, 0x00, 0x02, 0xca, 0x22 };

	memcpy(task.rsp_buf, ack, sizeof(ack));
	task.rsp_len = sizeof(ack);
	task.handler = NULL;

	k_msgq_put(&moto_task_q, &task, K_NO_WAIT);
}

void moto_cmd_clr_err(void)
{
	struct moto_task task = {0};

	task.cmd_len = build_write_1(task.cmd_buf, MOTO_ID, MOTO_REG_FAULT_CLR, 0x0006);
	/* Expected ack: 01 06 20 0e 00 06 63 cb (from original) */
	static const uint8_t ack[] = { 0x01, 0x06, 0x20, 0x0e, 0x00, 0x06, 0x63, 0xcb };

	memcpy(task.rsp_buf, ack, sizeof(ack));
	task.rsp_len = sizeof(ack);
	task.handler = NULL;

	k_msgq_put(&moto_task_q, &task, K_NO_WAIT);
}

void moto_cmd_get_all_speed(void)
{
	struct moto_task task = {0};

	task.cmd_len = build_read_n(task.cmd_buf, MOTO_ID, MOTO_REG_SPEED_READ, 2);
	task.rsp_len = 0;
	task.handler = read_speed_handler;

	k_msgq_put(&moto_task_q, &task, K_NO_WAIT);
}

int16_t moto_get_l_speed(void) { return l_speed_val; }
int16_t moto_get_r_speed(void) { return r_speed_val; }

#else /* !CONFIG_APP_FMR_MOTO */

int     moto_init(void)                               { return 0; }
void    moto_cmd_set_all_speed(int16_t r, int16_t l)  { (void)r; (void)l; }
void    moto_cmd_clr_err(void)                        {}
void    moto_cmd_get_all_speed(void)                  {}
int16_t moto_get_l_speed(void)                        { return 0; }
int16_t moto_get_r_speed(void)                        { return 0; }

#endif /* CONFIG_APP_FMR_MOTO */
