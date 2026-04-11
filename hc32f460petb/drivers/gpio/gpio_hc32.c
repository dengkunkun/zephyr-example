/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * GPIO driver for HC32F460.
 *
 * HC32F460 GPIO uses a single register block (CM_GPIO @ 0x40053800).
 * Each port (A-H) has:
 *   - PIDR: input data register  (read port pins)
 *   - PODR: output data register (read/write output latch)
 *   - POER: output enable
 *   - POSR: output set register  (write 1 = set)
 *   - PORR: output reset register (write 1 = clear)
 *   - POTR: output toggle register (write 1 = toggle)
 *   - PCRxy: per-pin configuration (direction, pull-up, drive, etc.)
 *
 * External interrupts use the INTC controller:
 *   - 16 EXTINT channels (0-15), one per pin number (shared across ports)
 *   - EIRQCR[n]: trigger config (falling/rising/both/low-level)
 *   - EIFR: interrupt flag register
 *   - EIFCR: flag clear register
 *   - SEL[n]: maps interrupt source to NVIC IRQ line
 */

#define DT_DRV_COMPAT hdsc_hc32_gpio

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <soc.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gpio_hc32, CONFIG_GPIO_LOG_LEVEL);

/*
 * HC32F460 GPIO register offsets per port (port stride = 0x10).
 * Each port block at offset port*0x10 contains:
 *   PIDR(+0x00), reserved(+0x02), PODR(+0x04), POER(+0x06),
 *   POSR(+0x08), PORR(+0x0A), POTR(+0x0C), reserved(+0x0E)
 */
#define GPIO_PIDR_OFFSET(port)  (0x0000 + (port) * 0x10) /* Port Input Data */
#define GPIO_PODR_OFFSET(port)  (0x0004 + (port) * 0x10) /* Port Output Data */
#define GPIO_POER_OFFSET(port)  (0x0006 + (port) * 0x10) /* Port Output Enable */
#define GPIO_POSR_OFFSET(port)  (0x0008 + (port) * 0x10) /* Port Output Set */
#define GPIO_PORR_OFFSET(port)  (0x000A + (port) * 0x10) /* Port Output Reset */
#define GPIO_POTR_OFFSET(port)  (0x000C + (port) * 0x10) /* Port Output Toggle */

/*
 * Per-pin config register PCRxy:
 * Base offset: 0x0400 + port*0x40 + pin*0x04
 * Bits: [0] POUT (output value), [1] POUTE (output enable),
 *       [4] NOD (open-drain), [8] DRV (drive), [9] PUU (pull-up)
 *       [14] INTE (EXTINT enable for this pin)
 */
#define GPIO_PCR_OFFSET(port, pin) (0x0400 + (port) * 0x40 + (pin) * 0x04)

#define PCR_POUT  BIT(0)   /* Output data */
#define PCR_POUTE BIT(1)   /* Output enable */
#define PCR_NOD   BIT(2)   /* Open-drain (N-channel OD) */
#define PCR_DRV0  BIT(4)   /* Drive strength bit 0 */
#define PCR_DRV1  BIT(5)   /* Drive strength bit 1 */
#define PCR_PUU   BIT(6)   /* Pull-up enable */
#define PCR_INVE  BIT(9)   /* Input inversion */
#define PCR_INTE  BIT(12)  /* EXTINT enable for this pin */

/*
 * Per-pin function select PFSRxy:
 * Base offset: 0x0402 + port*0x40 + pin*0x04
 * (PCR and PFSR are interleaved: PCR at +0x400, PFSR at +0x402 per pin)
 * Bits [5:0]: FSEL (function select, 0 = GPIO)
 */
#define GPIO_PFSR_OFFSET(port, pin) (0x0402 + (port) * 0x40 + (pin) * 0x04)

/* INTC (Interrupt Controller) registers */
#define INTC_BASE       0x40051000UL
#define INTC_EIRQCR(n)  (INTC_BASE + 0x010U + (n) * 4U)  /* EXTINT config */
#define INTC_EIFR       (INTC_BASE + 0x054U)               /* EXTINT flag */
#define INTC_EIFCR      (INTC_BASE + 0x058U)               /* EXTINT flag clear */
#define INTC_SEL(n)     (INTC_BASE + 0x05CU + (n) * 4U)   /* IRQ source select */
#define INTC_IER        (INTC_BASE + 0x2A4U)               /* IRQ enable */

/* EIRQCR trigger bits */
#define EIRQCR_TRIG_FALLING  0x00U
#define EIRQCR_TRIG_RISING   0x01U
#define EIRQCR_TRIG_BOTH     0x02U
#define EIRQCR_TRIG_LOW      0x03U
#define EIRQCR_EFEN          BIT(7)  /* digital filter enable */
#define EIRQCR_FCLK_DIV8     0x10U   /* filter clock PCLK3/8 */

/* We use NVIC IRQs 0-15 for EXTINT channels 0-15 */
#define EXTINT_NVIC_IRQ_BASE  0

struct gpio_hc32_config {
	struct gpio_driver_config common;
	uint32_t base;     /* GPIO register block base (CM_GPIO) */
	uint8_t port_idx;  /* 0=A, 1=B, 2=C, 3=D, 4=E, ... */
};

struct gpio_hc32_data {
	struct gpio_driver_data common;
	sys_slist_t callbacks;
	uint16_t irq_pins; /* bitmask of pins with interrupts enabled */
};

/* Track which device owns each EXTINT channel (one per pin number, shared) */
static const struct device *extint_owner[16];

static inline uint16_t gpio_hc32_read16(const struct gpio_hc32_config *cfg,
					 uint32_t offset)
{
	return sys_read16(cfg->base + offset);
}

static inline void gpio_hc32_write16(const struct gpio_hc32_config *cfg,
				     uint32_t offset, uint16_t val)
{
	sys_write16(val, cfg->base + offset);
}

static inline void gpio_hc32_write_pcr(const struct gpio_hc32_config *cfg,
					uint8_t pin, uint16_t val)
{
	sys_write16(val, cfg->base + GPIO_PCR_OFFSET(cfg->port_idx, pin));
}

static inline uint16_t gpio_hc32_read_pcr(const struct gpio_hc32_config *cfg,
					   uint8_t pin)
{
	return sys_read16(cfg->base + GPIO_PCR_OFFSET(cfg->port_idx, pin));
}

static int gpio_hc32_pin_configure(const struct device *dev,
				   gpio_pin_t pin, gpio_flags_t flags)
{
	const struct gpio_hc32_config *cfg = dev->config;
	uint16_t pcr;

	if (pin >= 16) {
		return -EINVAL;
	}

	/* Set pin function to GPIO (FSEL=0) */
	sys_write16(0, cfg->base + GPIO_PFSR_OFFSET(cfg->port_idx, pin));

	pcr = 0;

	if (flags & GPIO_OUTPUT) {
		pcr |= PCR_POUTE;

		if (flags & GPIO_OUTPUT_INIT_HIGH) {
			pcr |= PCR_POUT;
		}

		if (flags & GPIO_OPEN_DRAIN) {
			pcr |= PCR_NOD;
		}
	}

	if (flags & GPIO_PULL_UP) {
		pcr |= PCR_PUU;
	}

	gpio_hc32_write_pcr(cfg, pin, pcr);

	return 0;
}

static int gpio_hc32_port_get_raw(const struct device *dev,
				  gpio_port_value_t *value)
{
	const struct gpio_hc32_config *cfg = dev->config;

	*value = gpio_hc32_read16(cfg, GPIO_PIDR_OFFSET(cfg->port_idx));
	return 0;
}

static int gpio_hc32_port_set_masked_raw(const struct device *dev,
					 gpio_port_pins_t mask,
					 gpio_port_value_t value)
{
	const struct gpio_hc32_config *cfg = dev->config;
	uint16_t cur = gpio_hc32_read16(cfg, GPIO_PODR_OFFSET(cfg->port_idx));

	cur = (cur & ~(uint16_t)mask) | ((uint16_t)value & (uint16_t)mask);
	gpio_hc32_write16(cfg, GPIO_PODR_OFFSET(cfg->port_idx), cur);
	return 0;
}

static int gpio_hc32_port_set_bits_raw(const struct device *dev,
				       gpio_port_pins_t pins)
{
	const struct gpio_hc32_config *cfg = dev->config;

	gpio_hc32_write16(cfg, GPIO_POSR_OFFSET(cfg->port_idx), (uint16_t)pins);
	return 0;
}

static int gpio_hc32_port_clear_bits_raw(const struct device *dev,
					 gpio_port_pins_t pins)
{
	const struct gpio_hc32_config *cfg = dev->config;

	gpio_hc32_write16(cfg, GPIO_PORR_OFFSET(cfg->port_idx), (uint16_t)pins);
	return 0;
}

static int gpio_hc32_port_toggle_bits(const struct device *dev,
				      gpio_port_pins_t pins)
{
	const struct gpio_hc32_config *cfg = dev->config;

	gpio_hc32_write16(cfg, GPIO_POTR_OFFSET(cfg->port_idx), (uint16_t)pins);
	return 0;
}

static void gpio_hc32_extint_isr(const void *arg)
{
	uint8_t ch = (uint8_t)(uintptr_t)arg;
	const struct device *dev = extint_owner[ch];

	/* Clear the EXTINT flag */
	sys_write32(BIT(ch), INTC_EIFCR);

	if (dev != NULL) {
		struct gpio_hc32_data *data = dev->data;

		gpio_fire_callbacks(&data->callbacks, dev, BIT(ch));
	}
}

static int gpio_hc32_pin_interrupt_configure(const struct device *dev,
					     gpio_pin_t pin,
					     enum gpio_int_mode mode,
					     enum gpio_int_trig trig)
{
	const struct gpio_hc32_config *cfg = dev->config;
	struct gpio_hc32_data *data = dev->data;
	unsigned int irq_num = EXTINT_NVIC_IRQ_BASE + pin;
	uint16_t pcr;

	if (pin >= 16) {
		return -EINVAL;
	}

	if (mode == GPIO_INT_MODE_DISABLED) {
		/* Disable interrupt: clear INTE bit in PCR */
		pcr = gpio_hc32_read_pcr(cfg, pin);
		pcr &= ~PCR_INTE;
		gpio_hc32_write_pcr(cfg, pin, pcr);

		data->irq_pins &= ~BIT(pin);
		if (extint_owner[pin] == dev) {
			extint_owner[pin] = NULL;
			irq_disable(irq_num);
		}
		return 0;
	}

	if (mode == GPIO_INT_MODE_LEVEL) {
		/* HC32F460 only supports low-level, not high-level */
		if (trig != GPIO_INT_TRIG_LOW) {
			return -ENOTSUP;
		}
	}

	/* Check if another port already owns this EXTINT channel */
	if (extint_owner[pin] != NULL && extint_owner[pin] != dev) {
		return -EBUSY;
	}

	/* Configure EIRQCR: trigger type + digital filter */
	uint32_t eirqcr = EIRQCR_EFEN | EIRQCR_FCLK_DIV8;

	if (mode == GPIO_INT_MODE_LEVEL) {
		eirqcr |= EIRQCR_TRIG_LOW;
	} else {
		switch (trig) {
		case GPIO_INT_TRIG_LOW: /* falling edge */
			eirqcr |= EIRQCR_TRIG_FALLING;
			break;
		case GPIO_INT_TRIG_HIGH: /* rising edge */
			eirqcr |= EIRQCR_TRIG_RISING;
			break;
		case GPIO_INT_TRIG_BOTH:
			eirqcr |= EIRQCR_TRIG_BOTH;
			break;
		default:
			return -EINVAL;
		}
	}

	sys_write32(eirqcr, INTC_EIRQCR(pin));

	/* Clear any pending flag */
	sys_write32(BIT(pin), INTC_EIFCR);

	/* Map INT_SRC_PORT_EIRQ<pin> to NVIC IRQ <pin> via INTC SEL register */
	sys_write32(pin, INTC_SEL(irq_num)); /* INT_SRC_PORT_EIRQn = n */

	/* Set INTE bit in PCR to route this pin to EXTINT */
	pcr = gpio_hc32_read_pcr(cfg, pin);
	pcr |= PCR_INTE;
	gpio_hc32_write_pcr(cfg, pin, pcr);

	extint_owner[pin] = dev;
	data->irq_pins |= BIT(pin);

	/* Connect and enable NVIC IRQ */
	irq_connect_dynamic(irq_num, 0, gpio_hc32_extint_isr,
			    (const void *)(uintptr_t)pin, 0);
	irq_enable(irq_num);

	return 0;
}

static int gpio_hc32_manage_callback(const struct device *dev,
				     struct gpio_callback *callback, bool set)
{
	struct gpio_hc32_data *data = dev->data;

	return gpio_manage_callback(&data->callbacks, callback, set);
}

static DEVICE_API(gpio, gpio_hc32_driver_api) = {
	.pin_configure = gpio_hc32_pin_configure,
	.port_get_raw = gpio_hc32_port_get_raw,
	.port_set_masked_raw = gpio_hc32_port_set_masked_raw,
	.port_set_bits_raw = gpio_hc32_port_set_bits_raw,
	.port_clear_bits_raw = gpio_hc32_port_clear_bits_raw,
	.port_toggle_bits = gpio_hc32_port_toggle_bits,
	.pin_interrupt_configure = gpio_hc32_pin_interrupt_configure,
	.manage_callback = gpio_hc32_manage_callback,
};

static int gpio_hc32_init(const struct device *dev)
{
	/* Ensure GPIO write-protect is unlocked (PWPR @ offset 0x3FC) */
	sys_write16(0xA501U, 0x40053BFCUL);
	return 0;
}

#define GPIO_HC32_INIT(n)                                                      \
	static struct gpio_hc32_data gpio_hc32_data_##n;                       \
	static const struct gpio_hc32_config gpio_hc32_config_##n = {          \
		.common = { .port_pin_mask =                                   \
				GPIO_PORT_PIN_MASK_FROM_DT_INST(n) },          \
		.base = DT_INST_REG_ADDR(n),                                   \
		.port_idx = DT_INST_PROP(n, port_index),                       \
	};                                                                     \
	DEVICE_DT_INST_DEFINE(n, gpio_hc32_init, NULL, &gpio_hc32_data_##n,   \
			      &gpio_hc32_config_##n, PRE_KERNEL_1,            \
			      CONFIG_GPIO_INIT_PRIORITY, &gpio_hc32_driver_api);

DT_INST_FOREACH_STATUS_OKAY(GPIO_HC32_INIT)
