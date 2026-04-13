/**
 *******************************************************************************
 * @file  test_ws2812b.c
 * @brief WS2812B RGB LED test for UYUP board.
 *        Data pin PB1, chain of 2 LEDs (LED5, LED6).
 *
 *        HARDWARE LIMITATION:
 *        WS2812B VDD is connected to the 3.3V rail (ME6211C33M5G LDO) on this
 *        board. The WS2812B datasheet (XL-3528RGBW-WS2812B) specifies
 *        VDD_min = 3.5V. At 3.3V the internal timing logic is unreliable —
 *        all bits are read as "1" regardless of T0H duration, producing
 *        constant white. Verified with NOP-based bit-bang T0H sweep from
 *        20ns to 385ns (9 configs) and DWT-based bit-bang — no color change.
 *
 *        GPIO output on PB1 is verified correct (PIDRB readback). The data
 *        signal DOES reach the WS2812B (LEDs turn on from off state), but
 *        the chip cannot properly decode it at 3.3V.
 *
 *        Fix: add a level shifter (3.3V→5V) on PB1, or supply WS2812B VDD
 *        from 5V via the D3 (SS24L Schottky) path.
 *
 *        Datasheet timing reference:
 *          T0H typ=0.25us max=0.47us  (HIGH for "0" bit)
 *          T1H min=0.58us typ=0.85us  (HIGH for "1" bit)
 *          Cycle min=1.20us typ=1.25us
 *          Reset min=80us
 *          Data order: GRB, MSB first
 *
 *        HCLK = 200 MHz -> 5 ns / cycle
 *******************************************************************************
 */
#include "bsp_uyup.h"
#include "test_all.h"

#define WS_LED_COUNT    (BSP_WS2812_LED_COUNT)   /* 2 */
#define WS_PIN_MASK     (BSP_WS2812_PIN)          /* GPIO_PIN_01 = 0x0002 */

/* POSRB offset=0x18, PORRB offset=0x1A from CM_GPIO base */
#define GPIO_POSRB_OFF  (0x18)
#define GPIO_PORRB_OFF  (0x1A)

/* Timing: n_nop=15 → T0H≈235ns (typ 250ns), T1H=140cyc=700ns, period=250cyc=1.25us */
#define WS_T0H_NNOP    (15U)
#define WS_T1H_CYC     (140U)
#define WS_TBIT_CYC    (250U)

/**
 * @brief  Send one byte (8 bits, MSB first) to WS2812B.
 *         T0H uses NOP loop (~3 cycles/iter), T1H and period use DWT->CYCCNT.
 *         Compiled at -O2 for tight code generation.
 */
__attribute__((optimize("O2"), noinline))
static void WS_SendByte(uint8_t val)
{
    register volatile uint16_t *gpio = (volatile uint16_t *)CM_GPIO_BASE;
    register volatile uint32_t *cyccnt = &DWT->CYCCNT;
    register uint16_t mask = WS_PIN_MASK;

    for (int8_t bit = 7; bit >= 0; bit--) {
        uint32_t t0 = *cyccnt;

        /* Set HIGH */
        *(volatile uint16_t *)((uint8_t *)gpio + GPIO_POSRB_OFF) = mask;

        if (val & (1U << bit)) {
            /* BIT 1: long HIGH via DWT spin */
            while ((*cyccnt - t0) < WS_T1H_CYC) ;
        } else {
            /* BIT 0: short HIGH via NOP loop (3 cycles/iter, register counter) */
            register uint32_t n = WS_T0H_NNOP;
            while (n-- > 0U) {
                __NOP();
            }
        }

        /* Set LOW */
        *(volatile uint16_t *)((uint8_t *)gpio + GPIO_PORRB_OFF) = mask;

        /* Wait for total bit period */
        while ((*cyccnt - t0) < WS_TBIT_CYC) ;
    }
}

/**
 * @brief  Send full WS2812B frame (GRB order, MSB first).
 */
__attribute__((optimize("O2")))
static void WS_SendFrame(const uint8_t rgb[][3])
{
    __disable_irq();
    __DSB();
    __ISB();

    for (uint8_t led = 0U; led < WS_LED_COUNT; led++) {
        WS_SendByte(rgb[led][1]);   /* G */
        WS_SendByte(rgb[led][0]);   /* R */
        WS_SendByte(rgb[led][2]);   /* B */
    }
    CM_GPIO->PORRB = WS_PIN_MASK;   /* ensure LOW */
    __DSB();
    __enable_irq();

    DDL_DelayUS(300U);               /* reset > 80us */
}

void test_ws2812b(void)
{
    BSP_UART_Printf("[TEST] WS2812B (2 LEDs on PB1) ...\r\n");

    LL_PERIPH_WE(BSP_LL_PERIPH_SEL);

    /* Enable DWT cycle counter */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* Reset PB1 to GPIO mode */
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

    /* PB1 connectivity check */
    CM_GPIO->POSRB = WS_PIN_MASK;
    DDL_DelayUS(10U);
    uint16_t pidr_hi = CM_GPIO->PIDRB;
    CM_GPIO->PORRB = WS_PIN_MASK;
    DDL_DelayUS(10U);
    uint16_t pidr_lo = CM_GPIO->PIDRB;

    if (!(pidr_hi & WS_PIN_MASK) || (pidr_lo & WS_PIN_MASK)) {
        BSP_UART_Printf("[FAIL] WS2812B PB1 readback mismatch\r\n");
        return;
    }

    /* Demo: cycle through RED, GREEN, BLUE */
    const uint8_t red[2][3]   = {{32,0,0},{32,0,0}};
    const uint8_t green[2][3] = {{0,32,0},{0,32,0}};
    const uint8_t blue[2][3]  = {{0,0,32},{0,0,32}};
    const uint8_t black[2][3] = {{0,0,0},{0,0,0}};

    WS_SendFrame(red);
    BSP_UART_Printf("  RED\r\n");
    BSP_WDT_Feed();
    BSP_DelayMS(500UL);

    WS_SendFrame(green);
    BSP_UART_Printf("  GRN\r\n");
    BSP_WDT_Feed();
    BSP_DelayMS(500UL);

    WS_SendFrame(blue);
    BSP_UART_Printf("  BLU\r\n");
    BSP_WDT_Feed();
    BSP_DelayMS(500UL);

    WS_SendFrame(black);
    CM_GPIO->PORRB = WS_PIN_MASK;

    BSP_UART_Printf("[SKIP] WS2812B (VDD=3.3V < 3.5V min spec, colors unreliable)\r\n");
}
