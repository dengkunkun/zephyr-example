/**
 *******************************************************************************
 * @file  test_ws2812b.c
 * @brief WS2812B RGB LED test for UYUP board.
 *        Data pin PB1, chain of 2 LEDs.
 *        Uses DWT cycle counter for precise sub-µs timing and direct GPIO
 *        register writes for minimal latency.
 *******************************************************************************
 */
#include "bsp_uyup.h"
#include "test_all.h"

/*
 * WS2812B timing (800 kHz, 1.25 µs per bit):
 *   T0H = 0.40 µs  (tolerance ±150 ns)
 *   T0L = 0.85 µs
 *   T1H = 0.80 µs
 *   T1L = 0.45 µs
 *   Reset >= 50 µs
 *
 * At HCLK = 200 MHz (5 ns/cycle):
 *   T0H = 80 cycles,  T0L = 170 cycles
 *   T1H = 160 cycles, T1L = 90 cycles
 */

#define WS_T0H_CYC   (80U)
#define WS_T0L_CYC   (170U)
#define WS_T1H_CYC   (160U)
#define WS_T1L_CYC   (90U)

/* Direct register writes: PB1 = bit 1 of port B */
#define WS_PIN_MASK   (1U << 1)
#define WS_HIGH()     (CM_GPIO->POSRB = WS_PIN_MASK)
#define WS_LOW()      (CM_GPIO->PORRB = WS_PIN_MASK)

/* DWT cycle counter wait — cycle-accurate (±1 cycle = 5 ns) */
static inline void ws_wait(uint32_t cyc)
{
    uint32_t start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < cyc) {
        /* spin */
    }
}

static void WS2812_SendByte(uint8_t byte)
{
    uint8_t i;
    for (i = 0U; i < 8U; i++) {
        if (byte & 0x80U) {
            WS_HIGH();
            ws_wait(WS_T1H_CYC);
            WS_LOW();
            ws_wait(WS_T1L_CYC);
        } else {
            WS_HIGH();
            ws_wait(WS_T0H_CYC);
            WS_LOW();
            ws_wait(WS_T0L_CYC);
        }
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
    WS_LOW();
    DDL_DelayUS(80UL);  /* >50 µs reset */
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

    /* Enable DWT cycle counter */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* Brightness capped at 32 to avoid blinding */
    static const uint8_t colors[][3] = {
        { 32,   0,   0},   /* Red */
        {  0,  32,   0},   /* Green */
        {  0,   0,  32},   /* Blue */
        { 32,  32,   0},   /* Yellow */
        {  0,  32,  32},   /* Cyan */
        { 32,   0,  32},   /* Magenta */
        { 32,  32,  32},   /* White (dim) */
        {  0,   0,   0},   /* Off */
    };

    for (i = 0U; i < 8U; i++) {
        __disable_irq();
        WS2812_SendColor(colors[i][0], colors[i][1], colors[i][2]);
        WS2812_SendColor(colors[i][0], colors[i][1], colors[i][2]);
        WS2812_Reset();
        __enable_irq();

        BSP_UART_Printf("  Color: R=%u G=%u B=%u\r\n",
                         colors[i][0], colors[i][1], colors[i][2]);
        BSP_WDT_Feed();
        BSP_DelayMS(800UL);
    }

    BSP_UART_Printf("[PASS] WS2812B\r\n");
}
