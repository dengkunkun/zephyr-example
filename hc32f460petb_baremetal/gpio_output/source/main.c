/**
 *******************************************************************************
 * @file  gpio/gpio_output/source/main.c
 * @brief USART2 TX/RX on PA2/PA3 via DAPLink VCP (COM9).
 *        Echo received bytes + button PE2 level change log.
 *
 * Board wiring (UYUP-RPI-A-2.3):
 *   DAPLink VCP RX ← PA9  (must be UART idle HIGH — set to GPIO_FUNC_36)
 *   MCU USART2 TX  → PA2  (GPIO_FUNC_36) → COM9 RX
 *   MCU USART2 RX  ← PA3  (GPIO_FUNC_37) ← COM9 TX
 *   BTN1           → PE2  (active-low, internal pull-up)
 *******************************************************************************
 */

#include "main.h"

/* LEDs: active-low */
#define LED3_PORT (GPIO_PORT_D)
#define LED3_PIN (GPIO_PIN_10)
#define LED4_PORT (GPIO_PORT_E)
#define LED4_PIN (GPIO_PIN_15)
#define LED3_ON() GPIO_ResetPins(LED3_PORT, LED3_PIN)
#define LED4_TOGGLE() GPIO_TogglePins(LED4_PORT, LED4_PIN)

/* Button BTN1: PE2, active-low */
#define BTN1_PORT (GPIO_PORT_E)
#define BTN1_PIN (GPIO_PIN_02)

#define LL_PERIPH_SEL                                                          \
  (LL_PERIPH_GPIO | LL_PERIPH_FCG | LL_PERIPH_PWC_CLK_RMU | LL_PERIPH_EFM |    \
   LL_PERIPH_SRAM)

/* ---------- helpers ------------------------------------------------------ */

static void USART2_SendStr(const char *s) {
  while (*s != '\0') {
    while (0UL == READ_REG32_BIT(CM_USART2->SR, USART_SR_TXE)) {
    }
    WRITE_REG16(CM_USART2->TDR, (uint16_t)(uint8_t)*s);
    s++;
  }
  while (0UL == READ_REG32_BIT(CM_USART2->SR, USART_SR_TC)) {
  }
}

static void USART2_SendChar(uint8_t ch) {
  while (0UL == READ_REG32_BIT(CM_USART2->SR, USART_SR_TXE)) {
  }
  WRITE_REG16(CM_USART2->TDR, (uint16_t)ch);
}

/* ---------- main --------------------------------------------------------- */

int32_t main(void) {
  stc_gpio_init_t stcGpio;
  stc_usart_uart_init_t stcInit;
  uint32_t u32Div;
  float32_t f32Err;
  int32_t i32Ret;
  uint32_t cnt = 0UL;
  uint8_t btnPrev = 1U; /* PE2 idle = HIGH (pull-up) */
  uint8_t btnCur;
  uint8_t btnStable = 1U;
  uint32_t btnDebounce = 0UL;

  LL_PERIPH_WE(LL_PERIPH_SEL);

  /* LED init */
  (void)GPIO_StructInit(&stcGpio);
  stcGpio.u16PinState = PIN_STAT_SET;
  stcGpio.u16PinDir = PIN_DIR_OUT;
  (void)GPIO_Init(LED3_PORT, LED3_PIN, &stcGpio);
  (void)GPIO_Init(LED4_PORT, LED4_PIN, &stcGpio);

  /* Button PE2 init: input with pull-up */
  (void)GPIO_StructInit(&stcGpio);
  stcGpio.u16PinDir = PIN_DIR_IN;
  stcGpio.u16PullUp = PIN_PU_ON;
  (void)GPIO_Init(BTN1_PORT, BTN1_PIN, &stcGpio);

  /* PA9 → USART TX alt func (DAPLink VCP enable) */
  GPIO_SetFunc(GPIO_PORT_A, GPIO_PIN_09, GPIO_FUNC_36);

  /* USART2 GPIO: PA2 = TX, PA3 = RX */
  GPIO_SetFunc(GPIO_PORT_A, GPIO_PIN_02, GPIO_FUNC_36);
  GPIO_SetFunc(GPIO_PORT_A, GPIO_PIN_03, GPIO_FUNC_37);

  /* USART2 clock */
  FCG_Fcg1PeriphClockCmd(FCG1_PERIPH_USART2, ENABLE);

  /* USART2 init with auto clock-divider search (MRC 8 MHz) */
  (void)USART_UART_StructInit(&stcInit);
  stcInit.u32OverSampleBit = USART_OVER_SAMPLE_8BIT;
  (void)USART_UART_Init(CM_USART2, &stcInit, NULL);

  for (u32Div = 0UL; u32Div <= USART_CLK_DIV64; u32Div++) {
    USART_SetClockDiv(CM_USART2, u32Div);
    i32Ret = USART_SetBaudrate(CM_USART2, 115200UL, &f32Err);
    if ((LL_OK == i32Ret) && (f32Err >= -0.025F) && (f32Err <= 0.025F)) {
      break;
    }
  }

  LL_PERIPH_WP(LL_PERIPH_SEL);

  /* Enable TX and RX */
  USART_FuncCmd(CM_USART2, USART_TX, ENABLE);
  USART_FuncCmd(CM_USART2, USART_RX, ENABLE);

  LED3_ON();
  USART2_SendStr("HC32F460 ready (USART2 @ 115200, echo + BTN1 on PE2)\r\n");

  for (;;) {
    /* --- Echo received bytes --- */
    if (0UL != READ_REG32_BIT(CM_USART2->SR, USART_SR_RXNE)) {
      uint8_t ch = (uint8_t)READ_REG16(CM_USART2->RDR);
      USART2_SendChar(ch);
    }
    /* Clear overrun error */
    if (0UL != READ_REG32_BIT(CM_USART2->SR, USART_SR_ORE)) {
      CLR_REG32_BIT(CM_USART2->CR1, USART_CR1_RE);
      SET_REG32_BIT(CM_USART2->CR1, USART_CR1_RE);
    }

    /* --- Button PE2 debounce & edge detect --- */
    btnCur = (uint8_t)GPIO_ReadInputPins(BTN1_PORT, BTN1_PIN);
    if (btnCur != btnStable) {
      btnDebounce++;
      if (btnDebounce >= 5000UL) {
        btnDebounce = 0UL;
        btnStable = btnCur;
        if (btnStable != btnPrev) {
          if (0U == btnStable) {
            USART2_SendStr("[BTN1] pressed\r\n");
          } else {
            USART2_SendStr("[BTN1] released\r\n");
          }
          btnPrev = btnStable;
          LED4_TOGGLE();
        }
      }
    } else {
      btnDebounce = 0UL;
    }

    /* Periodic heartbeat */
    cnt++;
    if (cnt >= 500000UL) {
      cnt = 0UL;
      USART2_SendStr("alive\r\n");
    }
  }
}
