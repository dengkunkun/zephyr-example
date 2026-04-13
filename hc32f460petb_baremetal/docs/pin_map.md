# UYUP-RPI-A-2.3 开发板引脚映射 (HC32F460PETB)

> 芯片: HC32F460PETB (Cortex-M4, LQFP-100)
> 开发板: UYUP-RPI-A-2.3 (多芯片共用 PCB，以下仅列出 HC32F460 实际连线)
> 原理图: UYUP-RPI-A-2.3.pdf / UYUP-User-Manual-and-Schematic-1.5.pdf

---

## 1. 时钟源

| 信号 | 端口 | 频率 | 备注 |
|------|------|------|------|
| XTAL (OSC_IN/OUT) | PH0 / PH1 | 24 MHz | 外部高速晶振 |
| XTAL32 (OSC32_IN/OUT) | PC14 / PC15 | 32.768 kHz | RTC 低速晶振 |

**PLL 配置 (MPLL → 200MHz):**
- MPLL_M = 3 (24MHz / 3 = 8MHz)
- MPLL_N = 50 (8MHz × 50 = 400MHz VCO)
- MPLL_P = 2 (400MHz / 2 = 200MHz SYSCLK)

---

## 2. LED

| 名称 | 端口/引脚 | 类型 | 驱动方式 | 备注 |
|------|-----------|------|----------|------|
| LED3 | PD10 | 普通 LED | Active-Low (VDD→1K→LED→PD10) | LEDB (蓝) |
| LED4 | PE15 | 普通 LED | Active-Low (VDD→1K→LED→PE15) | LEDG (绿) |
| LED5 | PB1 (DI) | WS2812B | 数据驱动 (Timer3 CH4 + DMA) | 链式: LED5(DO)→LED6(DI) |
| LED6 | 链式 | WS2812B | 由 LED5 DO 驱动 | 第二颗 RGB LED |

---

## 3. 按键

| 名称 | 端口/引脚 | 类型 | 备注 |
|------|-----------|------|------|
| BTN0 | PA3 | Active-Low | GND→PA3，需内部上拉 |
| BTN1 | PE2 | Active-Low | GND→PE2，需内部上拉 |

---

## 4. 串口 (USART)

| 外设 | TX 引脚 | RX 引脚 | 功能码 | 波特率 | 用途 |
|------|---------|---------|--------|--------|------|
| USART1 (CM_USART1) | PA9 | PA10 | GPIO_FUNC_32 / GPIO_FUNC_33 | 115200 | DAPLink VCP (COM9 / /dev/ttyACM1) |
| USART3 (CM_USART3) | PD8 | PD9 | TBD | 115200 | RS485 (SP3485EN) |

> **注意**: PA2/PA3 属于板载 U3 (STM32F042F6P6)，非 HC32F460 的可用引脚。
> HC32F460 唯一的调试串口为 USART1(PA9/PA10) 通过 DAPLink VCP。

---

## 5. RS485 (SP3485EN-L/TR)

| 信号 | 端口/引脚 | 方向 | 备注 |
|------|-----------|------|------|
| TXD | PD8 | OUT | USART3_TX |
| RXD | PD9 | IN | USART3_RX |
| DE/RE | PA0 | OUT | HIGH=发送, LOW=接收 |

---

## 6. CAN (CAN1)

| 信号 | 端口/引脚 | 备注 |
|------|-----------|------|
| CAN1_TX | PE5 | → CAN 收发器 → CANH |
| CAN1_RX | PE6 | ← CAN 收发器 ← CANL |

> 原理图中 PD0/PD1 标注的 CAN1_TX/RX 为其它芯片变体，HC32F460 使用 PE5/PE6。

---

## 7. I2C (I2C3)

> **重要**: PB8/PB9 属于 Func_Grp2（数据手册 Table 2-1），GPIO_FUNC_48/49 映射到 **I2C3**（非 I2C1）。

| 信号 | 端口/引脚 | 功能码 | 备注 |
|------|-----------|--------|------|
| SCL | PB8 | GPIO_FUNC_49 | I2C3_SCL (Func_Grp2) |
| SDA | PB9 | GPIO_FUNC_48 | I2C3_SDA (Func_Grp2) |

### 连接设备: AT24C64D EEPROM
- I2C 7-bit 地址: **0x50** (A0=A1=A2=GND)
- 容量: 64Kbit (8KB)
- 页写大小: 32 bytes

---

## 8. SPI3 → W25Q32 Flash

> **重要**: PB3/PB4/PB5 属于 Func_Grp2（数据手册 Table 2-1），GPIO_FUNC_40-43 映射到 **SPI3**（非 SPI1）。
> PB3(JTDO) 和 PB4(NJTRST) 默认被 JTAG PSPCR 锁定，必须调用 `GPIO_SetDebugPort(0x14U, DISABLE)` 释放。

| 信号 | 端口/引脚 | 功能码 | 备注 |
|------|-----------|--------|------|
| SCK | PB3 | GPIO_FUNC_40 | SPI3_SCK (Func_Grp2, JTDO) |
| MISO (IO1) | PB4 | GPIO_FUNC_41 | SPI3_MISO (Func_Grp2, NJTRST) |
| MOSI (IO0) | PB5 | GPIO_FUNC_43 | SPI3_MOSI (Func_Grp2) |
| CS | PE3 | GPIO 控制 | 软件 CS，Active-Low |

### W25Q32 参数
- 容量: 32Mbit (4MB)
- 页写大小: 256 bytes
- 扇区大小: 4KB
- 块大小: 64KB
- 标准 SPI 模式（非 QSPI）

> SPI2 (PB10/PC2/PC3) 未连接任何设备。

---

## 9. SD 卡 (SDIOC)

| 信号 | 端口/引脚 | 备注 |
|------|-----------|------|
| SD_CMD | PD2 | 命令线 |
| SD_CLK | PC12 | 时钟 |
| SD_D0 | PC8 | 数据线 0 |
| SD_D1 | PC9 | 数据线 1 |
| SD_D2 | PC10 | 数据线 2 |
| SD_D3 | PC11 | 数据线 3 |
| SD_DET | PE14 | 卡检测 (Card Detect) |

---

## 10. USB

| 信号 | 端口/引脚 | 备注 |
|------|-----------|------|
| USB_D+ | PA12 | USB 数据正 |
| USB_D- | PA11 | USB 数据负 |
| USB_ON | PB15 | USB 电源使能 |

---

## 11. Timer PWM

| Timer | 通道 | 端口/引脚 | 原理图标注 | 备注 |
|-------|------|-----------|------------|------|
| Timer4 | CH1 | PD12 | T4C1 | PWM 输出 |
| Timer4 | CH2 | PD13 | T4C2 | PWM 输出 |
| Timer4 | CH3 | PD14 | T4C3 | PWM 输出 |
| Timer4 | CH4 | PD15 | T4C4 | PWM 输出 |
| Timer3 | CH4 | PB1 | T3C4 | WS2812B 数据驱动 |

---

## 12. 调试接口 (SWD)

| 信号 | 端口/引脚 | 备注 |
|------|-----------|------|
| SWDIO | PA13 | 调试数据 |
| SWCLK | PA14 | 调试时钟 |
| NRST | NRST | 复位 |

板载调试器: U3 (STM32F042F6P6) — Open Cherry USB CMSIS-DAP
- Windows: 设备管理器 "Open Cherry USB CMSIS-DAP"
- WSL2: Bus 001 Device 005, ID 0d28:0204
- pyOCD 兼容，目标: hc32f460xe

---

## 13. 电源

| 信号 | 电压 | 备注 |
|------|------|------|
| VDD | 3.3V | MCU 数字电源 |
| VDDA | 3.3V | 模拟电源 (ADC/DAC 参考) |
| +5V | 5V | USB 供电 |
| EXT_3V3 | 3.3V | 外部 3.3V |

---

## 14. 引脚汇总表 (按端口)

### Port A
| Pin | 功能 | 备注 |
|-----|------|------|
| PA0 | RS485 DE/RE | SP3485EN 方向控制 |
| PA3 | BTN0 | 按键，Active-Low |
| PA9 | USART1_TX | VCP 调试串口 |
| PA10 | USART1_RX | VCP 调试串口 |
| PA11 | USB_D- | USB 设备 |
| PA12 | USB_D+ | USB 设备 |
| PA13 | SWDIO | SWD 调试 |
| PA14 | SWCLK | SWD 调试 |

### Port B
| Pin | 功能 | 备注 |
|-----|------|------|
| PB1 | WS2812B DI | RGB LED (Timer3 CH4) |
| PB3 | SPI3_SCK | W25Q32 时钟 (Func_Grp2, JTDO) |
| PB4 | SPI3_MISO | W25Q32 数据输入 (Func_Grp2, NJTRST) |
| PB5 | SPI3_MOSI | W25Q32 数据输出 (Func_Grp2) |
| PB8 | I2C3_SCL | AT24C64D EEPROM (Func_Grp2) |
| PB9 | I2C3_SDA | AT24C64D EEPROM (Func_Grp2) |
| PB15 | USB_ON | USB 电源使能 |

### Port C
| Pin | 功能 | 备注 |
|-----|------|------|
| PC8 | SD_D0 | SD 卡数据 |
| PC9 | SD_D1 | SD 卡数据 |
| PC10 | SD_D2 | SD 卡数据 |
| PC11 | SD_D3 | SD 卡数据 |
| PC12 | SD_CLK | SD 卡时钟 |
| PC14 | OSC32_IN | 32.768kHz 晶振 |
| PC15 | OSC32_OUT | 32.768kHz 晶振 |

### Port D
| Pin | 功能 | 备注 |
|-----|------|------|
| PD2 | SD_CMD | SD 卡命令 |
| PD8 | USART3_TX | RS485 发送 |
| PD9 | USART3_RX | RS485 接收 |
| PD10 | LED3 | 蓝色 LED (Active-Low) |
| PD12 | Timer4_CH1 | PWM 输出 |
| PD13 | Timer4_CH2 | PWM 输出 |
| PD14 | Timer4_CH3 | PWM 输出 |
| PD15 | Timer4_CH4 | PWM 输出 |

### Port E
| Pin | 功能 | 备注 |
|-----|------|------|
| PE2 | BTN1 | 按键，Active-Low |
| PE3 | SPI3_CS (W25Q32) | Flash 片选 (Active-Low) |
| PE5 | CAN1_TX | CAN 发送 |
| PE6 | CAN1_RX | CAN 接收 |
| PE14 | SD_DET | SD 卡检测 |
| PE15 | LED4 | 绿色 LED (Active-Low) |

### Port H
| Pin | 功能 | 备注 |
|-----|------|------|
| PH0 | OSC_IN | 24MHz 晶振 |
| PH1 | OSC_OUT | 24MHz 晶振 |

---

## 15. HC32F460 GPIO 功能组 (Func_Grp) 注意事项

HC32F460 每个引脚属于固定的 **Func_Grp1** 或 **Func_Grp2**（见数据手册 Table 2-1）。
同一 GPIO_FUNC 编号在不同功能组映射到 **不同的外设单元**：

| FUNC 编号 | Func_Grp1 | Func_Grp2 |
|-----------|-----------|-----------|
| FUNC_32-33 | USART1 | USART3 |
| FUNC_40-43 | SPI1 | **SPI3** |
| FUNC_44-47 | SPI2 | SPI4 |
| FUNC_48-49 | I2C1 | **I2C3** |
| FUNC_50-51 | I2C2 | CAN |

**本板受影响的引脚（均为 Func_Grp2）：**
- PB3, PB4, PB5 → SPI3（不是 SPI1）
- PB8, PB9 → I2C3（不是 I2C1）

**JTAG PSPCR 锁定：**
PB3 = JTDO, PB4 = NJTRST，默认被 PSPCR 寄存器（偏移 0x00，值 0x1F）锁定为 JTAG 功能。
必须在 BSP 初始化时调用 `GPIO_SetDebugPort(0x14U, DISABLE)` 释放这两个引脚（保留 SWD）。
