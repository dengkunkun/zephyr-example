/**
 *******************************************************************************
 * @file  test_ws2812b.c
 * @brief WS2812B RGB LED test for UYUP board.
 *        Data pin PB1, chain of 2 LEDs, bit-bang via GPIO.
 *******************************************************************************
 */
#include "bsp_uyup.h"
#include "test_all.h"

/*
 * WS2812B timing (at 200 MHz HCLK):
 *   T0H = 0.4us → 80 cycles
 *   T0L = 0.85us → 170 cycles
 *   T1H = 0.8us → 160 cycles
 *   T1L = 0.45us → 90 cycles
 *   Reset > 50us → 10000 cycles
 *
 * Using bit-bang GPIO for simplicity. DMA+Timer would be more robust.
 */

/* Inline delay in CPU cycles (approximate) */
static void delay_cycles(volatile uint32_t n)
{
    while (n--) {
        __NOP();
    }
}

static void WS2812_SendBit(uint8_t bit)
{
    if (bit) {
        GPIO_SetPins(BSP_WS2812_PORT, BSP_WS2812_PIN);
        delay_cycles(30U);   /* T1H ~0.8us */
        GPIO_ResetPins(BSP_WS2812_PORT, BSP_WS2812_PIN);
        delay_cycles(10U);   /* T1L ~0.45us */
    } else {
        GPIO_SetPins(BSP_WS2812_PORT, BSP_WS2812_PIN);
        delay_cycles(10U);   /* T0H ~0.4us */
        GPIO_ResetPins(BSP_WS2812_PORT, BSP_WS2812_PIN);
        delay_cycles(30U);   /* T0L ~0.85us */
    }
}

static void WS2812_SendByte(uint8_t byte)
{
    uint8_t i;
    for (i = 0U; i < 8U; i++) {
        WS2812_SendBit(byte & 0x80U);
        byte <<= 1U;
    }
}

/* Send GRB color to one LED */
static void WS2812_SendColor(uint8_t r, uint8_t g, uint8_t b)
{
    WS2812_SendByte(g);
    WS2812_SendByte(r);
    WS2812_SendByte(b);
}

static void WS2812_Reset(void)
{
    GPIO_ResetPins(BSP_WS2812_PORT, BSP_WS2812_PIN);
    BSP_DelayUS(80UL);   /* >50us reset */
}

void test_ws2812b(void)
{
    stc_gpio_init_t stcGpio;
    uint8_t i;

    BSP_UART_Printf("[TEST] WS2812B (2 LEDs on PB1) ...\r\n");

    LL_PERIPH_WE(BSP_LL_PERIPH_SEL);

    /* PB1 as push-pull output */
    (void)GPIO_StructInit(&stcGpio);
    stcGpio.u16PinState = PIN_STAT_RST;
    stcGpio.u16PinDir   = PIN_DIR_OUT;
    stcGpio.u16PinOutputType = PIN_OUT_TYPE_CMOS;
    (void)GPIO_Init(BSP_WS2812_PORT, BSP_WS2812_PIN, &stcGpio);

    LL_PERIPH_WP(BSP_LL_PERIPH_SEL);

    /* Cycle through Red → Green → Blue → White → Off */
    static const uint8_t colors[][3] = {
        {255, 0, 0},       /* Red */
        {0, 255, 0},       /* Green */
        {0, 0, 255},       /* Blue */
        {255, 255, 255},   /* White */
        {0, 0, 0},         /* Off */
    };

    for (i = 0U; i < 5U; i++) {
        __disable_irq();
        WS2812_SendColor(colors[i][0], colors[i][1], colors[i][2]);
        WS2812_SendColor(colors[i][0], colors[i][1], colors[i][2]);
        WS2812_Reset();
        __enable_irq();

        BSP_UART_Printf("  Color: R=%u G=%u B=%u\r\n",
                         colors[i][0], colors[i][1], colors[i][2]);
        BSP_DelayMS(500UL);
    }

    BSP_UART_Printf("[PASS] WS2812B\r\n");
}
