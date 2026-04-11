/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * UART (polling) driver for HC32F460 USART.
 *
 * Uses direct register access — no DDL LL-driver functions required.
 * Board-specific pin muxing (PA2/PA3/PA9) is done in init.
 */

#define DT_DRV_COMPAT hdsc_hc32_usart

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <soc.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(uart_hc32, CONFIG_UART_LOG_LEVEL);

/* ---------- GPIO pin-function register helpers ---------- */
/* PFSR register: offset 0x0402 + port*0x40 + pin*0x04 within CM_GPIO */
#define GPIO_BASE          0x40053800UL
#define GPIO_PFSR(port, pin) \
	(GPIO_BASE + 0x0402U + (uint32_t)(port) * 0x40U + (uint32_t)(pin) * 0x04U)

/* PCR register: offset 0x0400 + port*0x40 + pin*0x04 within CM_GPIO */
#define GPIO_PCR(port, pin) \
	(GPIO_BASE + 0x0400U + (uint32_t)(port) * 0x40U + (uint32_t)(pin) * 0x04U)

/* Port indices */
#define PORT_A 0
#define PORT_B 1
#define PORT_C 2
#define PORT_D 3
#define PORT_E 4

static inline void gpio_set_func(uint8_t port, uint8_t pin, uint8_t func)
{
	uint32_t pfsr_addr = GPIO_PFSR(port, pin);
	uint16_t val = sys_read16(pfsr_addr);
	/* Set FSEL bits [5:0] only; do NOT set BFE (bare-metal DDL doesn't) */
	val = (val & ~0x3FU) | (func & 0x3FU);
	sys_write16(val, pfsr_addr);
}

/* ---------- USART register offsets ---------- */
/* CM_USART_TypeDef: SR(0x00), TDR(0x04), RDR(0x06), BRR(0x08),
 *                   CR1(0x0C), CR2(0x10), CR3(0x14), PR(0x18) */
#define USART_OFF_SR   0x00U
#define USART_OFF_TDR  0x04U
#define USART_OFF_RDR  0x06U
#define USART_OFF_BRR  0x08U
#define USART_OFF_CR1  0x0CU
#define USART_OFF_CR2  0x10U
#define USART_OFF_CR3  0x14U
#define USART_OFF_PR   0x18U

struct uart_hc32_config {
	uint32_t base;
	uint32_t baud_rate;
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

static void uart_hc32_poll_out(const struct device *dev, unsigned char c)
{
	const struct uart_hc32_config *cfg = dev->config;

	/* Wait until TX buffer empty */
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

	if (usart_read32(cfg, USART_OFF_SR) & USART_SR_ORE) {
		/* Clear overrun by re-toggling RE */
		uint32_t cr1 = usart_read32(cfg, USART_OFF_CR1);
		usart_write32(cfg, USART_OFF_CR1, cr1 & ~USART_CR1_RE);
		usart_write32(cfg, USART_OFF_CR1, cr1 | USART_CR1_RE);
		return UART_ERROR_OVERRUN;
	}
	return 0;
}

static DEVICE_API(uart, uart_hc32_driver_api) = {
	.poll_in = uart_hc32_poll_in,
	.poll_out = uart_hc32_poll_out,
	.err_check = uart_hc32_err_check,
};

/*
 * Compute BRR value for USART in UART mode.
 *
 * HC32F460 USART baud rate formula (UART mode):
 *   Without fraction: B = C / (8 * (2-OVER8) * (DIV_Integer + 1))
 *   With fraction:    B = C * (128 + DIV_Frac) / (8 * (2-OVER8) * (DIV_Integer + 1) * 256)
 *
 * BRR register: bits[14:8] = DIV_Integer, bits[6:0] = DIV_Fraction
 * CR1 bit FBME (bit 29) must be set when using fraction mode.
 *
 * PR register bits[1:0]: 0=CLK/1, 1=CLK/4, 2=CLK/16, 3=CLK/64
 */
static int uart_hc32_set_baud(const struct uart_hc32_config *cfg)
{
	const uint32_t pclk1 = 8000000U;  /* MRC 8 MHz default */
	/* PR divisor: index → actual divisor */
	static const uint32_t pr_div[] = {1, 4, 16, 64};
	uint32_t pr;

	for (pr = 0U; pr < 4U; pr++) {
		uint32_t usart_clk = pclk1 / pr_div[pr];
		/* With OVER8=1: B = C / (8 * (DIV_Integer+1)) without fraction
		 * DIV_Integer = C / (8 * B) - 1
		 */
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

		/* Calculate fractional part:
		 * DIV_Frac = round(256 * 8 * (div_int+1) * B / C) - 128
		 */
		uint64_t num = (uint64_t)256U * 8U * (div_int + 1U) * cfg->baud_rate;
		uint32_t frac_raw = (uint32_t)((num + usart_clk / 2U) / usart_clk);
		int32_t div_frac = (int32_t)frac_raw - 128;

		uint32_t brr;
		bool use_frac = false;

		if (div_frac >= 0 && div_frac <= 0x7F) {
			/* Use fraction mode */
			brr = ((div_int & 0xFFU) << 8) | (div_frac & 0x7FU);
			use_frac = true;

			/* Verify error: actual_baud = C * (128+frac) / (8*(int+1)*256) */
			uint32_t actual = (uint32_t)((uint64_t)usart_clk *
				(128U + div_frac) / (2048U * (div_int + 1U)));
			int32_t err_ppm = (int32_t)((int64_t)(actual - cfg->baud_rate) *
				10000 / (int64_t)cfg->baud_rate);
			if (err_ppm > 250 || err_ppm < -250) {
				continue;
			}
		} else {
			/* Round integer and use integer-only mode */
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

		/* Apply prescaler and BRR */
		usart_write32(cfg, USART_OFF_PR, pr);
		usart_write32(cfg, USART_OFF_BRR, brr);
		if (use_frac) {
			uint32_t cr1 = usart_read32(cfg, USART_OFF_CR1);
			usart_write32(cfg, USART_OFF_CR1, cr1 | USART_CR1_FBME);
		}
		return 0;
	}
	return -EINVAL;
}

static int uart_hc32_init(const struct device *dev)
{
	const struct uart_hc32_config *cfg = dev->config;

	/* Ensure GPIO write-protect is unlocked (PWPR @ offset 0x3FC) */
	sys_write16(0xA501U, 0x40053BFCUL);

	/* 1) Enable USART2 peripheral clock (clear bit in FCG1 = enable) */
	CM_PWC->FCG1 &= ~PWC_FCG1_USART2;

	/* 2) Configure GPIO pin alternate functions:
	 *    PA9 = FUNC 36 (USART TX idle-high for DAPLink VCP)
	 *    PA2 = FUNC 36 (USART2_TX)
	 *    PA3 = FUNC 37 (USART2_RX)
	 */
	gpio_set_func(PORT_A, 9, 36);
	gpio_set_func(PORT_A, 2, 36);
	gpio_set_func(PORT_A, 3, 37);

	/* Set high drive strength on TX pins (PCR DRV bit 8) — matches DDL */
	uint16_t pcr = sys_read16(GPIO_PCR(PORT_A, 2));
	sys_write16(pcr | (1U << 8), GPIO_PCR(PORT_A, 2));
	pcr = sys_read16(GPIO_PCR(PORT_A, 9));
	sys_write16(pcr | (1U << 8), GPIO_PCR(PORT_A, 9));

	/* 3) Configure USART: 8N1, 8× oversampling, falling-edge start bit detect */
	usart_write32(cfg, USART_OFF_CR1, USART_CR1_OVER8 | USART_CR1_SBS);
	usart_write32(cfg, USART_OFF_CR2, 0U);
	usart_write32(cfg, USART_OFF_CR3, 0U);

	/* 4) Set baud rate */
	int ret = uart_hc32_set_baud(cfg);
	if (ret != 0) {
		LOG_ERR("Failed to set baud rate %u", cfg->baud_rate);
		return ret;
	}

	/* 5) Enable TX and RX */
	uint32_t cr1 = usart_read32(cfg, USART_OFF_CR1);
	usart_write32(cfg, USART_OFF_CR1, cr1 | USART_CR1_TE | USART_CR1_RE);

	return 0;
}

#define UART_HC32_INIT(n)                                                      \
	static const struct uart_hc32_config uart_hc32_config_##n = {          \
		.base = DT_INST_REG_ADDR(n),                                   \
		.baud_rate = DT_INST_PROP_OR(n, current_speed, 115200),        \
	};                                                                     \
	DEVICE_DT_INST_DEFINE(n, uart_hc32_init, NULL, NULL,                   \
			      &uart_hc32_config_##n, PRE_KERNEL_1,            \
			      CONFIG_SERIAL_INIT_PRIORITY,                     \
			      &uart_hc32_driver_api);

DT_INST_FOREACH_STATUS_OKAY(UART_HC32_INIT)
