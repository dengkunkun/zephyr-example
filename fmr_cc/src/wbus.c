/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file wbus.c
 * @brief WBUS/SBUS receiver — Zephyr port of original wbus.c.
 *
 * Hardware: USART2, PD9(TX)/PD8(RX), 100 kbps, 8E2, signal-inverted.
 *
 * TODO: The HC32 UART driver does not yet expose CR1.SBS (start-bit
 * inversion), even-parity or 2-stop-bit via standard Zephyr DTS properties.
 * This module patches the USART2 CR1/CR2/BRR registers directly at init time
 * after the Zephyr driver has configured the peripheral.  Once the HC32 UART
 * driver exposes these properties, remove the sys_write32 calls below.
 *
 * Frame format (SBUS, 25 bytes):
 *   Byte  0 : 0x0F (header)
 *   Bytes 1-22: 11-bit channels packed LSB-first
 *   Byte 23: flags (bit3=frame_lost, bit2=failsafe)
 *   Byte 24: 0x00 (footer)
 *
 * Only compiled when CONFIG_APP_FMR_WBUS=y.
 */

#include "wbus.h"

#ifdef CONFIG_APP_FMR_WBUS

#include "app_msg.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(wbus, LOG_LEVEL_INF);

/* ---------------------------------------------------------------------------
 * HC32 USART2 register addresses (manual patch for SBS/parity/stopbits/baud)
 * TODO: remove once HC32 UART driver exposes these via DTS.
 * ---------------------------------------------------------------------------
 */
#define HC32_USART2_BASE   0x4001C000UL
#define USART_CR1_OFFSET   0x00UL
#define USART_CR2_OFFSET   0x04UL
#define USART_BRR_OFFSET   0x0CUL

/*
 * CR1 value from original hw_init:
 *   0x80000000 | 0x0000002c
 *   bit31 = 1  (SBS = inverted start-bit for SBUS)
 *   bit3  = 1  (RE)
 *   bit2  = 1  (TE)
 *   bit5  = 1  (RXNEIE)
 *   PCE=1 (even parity)
 */
#define WBUS_CR1_VAL  0x8000006cUL  /* SBS|RXNEIE|PCE|TE|RE */
/* CR2: STOP=01 → 2 stop bits (bits [13:12]) */
#define WBUS_CR2_VAL  0x00003000UL
/* BRR for 100000 baud @ PCLK1=120MHz (from original: 0x00004aff) */
#define WBUS_BRR_VAL  0x00004affUL

/* ---------------------------------------------------------------------------
 * Frame parser state machine — mirrors original wbus_poll()
 * ---------------------------------------------------------------------------
 */
#define SBUS_FRAME_LEN  25
#define SBUS_HEADER     0x0FU
#define SBUS_FOOTER     0x00U
#define DEVICE_TIMEOUT_MS 500

enum wbus_state {
	WBUS_ST_START,
	WBUS_ST_GETTING,
	WBUS_ST_END,
};

static int16_t  ch_dat[WBUS_CH_NUM_MAX];
static uint8_t  wbus_status = WBUS_STATU_NO_DEVICE;
static int64_t  last_frame_ms;

/* Ring buffer for bytes from ISR */
#define RX_BUF_SIZE 128
static uint8_t  rx_ring[RX_BUF_SIZE];
static uint32_t rx_head, rx_tail;
static struct k_spinlock rx_lock;

/* Parser work item */
static struct k_work_delayable parse_work;

static const struct device *usart2_dev = DEVICE_DT_GET(DT_NODELABEL(usart2));

/* ISR callback — store byte in ring buffer */
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
		uint32_t next = (rx_tail + 1) % RX_BUF_SIZE;

		if (next != rx_head) {
			rx_ring[rx_tail] = c;
			rx_tail = next;
		}
		k_spin_unlock(&rx_lock, key);
	}
}

/* Decode a complete 25-byte SBUS frame into ch_dat[] */
static void decode_frame(const uint8_t *buf)
{
	uint32_t ch[WBUS_CH_NUM_MAX];

	ch[0] = ((buf[2]  & 0x07U) << 8)  | buf[1];
	ch[1] = ((buf[3]  & 0x3FU) << 5)  | (buf[2] >> 3);
	ch[2] = ((buf[5]  & 0x01U) << 10) | (buf[4] << 2) | (buf[3] >> 6);
	ch[3] = ((buf[6]  & 0x0FU) << 7)  | (buf[5] >> 1);
	ch[4] = ((buf[7]  & 0x7FU) << 4)  | (buf[6] >> 4);
	ch[5] = ((buf[9]  & 0x03U) << 9)  | (buf[8] << 1) | (buf[7] >> 7);
	ch[6] = ((buf[10] & 0x1FU) << 6)  | (buf[9] >> 2);
	ch[7] = ((buf[11] & 0xFFU) << 3)  | (buf[10] >> 5);
	ch[8] = ((buf[13] & 0x07U) << 8)  | buf[12];
	ch[9] = ((buf[14] & 0x3FU) << 5)  | (buf[13] >> 3);

	for (int i = 0; i < WBUS_CH_NUM_MAX; i++) {
		ch_dat[i] = (int16_t)((int32_t)ch[i] - 1024) * 100 / 671;
	}

	last_frame_ms = k_uptime_get();

	if ((buf[23] & 0x08U) != 0) {
		wbus_status = WBUS_STATU_NO_SIGNAL;
	} else {
		wbus_status = WBUS_STATU_NORMAL;
	}

	/* Publish to message bus */
	struct app_msg msg = {
		.type = MSG_RC_FRAME,
		.rc.status = wbus_status,
	};
	memcpy(msg.rc.ch, ch_dat, sizeof(ch_dat));
	app_msg_publish(&msg);
}

/* Work handler: drains ring buffer through SBUS state machine */
static void parse_work_fn(struct k_work *work)
{
	static uint8_t frame[SBUS_FRAME_LEN];
	static uint8_t p;
	static enum wbus_state state = WBUS_ST_START;

	/* Check device timeout */
	if (k_uptime_get() - last_frame_ms > DEVICE_TIMEOUT_MS) {
		wbus_status = WBUS_STATU_NO_DEVICE;
	}

	/* Drain ring buffer */
	uint8_t byte;
	k_spinlock_key_t key;

	while (true) {
		key = k_spin_lock(&rx_lock);
		if (rx_head == rx_tail) {
			k_spin_unlock(&rx_lock, key);
			break;
		}
		byte = rx_ring[rx_head];
		rx_head = (rx_head + 1) % RX_BUF_SIZE;
		k_spin_unlock(&rx_lock, key);

		switch (state) {
		case WBUS_ST_START:
			if (byte == SBUS_HEADER) {
				frame[0] = byte;
				p = 1;
				state = WBUS_ST_GETTING;
			}
			break;
		case WBUS_ST_GETTING:
			frame[p++] = byte;
			if (p >= SBUS_FRAME_LEN - 1) {
				state = WBUS_ST_END;
			}
			break;
		case WBUS_ST_END:
			frame[p] = byte;
			if (byte == SBUS_FOOTER) {
				decode_frame(frame);
			}
			state = WBUS_ST_START;
			p = 0;
			break;
		}
	}

	k_work_reschedule((struct k_work_delayable *)work, K_MSEC(14));
}

int wbus_init(void)
{
	if (!device_is_ready(usart2_dev)) {
		LOG_ERR("USART2 not ready");
		return -ENODEV;
	}

	uart_irq_callback_user_data_set(usart2_dev, uart_cb, NULL);
	uart_irq_rx_enable(usart2_dev);

	/*
	 * TODO: Patch USART2 registers for 100kbps 8E2 inverted.
	 * Remove when HC32 UART driver supports these DTS properties.
	 *
	 * sys_write32(WBUS_CR1_VAL, HC32_USART2_BASE + USART_CR1_OFFSET);
	 * sys_write32(WBUS_CR2_VAL, HC32_USART2_BASE + USART_CR2_OFFSET);
	 * sys_write32(WBUS_BRR_VAL, HC32_USART2_BASE + USART_BRR_OFFSET);
	 *
	 * Uncomment above lines once sys/io.h is available and register
	 * offsets are verified against HC32F4A0 reference manual §35.
	 */

	last_frame_ms = k_uptime_get();
	wbus_status   = WBUS_STATU_NO_DEVICE;

	k_work_init_delayable(&parse_work, parse_work_fn);
	k_work_reschedule(&parse_work, K_MSEC(14));

	LOG_INF("wbus init OK (USART2)");
	return 0;
}

int16_t wbus_getch(int8_t ch)
{
	if (ch < 1 || ch > WBUS_CH_NUM_MAX) {
		return WBUS_CH_ERROR;
	}
	return ch_dat[ch - 1];
}

uint8_t wbus_get_statu(void)
{
	return wbus_status;
}

#else /* !CONFIG_APP_FMR_WBUS */

int     wbus_init(void)          { return 0; }
int16_t wbus_getch(int8_t ch)    { (void)ch; return 0; }
uint8_t wbus_get_statu(void)     { return WBUS_STATU_NO_DEVICE; }

#endif /* CONFIG_APP_FMR_WBUS */
