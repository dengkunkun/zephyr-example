/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * UART driver for HC32F460 USART (polling + interrupt + DMA modes).
 *
 * Uses direct register access for USART/DMA, while peripheral clocks and pin
 * muxing are handled via Zephyr's clock_control and pinctrl subsystems.
 *
 * Interrupt mode uses HC32F460 INTC controller to map USART interrupt
 * sources to NVIC IRQ lines via SEL registers.
 */

#define DT_DRV_COMPAT hdsc_hc32_usart

#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <hc32_uart.h>
#include <soc.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(uart_hc32, CONFIG_UART_LOG_LEVEL);

/* ---------- USART register offsets ---------- */
#define USART_OFF_SR   0x00U
#define USART_OFF_TDR  0x04U
#define USART_OFF_RDR  0x06U
#define USART_OFF_BRR  0x08U
#define USART_OFF_CR1  0x0CU
#define USART_OFF_CR2  0x10U
#define USART_OFF_CR3  0x14U
#define USART_OFF_PR   0x18U

/* INTC registers */
#define INTC_BASE       0x40051000UL
#define INTC_SEL(n)     (INTC_BASE + 0x05CU + (n) * 4U)

/* USART1 interrupt sources */
#define INT_SRC_USART1_EI   278U
#define INT_SRC_USART1_RI   279U
#define INT_SRC_USART1_TI   280U
#define INT_SRC_USART1_TCI  281U

/* USART2 interrupt sources */
#define INT_SRC_USART2_EI   283U
#define INT_SRC_USART2_RI   284U
#define INT_SRC_USART2_TI   285U
#define INT_SRC_USART2_TCI  286U

/* NVIC IRQ allocation for USART (avoid 0-15 which are for EXTINT/GPIO) */
#define USART_IRQ_BASE  16

struct uart_hc32_config {
	uint32_t base;
	uint32_t baud_rate;
	uint8_t  instance;  /* 1=USART1, 2=USART2 */
	uint16_t clock_id;
	const struct device *clock_dev;
	const struct pinctrl_dev_config *pcfg;
};

struct uart_hc32_data {
	uint32_t pm_saved_cr1;
	bool pm_suspended;
#ifdef CONFIG_UART_HC32_INTERRUPT
	uart_irq_callback_user_data_t user_cb;
	void *user_data;
	uint8_t irq_ri;   /* NVIC IRQ for RX full */
	uint8_t irq_ei;   /* NVIC IRQ for RX error */
	uint8_t irq_ti;   /* NVIC IRQ for TX empty */
	uint8_t irq_tci;  /* NVIC IRQ for TX complete */
#endif
#ifdef CONFIG_UART_HC32_DMA
	uart_callback_t async_cb;
	void *async_user_data;
	const uint8_t *tx_buf;
	size_t tx_len;
	bool tx_active;
	uint8_t *rx_buf;
	size_t rx_buf_len;
	size_t rx_offset;        /* bytes already reported to callback */
	uint8_t *rx_next_buf;
	size_t rx_next_buf_len;
	bool rx_active;
	uint8_t irq_dma_rx;
	uint8_t irq_dma_tx;
	struct k_timer rx_timer; /* periodic poll for partial RX data */
	bool rx_timer_paused;
#endif
};

static inline uint32_t usart_read32(const struct uart_hc32_config *cfg,
				    uint32_t off)
{
	return sys_read32(cfg->base + off);
}

static inline void usart_write32(const struct uart_hc32_config *cfg,
				 uint32_t off, uint32_t val)
{
	sys_write32(val, cfg->base + off);
}

static inline void usart_write16(const struct uart_hc32_config *cfg,
				 uint32_t off, uint16_t val)
{
	sys_write16(val, cfg->base + off);
}

/* ---------- Polling API ---------- */

static void uart_hc32_poll_out(const struct device *dev, unsigned char c)
{
	const struct uart_hc32_config *cfg = dev->config;

	while (!(usart_read32(cfg, USART_OFF_SR) & USART_SR_TXE)) {
	}
	usart_write16(cfg, USART_OFF_TDR, (uint16_t)c);
}

static int uart_hc32_poll_in(const struct device *dev, unsigned char *c)
{
	const struct uart_hc32_config *cfg = dev->config;

	if (!(usart_read32(cfg, USART_OFF_SR) & USART_SR_RXNE)) {
		return -1;
	}
	*c = (unsigned char)(sys_read16(cfg->base + USART_OFF_RDR) & 0xFFU);
	return 0;
}

static int uart_hc32_err_check(const struct device *dev)
{
	const struct uart_hc32_config *cfg = dev->config;
	int err = 0;

	uint32_t sr = usart_read32(cfg, USART_OFF_SR);

	if (sr & USART_SR_ORE) {
		err |= UART_ERROR_OVERRUN;
	}
	if (sr & USART_SR_FE) {
		err |= UART_ERROR_FRAMING;
	}
	if (sr & USART_SR_PE) {
		err |= UART_ERROR_PARITY;
	}

	if (err) {
		/* Clear errors by toggling RE */
		uint32_t cr1 = usart_read32(cfg, USART_OFF_CR1);
		usart_write32(cfg, USART_OFF_CR1, cr1 & ~USART_CR1_RE);
		usart_write32(cfg, USART_OFF_CR1, cr1 | USART_CR1_RE);
	}
	return err;
}

/* ---------- Interrupt API ---------- */

#ifdef CONFIG_UART_HC32_INTERRUPT

static void uart_hc32_isr(const void *arg)
{
	const struct device *dev = (const struct device *)arg;
	struct uart_hc32_data *data = dev->data;

	if (data->user_cb) {
		data->user_cb(dev, data->user_data);
	}
}

static int uart_hc32_fifo_fill(const struct device *dev,
			       const uint8_t *tx_data, int size)
{
	const struct uart_hc32_config *cfg = dev->config;

	if (size == 0) {
		return 0;
	}

	if (!(usart_read32(cfg, USART_OFF_SR) & USART_SR_TXE)) {
		return 0;
	}

	usart_write16(cfg, USART_OFF_TDR, (uint16_t)tx_data[0]);
	return 1;
}

static int uart_hc32_fifo_read(const struct device *dev,
			       uint8_t *rx_data, const int size)
{
	const struct uart_hc32_config *cfg = dev->config;

	if (size == 0) {
		return 0;
	}

	if (!(usart_read32(cfg, USART_OFF_SR) & USART_SR_RXNE)) {
		return 0;
	}

	rx_data[0] = (uint8_t)(sys_read16(cfg->base + USART_OFF_RDR) & 0xFFU);
	return 1;
}

static void uart_hc32_irq_tx_enable(const struct device *dev)
{
	const struct uart_hc32_config *cfg = dev->config;
	struct uart_hc32_data *data = dev->data;
	uint32_t cr1 = usart_read32(cfg, USART_OFF_CR1);

	/*
	 * Enable both TXE and TC interrupts.  TCIE provides reliable
	 * re-triggering: after each byte finishes, TC=1 fires TCI even
	 * if the INTC missed the TXE edge when TXEIE was first set.
	 */
	usart_write32(cfg, USART_OFF_CR1,
		      cr1 | USART_CR1_TXEIE | USART_CR1_TCIE);

	/* Kickstart: pend TI in NVIC in case TXE was already high */
	NVIC_SetPendingIRQ(data->irq_ti);
}

static void uart_hc32_irq_tx_disable(const struct device *dev)
{
	const struct uart_hc32_config *cfg = dev->config;
	struct uart_hc32_data *data = dev->data;
	uint32_t cr1 = usart_read32(cfg, USART_OFF_CR1);

	usart_write32(cfg, USART_OFF_CR1,
		      cr1 & ~(USART_CR1_TXEIE | USART_CR1_TCIE));

	/* Clear stale pending bits to prevent spurious ISR entry */
	NVIC_ClearPendingIRQ(data->irq_ti);
	NVIC_ClearPendingIRQ(data->irq_tci);
}

static int uart_hc32_irq_tx_ready(const struct device *dev)
{
	const struct uart_hc32_config *cfg = dev->config;
	uint32_t cr1 = usart_read32(cfg, USART_OFF_CR1);
	uint32_t sr = usart_read32(cfg, USART_OFF_SR);

	return (cr1 & USART_CR1_TXEIE) && (sr & USART_SR_TXE);
}

static int uart_hc32_irq_tx_complete(const struct device *dev)
{
	const struct uart_hc32_config *cfg = dev->config;

	return !!(usart_read32(cfg, USART_OFF_SR) & USART_SR_TC);
}

static void uart_hc32_irq_rx_enable(const struct device *dev)
{
	const struct uart_hc32_config *cfg = dev->config;
	uint32_t cr1 = usart_read32(cfg, USART_OFF_CR1);

	usart_write32(cfg, USART_OFF_CR1, cr1 | USART_CR1_RIE);
}

static void uart_hc32_irq_rx_disable(const struct device *dev)
{
	const struct uart_hc32_config *cfg = dev->config;
	uint32_t cr1 = usart_read32(cfg, USART_OFF_CR1);

	usart_write32(cfg, USART_OFF_CR1, cr1 & ~USART_CR1_RIE);
}

static int uart_hc32_irq_rx_ready(const struct device *dev)
{
	const struct uart_hc32_config *cfg = dev->config;

	return !!(usart_read32(cfg, USART_OFF_SR) & USART_SR_RXNE);
}

static int uart_hc32_irq_is_pending(const struct device *dev)
{
	return uart_hc32_irq_tx_ready(dev) || uart_hc32_irq_rx_ready(dev);
}

static int uart_hc32_irq_update(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 1;
}

static void uart_hc32_irq_callback_set(const struct device *dev,
					uart_irq_callback_user_data_t cb,
					void *user_data)
{
	struct uart_hc32_data *data = dev->data;

	data->user_cb = cb;
	data->user_data = user_data;
}

static void uart_hc32_irq_setup(const struct device *dev)
{
	const struct uart_hc32_config *cfg = dev->config;
	struct uart_hc32_data *data = dev->data;
	uint32_t int_src_ri, int_src_ei, int_src_ti, int_src_tci;
	uint8_t irq_base;
	static uint8_t next_irq = USART_IRQ_BASE;

	switch (cfg->instance) {
	case 1:
		int_src_ri  = INT_SRC_USART1_RI;
		int_src_ei  = INT_SRC_USART1_EI;
		int_src_ti  = INT_SRC_USART1_TI;
		int_src_tci = INT_SRC_USART1_TCI;
		break;
	case 2:
		int_src_ri  = INT_SRC_USART2_RI;
		int_src_ei  = INT_SRC_USART2_EI;
		int_src_ti  = INT_SRC_USART2_TI;
		int_src_tci = INT_SRC_USART2_TCI;
		break;
	case 3:
		int_src_ri  = INT_SRC_USART3_RI;
		int_src_ei  = INT_SRC_USART3_EI;
		int_src_ti  = INT_SRC_USART3_TI;
		int_src_tci = INT_SRC_USART3_TCI;
		break;
	case 4:
		int_src_ri  = INT_SRC_USART4_RI;
		int_src_ei  = INT_SRC_USART4_EI;
		int_src_ti  = INT_SRC_USART4_TI;
		int_src_tci = INT_SRC_USART4_TCI;
		break;
#if defined(INT_SRC_USART5_RI)
	case 5:
		int_src_ri  = INT_SRC_USART5_RI;
		int_src_ei  = INT_SRC_USART5_EI;
		int_src_ti  = INT_SRC_USART5_TI;
		int_src_tci = INT_SRC_USART5_TCI;
		break;
#endif
#if defined(INT_SRC_USART6_RI)
	case 6:
		int_src_ri  = INT_SRC_USART6_RI;
		int_src_ei  = INT_SRC_USART6_EI;
		int_src_ti  = INT_SRC_USART6_TI;
		int_src_tci = INT_SRC_USART6_TCI;
		break;
#endif
#if defined(INT_SRC_USART7_RI)
	case 7:
		int_src_ri  = INT_SRC_USART7_RI;
		int_src_ei  = INT_SRC_USART7_EI;
		int_src_ti  = INT_SRC_USART7_TI;
		int_src_tci = INT_SRC_USART7_TCI;
		break;
#endif
#if defined(INT_SRC_USART8_RI)
	case 8:
		int_src_ri  = INT_SRC_USART8_RI;
		int_src_ei  = INT_SRC_USART8_EI;
		int_src_ti  = INT_SRC_USART8_TI;
		int_src_tci = INT_SRC_USART8_TCI;
		break;
#endif
#if defined(INT_SRC_USART9_RI)
	case 9:
		int_src_ri  = INT_SRC_USART9_RI;
		int_src_ei  = INT_SRC_USART9_EI;
		int_src_ti  = INT_SRC_USART9_TI;
		int_src_tci = INT_SRC_USART9_TCI;
		break;
#endif
#if defined(INT_SRC_USART10_RI)
	case 10:
		int_src_ri  = INT_SRC_USART10_RI;
		int_src_ei  = INT_SRC_USART10_EI;
		int_src_ti  = INT_SRC_USART10_TI;
		int_src_tci = INT_SRC_USART10_TCI;
		break;
#endif
	default:
		LOG_ERR("unsupported USART instance %u", cfg->instance);
		return;
	}

	/* Allocate 4 consecutive NVIC IRQs for this USART instance */
	irq_base = next_irq;
	next_irq += 4;

	data->irq_ri  = irq_base;
	data->irq_ei  = irq_base + 1;
	data->irq_ti  = irq_base + 2;
	data->irq_tci = irq_base + 3;

	/* Map USART interrupt sources to NVIC IRQs via INTC SEL registers */
	sys_write32(int_src_ri,  INTC_SEL(data->irq_ri));
	sys_write32(int_src_ei,  INTC_SEL(data->irq_ei));
	sys_write32(int_src_ti,  INTC_SEL(data->irq_ti));
	sys_write32(int_src_tci, INTC_SEL(data->irq_tci));

	/* Connect all 4 interrupts to our single ISR */
	irq_connect_dynamic(data->irq_ri, 1, uart_hc32_isr, dev, 0);
	irq_connect_dynamic(data->irq_ei, 1, uart_hc32_isr, dev, 0);
	irq_connect_dynamic(data->irq_ti, 1, uart_hc32_isr, dev, 0);
	irq_connect_dynamic(data->irq_tci, 1, uart_hc32_isr, dev, 0);

	/* Enable NVIC IRQs for RX and TX */
	irq_enable(data->irq_ri);
	irq_enable(data->irq_ei);
	irq_enable(data->irq_ti);
	irq_enable(data->irq_tci);
}

#endif /* CONFIG_UART_HC32_INTERRUPT */

/* ---------- DMA Async API ---------- */

#ifdef CONFIG_UART_HC32_DMA

/* DMA base addresses */
#define DMA1_BASE        0x40053000UL
#define DMA2_BASE        0x40053400UL
#define AOS_BASE         0x40010800UL

/* DMA global registers (offsets from DMA base) */
#define DMA_EN_OFF       0x00U
#define DMA_INTSTAT1_OFF 0x08U
#define DMA_INTMASK1_OFF 0x10U
#define DMA_INTCLR1_OFF  0x18U
#define DMA_CHEN_OFF     0x1CU
#define DMA_SWREQ_OFF    0x30U

/* DMA per-channel registers (channel base = 0x40 + ch * 0x40) */
#define DMA_CH_SAR(ch)       (0x40U + (ch) * 0x40U + 0x00U)
#define DMA_CH_DAR(ch)       (0x40U + (ch) * 0x40U + 0x04U)
#define DMA_CH_DTCTL(ch)     (0x40U + (ch) * 0x40U + 0x08U)
#define DMA_CH_CHCTL(ch)     (0x40U + (ch) * 0x40U + 0x1CU)
#define DMA_CH_MONDTCTL(ch)  (0x40U + (ch) * 0x40U + 0x28U)

/* CHCTL bit fields */
#define DMA_CHCTL_SINC_INC  (1U << 0)
#define DMA_CHCTL_DINC_INC  (1U << 2)
#define DMA_CHCTL_HSIZE_8   (0U << 8)

/* TC flag bit per channel in INTSTAT1/INTCLR1/INTMASK1 */
#define DMA_TC_FLAG(ch)   BIT(ch)

/* AOS trigger select registers (relative to AOS_BASE) */
#define AOS_DMA1_TRGSEL(ch)  (0x14U + (ch) * 4U)
#define AOS_DMA2_TRGSEL(ch)  (0x24U + (ch) * 4U)

/* USART event sources for DMA triggers */
#define EVT_SRC_USART1_RI  279U
#define EVT_SRC_USART1_TI  280U
#define EVT_SRC_USART2_RI  284U
#define EVT_SRC_USART2_TI  285U

/* DMA transfer-complete interrupt sources via INTC */
#define DMA_INT_SRC_DMA1_TC0  32U
#define DMA_INT_SRC_DMA1_TC1  33U
#define DMA_INT_SRC_DMA2_TC0  36U
#define DMA_INT_SRC_DMA2_TC1  37U

/* FCG0 bits for DMA/AOS clock (clear bit to enable) */
#define FCG0_DMA1  0x00004000UL
#define FCG0_DMA2  0x00008000UL
#define FCG0_AOS   0x00020000UL

/* DMA channel: USART instance 1 → CH0, instance 2 → CH1 */
#define DMA_UART_CH(inst)  ((inst) - 1U)

/* NVIC IRQs for DMA (after USART's 16-19) */
#define DMA_IRQ_BASE  20U

static inline uint32_t dma_read(uint32_t dma_base, uint32_t off)
{
	return sys_read32(dma_base + off);
}

static inline void dma_write(uint32_t dma_base, uint32_t off, uint32_t val)
{
	sys_write32(val, dma_base + off);
}

static void uart_hc32_async_notify(const struct device *dev,
				   struct uart_event *evt)
{
	struct uart_hc32_data *data = dev->data;

	if (data->async_cb) {
		data->async_cb(dev, evt, data->async_user_data);
	}
}

/* Period for polling partial RX data (ms) */
#define RX_POLL_PERIOD_MS  5

/*
 * Timer callback: check DMA progress and fire UART_RX_RDY for any
 * new bytes received since the last check.  This is necessary because
 * DMA TC only fires when the entire buffer is full, but the shell
 * needs per-character (or near-real-time) notification.
 */
static void uart_hc32_rx_timer_handler(struct k_timer *timer)
{
	const struct device *dev = k_timer_user_data_get(timer);
	const struct uart_hc32_config *cfg = dev->config;
	struct uart_hc32_data *data = dev->data;
	uint8_t ch = DMA_UART_CH(cfg->instance);

	if (!data->rx_active) {
		return;
	}

	uint32_t mondtctl = dma_read(DMA1_BASE, DMA_CH_MONDTCTL(ch));
	size_t remaining = (mondtctl >> 16) & 0xFFFFU;
	size_t received = data->rx_buf_len > remaining ?
			  data->rx_buf_len - remaining : 0;

	if (received > data->rx_offset) {
		struct uart_event evt = {
			.type = UART_RX_RDY,
			.data.rx.buf = data->rx_buf,
			.data.rx.len = received - data->rx_offset,
			.data.rx.offset = data->rx_offset,
		};
		data->rx_offset = received;
		uart_hc32_async_notify(dev, &evt);
	}
}

/* DMA TX completion ISR — all bytes transferred */
static void uart_hc32_dma_tx_isr(const void *arg)
{
	const struct device *dev = (const struct device *)arg;
	const struct uart_hc32_config *cfg = dev->config;
	struct uart_hc32_data *data = dev->data;
	uint8_t ch = DMA_UART_CH(cfg->instance);

	dma_write(DMA2_BASE, DMA_INTCLR1_OFF, DMA_TC_FLAG(ch));
	uint32_t chen = dma_read(DMA2_BASE, DMA_CHEN_OFF);
	dma_write(DMA2_BASE, DMA_CHEN_OFF, chen & ~BIT(ch));

	if (data->tx_active) {
		data->tx_active = false;
		struct uart_event evt = {
			.type = UART_TX_DONE,
			.data.tx.buf = data->tx_buf,
			.data.tx.len = data->tx_len,
		};
		uart_hc32_async_notify(dev, &evt);
	}
}

/* DMA RX completion ISR — receive buffer full */
static void uart_hc32_dma_rx_isr(const void *arg)
{
	const struct device *dev = (const struct device *)arg;
	const struct uart_hc32_config *cfg = dev->config;
	struct uart_hc32_data *data = dev->data;
	uint8_t ch = DMA_UART_CH(cfg->instance);

	dma_write(DMA1_BASE, DMA_INTCLR1_OFF, DMA_TC_FLAG(ch));

	if (!data->rx_active) {
		return;
	}

	k_timer_stop(&data->rx_timer);

	/* Report any bytes not yet reported by the timer */
	size_t unreported = data->rx_buf_len - data->rx_offset;
	if (unreported > 0) {
		struct uart_event rdy_evt = {
			.type = UART_RX_RDY,
			.data.rx.buf = data->rx_buf,
			.data.rx.len = unreported,
			.data.rx.offset = data->rx_offset,
		};
		uart_hc32_async_notify(dev, &rdy_evt);
	}

	struct uart_event rel_evt = {
		.type = UART_RX_BUF_RELEASED,
		.data.rx_buf.buf = data->rx_buf,
	};
	uart_hc32_async_notify(dev, &rel_evt);

	struct uart_event req_evt = {
		.type = UART_RX_BUF_REQUEST,
	};
	uart_hc32_async_notify(dev, &req_evt);

	/* Continue with next buffer if application provided one */
	if (data->rx_next_buf != NULL && data->rx_next_buf_len > 0) {
		data->rx_buf = data->rx_next_buf;
		data->rx_buf_len = data->rx_next_buf_len;
		data->rx_offset = 0;
		data->rx_next_buf = NULL;
		data->rx_next_buf_len = 0;

		dma_write(DMA1_BASE, DMA_CH_DAR(ch), (uint32_t)data->rx_buf);
		dma_write(DMA1_BASE, DMA_CH_DTCTL(ch),
			  ((uint32_t)data->rx_buf_len << 16) | 1U);
		uint32_t chen_val = dma_read(DMA1_BASE, DMA_CHEN_OFF);
		dma_write(DMA1_BASE, DMA_CHEN_OFF, chen_val | BIT(ch));

		if (!data->rx_timer_paused) {
			k_timer_start(&data->rx_timer,
				      K_MSEC(RX_POLL_PERIOD_MS),
				      K_MSEC(RX_POLL_PERIOD_MS));
		}
	} else {
		data->rx_active = false;
		struct uart_event dis_evt = {
			.type = UART_RX_DISABLED,
		};
		uart_hc32_async_notify(dev, &dis_evt);
	}
}

static int uart_hc32_async_callback_set(const struct device *dev,
					 uart_callback_t callback,
					 void *user_data)
{
	struct uart_hc32_data *data = dev->data;

	data->async_cb = callback;
	data->async_user_data = user_data;
	return 0;
}

static int uart_hc32_async_tx(const struct device *dev,
			      const uint8_t *buf, size_t len,
			      int32_t timeout)
{
	const struct uart_hc32_config *cfg = dev->config;
	struct uart_hc32_data *data = dev->data;
	uint8_t ch = DMA_UART_CH(cfg->instance);

	ARG_UNUSED(timeout);

	if (data->tx_active) {
		return -EBUSY;
	}

	data->tx_buf = buf;
	data->tx_len = len;
	data->tx_active = true;

	/* Wait for any previous transmission to complete (e.g. poll_out) */
	while (!(usart_read32(cfg, USART_OFF_SR) & USART_SR_TC)) {
	}

	/*
	 * Disable TE before configuring DMA.  Re-enabling TE later generates
	 * a fresh TXE 0→1 edge that the AOS routes to DMA2 as a trigger.
	 * SWREQ does not work when an AOS hardware trigger is configured.
	 */
	uint32_t cr1 = usart_read32(cfg, USART_OFF_CR1);
	usart_write32(cfg, USART_OFF_CR1, cr1 & ~USART_CR1_TE);

	/* DMA2 CHn: SAR=buffer(inc), DAR=TDR(fixed), 8-bit */
	dma_write(DMA2_BASE, DMA_CH_SAR(ch), (uint32_t)buf);
	dma_write(DMA2_BASE, DMA_CH_DAR(ch), cfg->base + USART_OFF_TDR);
	dma_write(DMA2_BASE, DMA_CH_DTCTL(ch), ((uint32_t)len << 16) | 1U);
	dma_write(DMA2_BASE, DMA_CH_CHCTL(ch),
		  DMA_CHCTL_SINC_INC | DMA_CHCTL_HSIZE_8 | DMA_CHCTL_IE);

	dma_write(DMA2_BASE, DMA_INTCLR1_OFF, DMA_TC_FLAG(ch));
	uint32_t mask = dma_read(DMA2_BASE, DMA_INTMASK1_OFF);
	dma_write(DMA2_BASE, DMA_INTMASK1_OFF, mask & ~DMA_TC_FLAG(ch));

	/* Enable DMA channel first, then re-enable TE to trigger DMA */
	uint32_t chen = dma_read(DMA2_BASE, DMA_CHEN_OFF);
	dma_write(DMA2_BASE, DMA_CHEN_OFF, chen | BIT(ch));

	usart_write32(cfg, USART_OFF_CR1, cr1 | USART_CR1_TE);

	return 0;
}

static int uart_hc32_async_tx_abort(const struct device *dev)
{
	const struct uart_hc32_config *cfg = dev->config;
	struct uart_hc32_data *data = dev->data;
	uint8_t ch = DMA_UART_CH(cfg->instance);

	if (!data->tx_active) {
		return -EFAULT;
	}

	uint32_t chen = dma_read(DMA2_BASE, DMA_CHEN_OFF);
	dma_write(DMA2_BASE, DMA_CHEN_OFF, chen & ~BIT(ch));

	uint32_t mondtctl = dma_read(DMA2_BASE, DMA_CH_MONDTCTL(ch));
	size_t remaining = (mondtctl >> 16) & 0xFFFFU;
	size_t sent = data->tx_len > remaining ? data->tx_len - remaining : 0;

	data->tx_active = false;

	struct uart_event evt = {
		.type = UART_TX_ABORTED,
		.data.tx.buf = data->tx_buf,
		.data.tx.len = sent,
	};
	uart_hc32_async_notify(dev, &evt);

	return 0;
}

static int uart_hc32_async_rx_enable(const struct device *dev,
				     uint8_t *buf, size_t len,
				     int32_t timeout)
{
	const struct uart_hc32_config *cfg = dev->config;
	struct uart_hc32_data *data = dev->data;
	uint8_t ch = DMA_UART_CH(cfg->instance);

	ARG_UNUSED(timeout);

	if (data->rx_active) {
		return -EBUSY;
	}

	data->rx_buf = buf;
	data->rx_buf_len = len;
	data->rx_offset = 0;
	data->rx_next_buf = NULL;
	data->rx_next_buf_len = 0;
	data->rx_active = true;
	data->rx_timer_paused = false;

	/* DMA1 CHn: SAR=RDR(fixed), DAR=buffer(inc), 8-bit */
	dma_write(DMA1_BASE, DMA_CH_SAR(ch), cfg->base + USART_OFF_RDR);
	dma_write(DMA1_BASE, DMA_CH_DAR(ch), (uint32_t)buf);
	dma_write(DMA1_BASE, DMA_CH_DTCTL(ch), ((uint32_t)len << 16) | 1U);
	dma_write(DMA1_BASE, DMA_CH_CHCTL(ch),
		  DMA_CHCTL_DINC_INC | DMA_CHCTL_HSIZE_8 | DMA_CHCTL_IE);

	dma_write(DMA1_BASE, DMA_INTCLR1_OFF, DMA_TC_FLAG(ch));
	uint32_t mask = dma_read(DMA1_BASE, DMA_INTMASK1_OFF);
	dma_write(DMA1_BASE, DMA_INTMASK1_OFF, mask & ~DMA_TC_FLAG(ch));

	uint32_t chen_val = dma_read(DMA1_BASE, DMA_CHEN_OFF);
	dma_write(DMA1_BASE, DMA_CHEN_OFF, chen_val | BIT(ch));

	/* Start periodic timer to detect partial RX data */
	k_timer_start(&data->rx_timer,
		      K_MSEC(RX_POLL_PERIOD_MS),
		      K_MSEC(RX_POLL_PERIOD_MS));

	return 0;
}

static int uart_hc32_async_rx_buf_rsp(const struct device *dev,
				      uint8_t *buf, size_t len)
{
	struct uart_hc32_data *data = dev->data;

	data->rx_next_buf = buf;
	data->rx_next_buf_len = len;
	return 0;
}

static int uart_hc32_async_rx_disable(const struct device *dev)
{
	const struct uart_hc32_config *cfg = dev->config;
	struct uart_hc32_data *data = dev->data;
	uint8_t ch = DMA_UART_CH(cfg->instance);

	if (!data->rx_active) {
		return -EFAULT;
	}

	k_timer_stop(&data->rx_timer);

	uint32_t chen = dma_read(DMA1_BASE, DMA_CHEN_OFF);
	dma_write(DMA1_BASE, DMA_CHEN_OFF, chen & ~BIT(ch));

	uint32_t mondtctl = dma_read(DMA1_BASE, DMA_CH_MONDTCTL(ch));
	size_t remaining = (mondtctl >> 16) & 0xFFFFU;
	size_t received = data->rx_buf_len > remaining ?
			  data->rx_buf_len - remaining : 0;

	data->rx_active = false;

	if (received > 0) {
		struct uart_event rdy_evt = {
			.type = UART_RX_RDY,
			.data.rx.buf = data->rx_buf,
			.data.rx.len = received,
			.data.rx.offset = 0,
		};
		uart_hc32_async_notify(dev, &rdy_evt);
	}

	struct uart_event rel_evt = {
		.type = UART_RX_BUF_RELEASED,
		.data.rx_buf.buf = data->rx_buf,
	};
	uart_hc32_async_notify(dev, &rel_evt);

	struct uart_event dis_evt = {
		.type = UART_RX_DISABLED,
	};
	uart_hc32_async_notify(dev, &dis_evt);

	return 0;
}

/* Set up DMA channels and INTC mapping for this USART instance */
static void uart_hc32_dma_setup(const struct device *dev)
{
	const struct uart_hc32_config *cfg = dev->config;
	struct uart_hc32_data *data = dev->data;
	uint8_t ch = DMA_UART_CH(cfg->instance);
	uint32_t evt_ri, evt_ti, isrc_rx_tc, isrc_tx_tc;
	static uint8_t next_dma_irq = DMA_IRQ_BASE;

	/* Initialize RX poll timer */
	k_timer_init(&data->rx_timer, uart_hc32_rx_timer_handler, NULL);
	k_timer_user_data_set(&data->rx_timer, (void *)dev);
	data->rx_timer_paused = false;

	/* Enable DMA1, DMA2, AOS clocks */
	CM_PWC->FCG0 &= ~(FCG0_DMA1 | FCG0_DMA2 | FCG0_AOS);

	/* Enable DMA units globally */
	dma_write(DMA1_BASE, DMA_EN_OFF, 1U);
	dma_write(DMA2_BASE, DMA_EN_OFF, 1U);

	if (cfg->instance == 1) {
		evt_ri = EVT_SRC_USART1_RI;
		evt_ti = EVT_SRC_USART1_TI;
		isrc_rx_tc = DMA_INT_SRC_DMA1_TC0;
		isrc_tx_tc = DMA_INT_SRC_DMA2_TC0;
	} else {
		evt_ri = EVT_SRC_USART2_RI;
		evt_ti = EVT_SRC_USART2_TI;
		isrc_rx_tc = DMA_INT_SRC_DMA1_TC1;
		isrc_tx_tc = DMA_INT_SRC_DMA2_TC1;
	}

	/* AOS: route USART events to DMA trigger inputs */
	sys_write32(evt_ri & 0x1FFU, AOS_BASE + AOS_DMA1_TRGSEL(ch));
	sys_write32(evt_ti & 0x1FFU, AOS_BASE + AOS_DMA2_TRGSEL(ch));

	/* Allocate NVIC IRQs for DMA completion interrupts */
	data->irq_dma_rx = next_dma_irq++;
	data->irq_dma_tx = next_dma_irq++;

	sys_write32(isrc_rx_tc, INTC_SEL(data->irq_dma_rx));
	sys_write32(isrc_tx_tc, INTC_SEL(data->irq_dma_tx));

	irq_connect_dynamic(data->irq_dma_rx, 1,
			    uart_hc32_dma_rx_isr, dev, 0);
	irq_connect_dynamic(data->irq_dma_tx, 1,
			    uart_hc32_dma_tx_isr, dev, 0);

	irq_enable(data->irq_dma_rx);
	irq_enable(data->irq_dma_tx);

	LOG_DBG("USART%u DMA: RX=DMA1_CH%u TX=DMA2_CH%u IRQ=%u/%u",
		cfg->instance, ch, ch, data->irq_dma_rx, data->irq_dma_tx);
}

#endif /* CONFIG_UART_HC32_DMA */

/* ---------- Baud rate ---------- */

static int uart_hc32_set_baud(const struct uart_hc32_config *cfg)
{
	uint32_t pclk1;
	static const uint32_t pr_div[] = {1, 4, 16, 64};
	uint32_t pr;

	if (clock_control_get_rate(cfg->clock_dev,
				   (clock_control_subsys_t)(uintptr_t)cfg->clock_id,
				   &pclk1) < 0) {
		return -EIO;
	}

	for (pr = 0U; pr < 4U; pr++) {
		uint32_t usart_clk = pclk1 / pr_div[pr];
		uint32_t denom = 8U * cfg->baud_rate;

		if (denom == 0U) {
			return -EINVAL;
		}
		uint32_t div_int = usart_clk / denom;

		if (div_int == 0U) {
			continue;
		}
		div_int -= 1U;
		if (div_int > 0xFFU) {
			continue;
		}

		/* Calculate fractional part */
		uint64_t num = (uint64_t)256U * 8U * (div_int + 1U) * cfg->baud_rate;
		uint32_t frac_raw = (uint32_t)((num + usart_clk / 2U) / usart_clk);
		int32_t div_frac = (int32_t)frac_raw - 128;

		uint32_t brr;
		bool use_frac = false;
		uint32_t cr1;

		if (div_frac >= 0 && div_frac <= 0x7F) {
			brr = ((div_int & 0xFFU) << 8) | (div_frac & 0x7FU);
			use_frac = true;

			uint32_t actual = (uint32_t)((uint64_t)usart_clk *
				(128U + div_frac) / (2048U * (div_int + 1U)));
			int32_t err_ppm = (int32_t)((int64_t)(actual - cfg->baud_rate) *
				10000 / (int64_t)cfg->baud_rate);
			if (err_ppm > 250 || err_ppm < -250) {
				continue;
			}
		} else {
			uint32_t div_int_r = ((usart_clk * 10U / denom + 5U) / 10U);

			if (div_int_r == 0U) {
				continue;
			}
			div_int_r -= 1U;
			if (div_int_r > 0xFFU) {
				continue;
			}
			brr = (div_int_r & 0xFFU) << 8;

			uint32_t actual = usart_clk / (8U * (div_int_r + 1U));
			int32_t err_ppm = (int32_t)((int64_t)(actual - cfg->baud_rate) *
				10000 / (int64_t)cfg->baud_rate);
			if (err_ppm > 250 || err_ppm < -250) {
				continue;
			}
		}

		usart_write32(cfg, USART_OFF_PR, pr);
		usart_write32(cfg, USART_OFF_BRR, brr);
		cr1 = usart_read32(cfg, USART_OFF_CR1);
		cr1 &= ~USART_CR1_FBME;
		if (use_frac) {
			cr1 |= USART_CR1_FBME;
		}
		usart_write32(cfg, USART_OFF_CR1, cr1);
		return 0;
	}
	return -EINVAL;
}

int hc32_uart_reconfigure(const struct device *dev)
{
	const struct uart_hc32_config *cfg;
	uint32_t cr1;
	int ret;

	if (dev == NULL) {
		return -ENODEV;
	}

	cfg = dev->config;
	cr1 = usart_read32(cfg, USART_OFF_CR1);
	usart_write32(cfg, USART_OFF_CR1, cr1 & ~(USART_CR1_TE | USART_CR1_RE));
	ret = uart_hc32_set_baud(cfg);
	usart_write32(cfg, USART_OFF_CR1, cr1);

	return ret;
}

int hc32_uart_pm_control(const struct device *dev, bool suspended)
{
	const struct uart_hc32_config *cfg;
	struct uart_hc32_data *data;
	uint32_t cr1;

	if (dev == NULL) {
		return -ENODEV;
	}

	cfg = dev->config;
	data = dev->data;

	if (suspended) {
		if (data->pm_suspended) {
			return 0;
		}

		while (!(usart_read32(cfg, USART_OFF_SR) & USART_SR_TC)) {
		}

		cr1 = usart_read32(cfg, USART_OFF_CR1);
		data->pm_saved_cr1 = cr1;
		usart_write32(cfg, USART_OFF_CR1,
			      cr1 & ~(USART_CR1_TXEIE | USART_CR1_TCIE |
				      USART_CR1_RIE | USART_CR1_TE |
				      USART_CR1_RE));
#ifdef CONFIG_UART_HC32_INTERRUPT
		if (data->irq_ri != 0U) {
			NVIC_ClearPendingIRQ(data->irq_ri);
		}
		if (data->irq_ei != 0U) {
			NVIC_ClearPendingIRQ(data->irq_ei);
		}
		if (data->irq_ti != 0U) {
			NVIC_ClearPendingIRQ(data->irq_ti);
		}
		if (data->irq_tci != 0U) {
			NVIC_ClearPendingIRQ(data->irq_tci);
		}
#endif
#ifdef CONFIG_UART_HC32_DMA
		data->rx_timer_paused = true;
		if (data->rx_active) {
			k_timer_stop(&data->rx_timer);
		}
		if (data->irq_dma_rx != 0U) {
			NVIC_ClearPendingIRQ(data->irq_dma_rx);
		}
		if (data->irq_dma_tx != 0U) {
			NVIC_ClearPendingIRQ(data->irq_dma_tx);
		}
#endif
		data->pm_suspended = true;
		return 0;
	}

	if (!data->pm_suspended) {
		return 0;
	}

	usart_write32(cfg, USART_OFF_CR1, data->pm_saved_cr1);
#ifdef CONFIG_UART_HC32_DMA
	data->rx_timer_paused = false;
	if (data->rx_active) {
		k_timer_start(&data->rx_timer,
			      K_MSEC(RX_POLL_PERIOD_MS),
			      K_MSEC(RX_POLL_PERIOD_MS));
	}
#endif
	data->pm_suspended = false;

	return 0;
}

/* ---------- Init ---------- */

static int uart_hc32_init(const struct device *dev)
{
	const struct uart_hc32_config *cfg = dev->config;

	if (!device_is_ready(cfg->clock_dev)) {
		return -ENODEV;
	}

	if (clock_control_on(cfg->clock_dev,
			     (clock_control_subsys_t)(uintptr_t)cfg->clock_id) < 0) {
		return -EIO;
	}

	if (pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT) < 0) {
		return -EIO;
	}

	/* Configure USART: 8N1, 8× oversampling, falling-edge start bit */
	usart_write32(cfg, USART_OFF_CR1, USART_CR1_OVER8 | USART_CR1_SBS);
	usart_write32(cfg, USART_OFF_CR2, 0U);
	usart_write32(cfg, USART_OFF_CR3, 0U);

	/* Set baud rate */
	int ret = uart_hc32_set_baud(cfg);
	if (ret != 0) {
		LOG_ERR("Failed to set baud rate %u", cfg->baud_rate);
		return ret;
	}

	/* Enable TX and RX */
	uint32_t cr1 = usart_read32(cfg, USART_OFF_CR1);
	usart_write32(cfg, USART_OFF_CR1, cr1 | USART_CR1_TE | USART_CR1_RE);

#ifdef CONFIG_UART_HC32_INTERRUPT
	/* Set up INTC interrupt mapping and NVIC */
	uart_hc32_irq_setup(dev);
#endif

#ifdef CONFIG_UART_HC32_DMA
	/* Set up DMA channels and interrupts */
	uart_hc32_dma_setup(dev);
#endif

	return 0;
}

/* ---------- API struct ---------- */

static DEVICE_API(uart, uart_hc32_driver_api) = {
	.poll_in = uart_hc32_poll_in,
	.poll_out = uart_hc32_poll_out,
	.err_check = uart_hc32_err_check,
#ifdef CONFIG_UART_HC32_INTERRUPT
	.fifo_fill = uart_hc32_fifo_fill,
	.fifo_read = uart_hc32_fifo_read,
	.irq_tx_enable = uart_hc32_irq_tx_enable,
	.irq_tx_disable = uart_hc32_irq_tx_disable,
	.irq_tx_ready = uart_hc32_irq_tx_ready,
	.irq_tx_complete = uart_hc32_irq_tx_complete,
	.irq_rx_enable = uart_hc32_irq_rx_enable,
	.irq_rx_disable = uart_hc32_irq_rx_disable,
	.irq_rx_ready = uart_hc32_irq_rx_ready,
	.irq_is_pending = uart_hc32_irq_is_pending,
	.irq_update = uart_hc32_irq_update,
	.irq_callback_set = uart_hc32_irq_callback_set,
#endif
#ifdef CONFIG_UART_HC32_DMA
	.callback_set = uart_hc32_async_callback_set,
	.tx = uart_hc32_async_tx,
	.tx_abort = uart_hc32_async_tx_abort,
	.rx_enable = uart_hc32_async_rx_enable,
	.rx_buf_rsp = uart_hc32_async_rx_buf_rsp,
	.rx_disable = uart_hc32_async_rx_disable,
#endif
};

/* Determine USART instance number from base address */
#define HC32_USART_INSTANCE(base) \
	(((base) == 0x4001D000UL) ? 1 : \
	 ((base) == 0x4001D400UL) ? 2 : \
	 ((base) == 0x40021000UL) ? 3 : 4)

#define HC32_CLOCK_ID(bus, bit)  ((((bus) & 0xffU) << 8) | ((bit) & 0xffU))

/* ---------- Instantiation ---------- */

#define UART_HC32_INIT(n)                                                      \
	PINCTRL_DT_INST_DEFINE(n);                                             \
	static struct uart_hc32_data uart_hc32_data_##n;                      \
	static const struct uart_hc32_config uart_hc32_config_##n = {         \
		.base = DT_INST_REG_ADDR(n),                                   \
		.baud_rate = DT_INST_PROP_OR(n, current_speed, 115200),        \
		.instance = HC32_USART_INSTANCE(DT_INST_REG_ADDR(n)),         \
		.clock_id = HC32_CLOCK_ID(DT_INST_CLOCKS_CELL(n, bus),         \
					  DT_INST_CLOCKS_CELL(n, bit)),        \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),            \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                     \
	};                                                                     \
	DEVICE_DT_INST_DEFINE(n, uart_hc32_init, NULL,                         \
			      &uart_hc32_data_##n,                            \
			      &uart_hc32_config_##n, PRE_KERNEL_1,            \
			      CONFIG_SERIAL_INIT_PRIORITY,                     \
			      &uart_hc32_driver_api);

DT_INST_FOREACH_STATUS_OKAY(UART_HC32_INIT)
