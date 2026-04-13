/**
 *******************************************************************************
 * @file  test_ws2812b.c
 * @brief WS2812B RGB LED test for UYUP board.
 *        Data pin PB1, chain of 2 LEDs.
 *
 *        Uses DWT cycle-counter bit-bang at -O2 with ISR disabled.
 *        Includes PB1 connectivity test and multi-timing sweep.
 *
 *        HCLK = 200 MHz → 5 ns / cycle
 *******************************************************************************
 */
#include "bsp_uyup.h"
#include "test_all.h"

#define WS_LED_COUNT    (BSP_WS2812_LED_COUNT)   /* 2 */
#define WS_PIN_MASK     (BSP_WS2812_PIN)          /* GPIO_PIN_01 = 0x0002 */

/* Pointers kept in registers for tight inner loop */
static volatile uint32_t * const pCYCCNT = &DWT->CYCCNT;
static volatile uint16_t * const pPOSRB  = &CM_GPIO->POSRB;
static volatile uint16_t * const pPORRB  = &CM_GPIO->PORRB;

/**
 * @brief  Core bit-bang send. Compiled at -O2 for tight loop.
 *         Pointers are pre-resolved to avoid literal-pool reloads.
 */
__attribute__((optimize("O2")))
static void WS_SendByte_O2(uint8_t val, uint32_t t0h, uint32_t t1h, uint32_t tbit)
{
    volatile uint32_t *cyccnt = pCYCCNT;
    volatile uint16_t *posrb  = pPOSRB;
    volatile uint16_t *porrb  = pPORRB;
    const uint16_t mask = WS_PIN_MASK;

    for (int8_t bit = 7; bit >= 0; bit--) {
        const uint32_t th = (val & (1U << bit)) ? t1h : t0h;
        const uint32_t t0 = *cyccnt;

        *posrb = mask;                                          /* HIGH */
        while ((*cyccnt - t0) < th)  ;                          /* spin T_HIGH */

        *porrb = mask;                                          /* LOW */
        while ((*cyccnt - t0) < tbit) ;                         /* spin remainder */
    }
}

/**
 * @brief  Send full frame with given timing.
 */
__attribute__((optimize("O2")))
static void WS_SendFrame_Ex(const uint8_t rgb[][3],
                             uint32_t t0h, uint32_t t1h, uint32_t tbit)
{
    __disable_irq();
    __DSB();
    __ISB();

    for (uint8_t led = 0U; led < WS_LED_COUNT; led++) {
        WS_SendByte_O2(rgb[led][1], t0h, t1h, tbit);   /* G */
        WS_SendByte_O2(rgb[led][0], t0h, t1h, tbit);   /* R */
        WS_SendByte_O2(rgb[led][2], t0h, t1h, tbit);   /* B */
    }
    CM_GPIO->PORRB = WS_PIN_MASK;                       /* LOW */
    __DSB();

    __enable_irq();
    DDL_DelayUS(300U);                                   /* reset pulse */
}

/* Convenience wrapper with default timing */
static void WS_SendFrame(const uint8_t rgb[][3],
                          uint32_t t0h, uint32_t t1h, uint32_t tbit)
{
    WS_SendFrame_Ex(rgb, t0h, t1h, tbit);
}

void test_ws2812b(void)
{
    BSP_UART_Printf("[TEST] WS2812B (2 LEDs on PB1) ...\r\n");

    LL_PERIPH_WE(BSP_LL_PERIPH_SEL);

    /* Enable DWT cycle counter */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* Reset PB1 to GPIO mode (in case prior test set PFSR) */
    GPIO_SetFunc(BSP_WS2812_PORT, BSP_WS2812_PIN, GPIO_FUNC_0);

    /* Configure PB1 as push-pull output, HIGH drive, initial LOW */
    stc_gpio_init_t stcGpio;
    (void)GPIO_StructInit(&stcGpio);
    stcGpio.u16PinState      = PIN_STAT_RST;
    stcGpio.u16PinDir        = PIN_DIR_OUT;
    stcGpio.u16PinDrv        = PIN_HIGH_DRV;
    stcGpio.u16PinOutputType = PIN_OUT_TYPE_CMOS;
    (void)GPIO_Init(BSP_WS2812_PORT, BSP_WS2812_PIN, &stcGpio);

    LL_PERIPH_WP(BSP_LL_PERIPH_SEL);

    /* ----- DWT sanity check ----- */
    uint32_t c0 = DWT->CYCCNT;
    DDL_DelayUS(10U);
    uint32_t c1 = DWT->CYCCNT;
    BSP_UART_Printf("  DWT: %lu cyc/10us\r\n", (unsigned long)(c1 - c0));

    /* ----- Step 1: PB1 connectivity test ----- */
    BSP_UART_Printf("  PB1 HIGH 2s...\r\n");
    CM_GPIO->POSRB = WS_PIN_MASK;                       /* PB1 HIGH */
    BSP_WDT_Feed();
    BSP_DelayMS(2000UL);
    uint16_t pidr_hi = CM_GPIO->PIDRB;
    BSP_UART_Printf("  PB1 LOW 2s...\r\n");
    CM_GPIO->PORRB = WS_PIN_MASK;                       /* PB1 LOW */
    BSP_WDT_Feed();
    BSP_DelayMS(2000UL);
    uint16_t pidr_lo = CM_GPIO->PIDRB;
    BSP_UART_Printf("  PB1 HI:PIDR=0x%04X LO:PIDR=0x%04X POER=0x%04X PFSR=0x%04X\r\n",
                     (unsigned)pidr_hi, (unsigned)pidr_lo,
                     (unsigned)CM_GPIO->POERB,
                     (unsigned)CM_GPIO->PFSRB1);

    /* ----- Step 2: timing sweep ----- */
    /* Try 5 timing configs (cycles at 200 MHz = 5ns/cycle):
     * Config A: WS2812B-V5 aggressive (T0H=0.20us T1H=0.58us period=1.00us)
     * Config B: WS2812B moderate      (T0H=0.30us T1H=0.70us period=1.25us)
     * Config C: WS2812B standard      (T0H=0.40us T1H=0.80us period=1.25us)
     * Config D: Relaxed wide margin   (T0H=0.25us T1H=0.90us period=1.50us)
     * Config E: WS2811 400kHz         (T0H=0.50us T1H=1.20us period=2.50us)
     */
    static const struct {
        uint32_t t0h, t1h, tbit;
        const char *label;
    } cfgs[] = {
        {  40, 116, 200, "A:V5-0.20us"  },
        {  60, 140, 250, "B:mod-0.30us"  },
        {  80, 160, 250, "C:std-0.40us"  },
        {  50, 180, 300, "D:wide-0.25us" },
        { 100, 240, 500, "E:WS2811"      },
    };

    /* For each config, send bright RED then bright GREEN */
    const uint8_t red[2][3]   = {{255,0,0},{255,0,0}};
    const uint8_t green[2][3] = {{0,255,0},{0,255,0}};
    const uint8_t black[2][3] = {{0,0,0},{0,0,0}};

    for (uint8_t ci = 0U; ci < 5U; ci++) {
        BSP_UART_Printf("  Timing %s\r\n", cfgs[ci].label);

        /* Black first */
        WS_SendFrame(black, cfgs[ci].t0h, cfgs[ci].t1h, cfgs[ci].tbit);
        BSP_WDT_Feed();
        BSP_DelayMS(300UL);

        /* RED */
        WS_SendFrame(red, cfgs[ci].t0h, cfgs[ci].t1h, cfgs[ci].tbit);
        BSP_UART_Printf("    -> RED\r\n");
        BSP_WDT_Feed();
        BSP_DelayMS(1200UL);

        /* GREEN */
        WS_SendFrame(green, cfgs[ci].t0h, cfgs[ci].t1h, cfgs[ci].tbit);
        BSP_UART_Printf("    -> GREEN\r\n");
        BSP_WDT_Feed();
        BSP_DelayMS(1200UL);

        /* OFF */
        WS_SendFrame(black, cfgs[ci].t0h, cfgs[ci].t1h, cfgs[ci].tbit);
        BSP_WDT_Feed();
        BSP_DelayMS(300UL);
    }

    /* Leave pin LOW */
    CM_GPIO->PORRB = WS_PIN_MASK;

    /* Check if PB1 drove correctly during connectivity test */
    if ((pidr_hi & WS_PIN_MASK) && !(pidr_lo & WS_PIN_MASK)) {
        BSP_UART_Printf("[PASS] WS2812B (GPIO OK, check LED colors visually)\r\n");
    } else {
        BSP_UART_Printf("[FAIL] WS2812B PB1 readback mismatch\r\n");
    }
}
