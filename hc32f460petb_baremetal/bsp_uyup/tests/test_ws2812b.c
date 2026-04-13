/**
 *******************************************************************************
 * @file  test_ws2812b.c
 * @brief WS2812B RGB LED test for UYUP board.
 *        Data pin PB1 (TMRA3_CH4), chain of 2 LEDs.
 *        Uses hardware PWM (TMRA3) + DMA for jitter-free WS2812B protocol.
 *
 *        PCLK1 = 100 MHz → TMRA3 period = 125 counts = 800 kHz
 *        Bit-0: compare = 35 (HIGH 0.35 µs, LOW 0.90 µs)
 *        Bit-1: compare = 85 (HIGH 0.85 µs, LOW 0.40 µs)
 *        DMA transfers compare values on each timer overflow.
 *******************************************************************************
 */
#include "bsp_uyup.h"
#include "test_all.h"

/* PWM timing at PCLK1 = 100 MHz (10 ns/count) */
#define WS_PWM_PERIOD       (125U - 1U)    /* 800 kHz */
#define WS_CMP_BIT0         (35U)          /* 0.35 µs HIGH */
#define WS_CMP_BIT1         (85U)          /* 0.85 µs HIGH */

#define WS_LED_COUNT        (BSP_WS2812_LED_COUNT)
#define WS_BIT_COUNT        (WS_LED_COUNT * 24U)
/* Buffer: data bits + trailing zeros for reset (>50 µs = 40 periods) */
#define WS_RESET_COUNT      (50U)
#define WS_BUF_SIZE         (WS_BIT_COUNT + WS_RESET_COUNT)

static uint16_t ws_dma_buf[WS_BUF_SIZE];

/**
 * @brief  Fill DMA buffer with WS2812B compare values.
 * @param  rgb  Array of [R, G, B] triplets for each LED.
 */
static void WS_FillBuffer(const uint8_t rgb[][3])
{
    uint16_t idx = 0U;
    for (uint16_t led = 0U; led < WS_LED_COUNT; led++) {
        /* WS2812B expects GRB byte order */
        const uint8_t grb[3] = { rgb[led][1], rgb[led][0], rgb[led][2] };
        for (uint8_t b = 0U; b < 3U; b++) {
            uint8_t val = grb[b];
            for (uint8_t bit = 0U; bit < 8U; bit++) {
                ws_dma_buf[idx++] = (val & 0x80U) ? WS_CMP_BIT1 : WS_CMP_BIT0;
                val <<= 1U;
            }
        }
    }
    /* Trailing zeros: keep output LOW for reset period */
    for (uint16_t i = idx; i < WS_BUF_SIZE; i++) {
        ws_dma_buf[i] = 0U;
    }
}

/**
 * @brief  Send the DMA buffer to WS2812B via TMRA3_CH4 PWM.
 */
static void WS_Send(void)
{
    stc_dma_init_t stcDma;

    /* Set first compare value before starting */
    TMRA_SetCompareValue(CM_TMRA_3, TMRA_CH4, ws_dma_buf[0]);

    /* DMA1 CH2: transfer compare values on each timer overflow */
    (void)DMA_StructInit(&stcDma);
    stcDma.u32SrcAddr    = (uint32_t)&ws_dma_buf[1];
    stcDma.u32DestAddr   = (uint32_t)&CM_TMRA_3->CMPAR4;
    stcDma.u32DataWidth  = DMA_DATAWIDTH_16BIT;
    stcDma.u32BlockSize  = 1U;
    stcDma.u32TransCount = WS_BUF_SIZE - 1U;
    stcDma.u32SrcAddrInc  = DMA_SRC_ADDR_INC;
    stcDma.u32DestAddrInc = DMA_DEST_ADDR_FIX;
    (void)DMA_Init(CM_DMA1, DMA_CH2, &stcDma);

    /* Trigger DMA on TMRA3 overflow */
    AOS_SetTriggerEventSrc(AOS_DMA1_2, EVT_SRC_TMRA_3_OVF);

    /* Clear any stale flags, then enable */
    DMA_ClearTransCompleteStatus(CM_DMA1, DMA_FLAG_TC_CH2 | DMA_FLAG_BTC_CH2);
    TMRA_ClearStatus(CM_TMRA_3, TMRA_FLAG_OVF);
    DMA_ChCmd(CM_DMA1, DMA_CH2, ENABLE);
    DMA_Cmd(CM_DMA1, ENABLE);

    /* Start PWM output */
    TMRA_Start(CM_TMRA_3);

    /* Wait for DMA transfer complete */
    uint32_t timeout = 200000UL;
    while ((DMA_GetTransCompleteStatus(CM_DMA1, DMA_FLAG_TC_CH2) == RESET) &&
           (timeout-- > 0UL)) {
        /* spin */
    }

    /* Stop timer and clean up */
    TMRA_Stop(CM_TMRA_3);
    DMA_ChCmd(CM_DMA1, DMA_CH2, DISABLE);
    DMA_ClearTransCompleteStatus(CM_DMA1, DMA_FLAG_TC_CH2 | DMA_FLAG_BTC_CH2);

    /* Ensure reset period: pin stays LOW via the trailing zero compare values
     * that were already sent, plus this extra delay. */
    DDL_DelayUS(60UL);
}

void test_ws2812b(void)
{
    BSP_UART_Printf("[TEST] WS2812B PWM+DMA (2 LEDs on PB1) ...\r\n");

    LL_PERIPH_WE(BSP_LL_PERIPH_SEL);

    /* Enable peripheral clocks */
    FCG_Fcg2PeriphClockCmd(FCG2_PERIPH_TMRA_3, ENABLE);
    FCG_Fcg0PeriphClockCmd(FCG0_PERIPH_DMA1 | FCG0_PERIPH_AOS, ENABLE);

    /* PB1 → TMRA3_CH4 PWM output (Func_Grp1, GPIO_FUNC_5) */
    GPIO_SetFunc(BSP_WS2812_PORT, BSP_WS2812_PIN, BSP_WS2812_TMR_FUNC);

    /* TMRA3: sawtooth up-count, period = 124 → 800 kHz at PCLK1=100MHz */
    stc_tmra_init_t stcTmra;
    (void)TMRA_StructInit(&stcTmra);
    stcTmra.sw_count.u8CountMode = TMRA_MD_SAWTOOTH;
    stcTmra.sw_count.u8CountDir  = TMRA_DIR_UP;
    stcTmra.u32PeriodValue = WS_PWM_PERIOD;
    (void)TMRA_Init(CM_TMRA_3, &stcTmra);

    /* PWM CH4: HIGH at start, LOW on compare match */
    stc_tmra_pwm_init_t stcPwm;
    (void)TMRA_PWM_StructInit(&stcPwm);
    stcPwm.u32CompareValue        = 0U;
    stcPwm.u16StartPolarity       = TMRA_PWM_HIGH;
    stcPwm.u16StopPolarity        = TMRA_PWM_LOW;
    stcPwm.u16CompareMatchPolarity = TMRA_PWM_LOW;
    stcPwm.u16PeriodMatchPolarity  = TMRA_PWM_HIGH;
    (void)TMRA_PWM_Init(CM_TMRA_3, TMRA_CH4, &stcPwm);
    TMRA_PWM_OutputCmd(CM_TMRA_3, TMRA_CH4, ENABLE);

    LL_PERIPH_WP(BSP_LL_PERIPH_SEL);

    /* Color table (RGB, brightness capped at 32) */
    static const uint8_t colors[][3] = {
        { 32,   0,   0 },  /* Red */
        {  0,  32,   0 },  /* Green */
        {  0,   0,  32 },  /* Blue */
        { 32,  32,   0 },  /* Yellow */
        {  0,  32,  32 },  /* Cyan */
        { 32,   0,  32 },  /* Magenta */
        { 32,  32,  32 },  /* White (dim) */
        {  0,   0,   0 },  /* Off */
    };

    for (uint8_t i = 0U; i < 8U; i++) {
        /* Same color on both LEDs */
        const uint8_t both[2][3] = {
            { colors[i][0], colors[i][1], colors[i][2] },
            { colors[i][0], colors[i][1], colors[i][2] },
        };
        WS_FillBuffer(both);

        LL_PERIPH_WE(BSP_LL_PERIPH_SEL);
        WS_Send();
        LL_PERIPH_WP(BSP_LL_PERIPH_SEL);

        BSP_UART_Printf("  Color: R=%u G=%u B=%u\r\n",
                         colors[i][0], colors[i][1], colors[i][2]);
        BSP_WDT_Feed();
        BSP_DelayMS(800UL);
    }

    /* Clean up: disable PWM output, set pin to GPIO LOW */
    LL_PERIPH_WE(BSP_LL_PERIPH_SEL);
    TMRA_PWM_OutputCmd(CM_TMRA_3, TMRA_CH4, DISABLE);
    stc_gpio_init_t stcGpio;
    (void)GPIO_StructInit(&stcGpio);
    stcGpio.u16PinState = PIN_STAT_RST;
    stcGpio.u16PinDir   = PIN_DIR_OUT;
    (void)GPIO_Init(BSP_WS2812_PORT, BSP_WS2812_PIN, &stcGpio);
    LL_PERIPH_WP(BSP_LL_PERIPH_SEL);

    BSP_UART_Printf("[PASS] WS2812B\r\n");
}
