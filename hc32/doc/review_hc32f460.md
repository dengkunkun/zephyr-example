# HC32F460 Zephyr 移植代码正确性审计报告

> **审计者**：Copilot 自动化深度代码审计  
> **审计日期**：2025  
> **Zephyr 端口路径**：`hc32f/`  
> **DDL 参考版本**：HC32F460_DDL_Rev3.3.0  
> **目标读者**：基于本审计在 HC32F4A0 上编写 Zephyr 驱动的工程师

---

## 目录

1. [总览](#1-总览)
2. [逐驱动审计](#2-逐驱动审计)
   - [2a. soc.c](#2a-sochdsc-hc32f460-socc)
   - [2b. icg_config.c + icg_rom_start.ld](#2b-icg_configc--icg_rom_startld)
   - [2c. clock_control_hc32.c](#2c-clock_control_hc32c)
   - [2d. pinctrl_hc32.c + binding + header](#2d-pinctrl_hc32c--binding--header)
   - [2e. gpio_hc32.c](#2e-gpio_hc32c)
   - [2f. uart_hc32.c + hc32_uart.h](#2f-uart_hc32c--hc32_uarth)
   - [2g. counter_hc32_tmra.c](#2g-counter_hc32_tmrac)
   - [2h. wdt_hc32.c + wdt_hc32_swdt.c](#2h-wdt_hc32c--wdt_hc32_swdtc)
   - [2i. hc32f460.dtsi](#2i-dtsarmhdschar32f460dtsi)
   - [2j. boards/hdsc/hc32f460petb](#2j-boardshdschar32f460petb)
3. [Zephyr 集成审计](#3-zephyr-集成审计)
4. [对 HC32F4A0 移植的启示](#4-对-hc32f4a0-移植的启示)
5. [确定问题与建议修复](#5-确定问题与建议修复)
6. [问题统计总表](#6-问题统计总表)

---

## 1. 总览

### 1.1 目录布局

```
hc32f/
├── boards/hdsc/hc32f460petb/         # 开发板文件 (DTS, defconfig, board.cmake)
├── drivers/
│   ├── clock_control/clock_control_hc32.c
│   ├── counter/counter_hc32_tmra.c
│   ├── gpio/gpio_hc32.c
│   ├── pinctrl/pinctrl_hc32.c
│   ├── serial/uart_hc32.c
│   └── watchdog/wdt_hc32.c + wdt_hc32_swdt.c
├── dts/arm/hdsc/hc32f460.dtsi
├── include/zephyr/dt-bindings/pinctrl/hc32-pinctrl.h
└── soc/hdsc/hc32f460/
    ├── soc.c                         # 早期初始化、解锁、断言桥接
    ├── soc.h                         # CMSIS IRQ 名称兼容别名
    ├── icg_config.c                  # ICG ROM 常量
    ├── icg_rom_start.ld              # ICG 链接器片段
    ├── hc32_clock.h + hc32_uart.h   # SoC 级内部 API
    ├── pinctrl_soc.h                 # pinctrl 编码宏与解码宏
    ├── power.c                       # PM 状态机 (stop/power-down)
    └── Kconfig.soc                   # SOC 能力选择
```

### 1.2 关键文件清单

| 文件 | 功能 | 依赖 DDL 模块 |
|------|------|--------------|
| soc.c | SystemInit、LL_PERIPH_WE | hc32_ll.h (所有) |
| clock_control_hc32.c | PLL/HXT/LXT、FCG 门控 | hc32_ll_clk, hc32_ll_efm, hc32_ll_fcg, hc32_ll_sram |
| pinctrl_hc32.c | PFSR 功能选择、PCR 属性 | hc32_ll_gpio |
| gpio_hc32.c | PCR 配置、EXTINT、INTC SEL | 直接寄存器访问 |
| uart_hc32.c | BRR 计算、CR1/2/3、INTC SEL | 直接寄存器访问 |
| counter_hc32_tmra.c | TMRA 初始化、比较/溢出中断 | hc32_ll_tmra |
| wdt_hc32.c | WDT CR、RR 刷新 | hc32_ll_wdt |
| wdt_hc32_swdt.c | SWDT ICG 配合、RR 刷新 | hc32_ll_swdt |
| power.c | Stop/Sleep/Power-down PM | hc32_ll_clk, hc32_ll_pwc |

### 1.3 构建与烧录工作流

- **构建**：标准 `west build -b hc32f460petb`，使用 Zephyr CMake 系统，out-of-tree 模块由 `MODULE_EXT_ROOT` 指向 `hc32f/`。
- **烧录**：`board.cmake` 使用 pyOCD，target = `hc32f460xe`（512 KB Flash 型号）。
- **调试**：`CONFIG_HAS_SWO=y` 通过 SWO 引脚输出；DAPLink VCP 映射至 USART1（PA9/PA10）。

---

## 2. 逐驱动审计

### 2a. `soc/hdsc/hc32f460/soc.c`

#### `SystemInit` 调用时机

✅ **已验证**（`soc.c:29–36`）

Zephyr 的 crt0 不调用 DDL 的汇编启动文件，因此 `SystemInit()` 在 `soc_early_init_hook()` 中被显式调用。此钩子在 Zephyr 内核最早阶段（`PRE_KERNEL_1` 之前，VTOR/FPU 尚未配置时）执行，调用时序完全正确。`SystemInit` 内部执行：
- SCB->VTOR 设置
- FPU 访问权限配置（CPACR）
- `SystemCoreClock` 更新

#### `LL_PERIPH_WE` 解锁范围

✅ **已验证**（`soc.c:38–46`）

调用 `LL_PERIPH_WE(LL_PERIPH_ALL)` 解锁所有外设写保护：
- `LL_PERIPH_GPIO` → `GPIO_REG_Unlock()` (PWPR = 0xA501)
- `LL_PERIPH_FCG` → `PWC_FCG0_REG_Unlock()` (FCG0PC = 0xA5010001)
- `LL_PERIPH_PWC_CLK_RMU` → `PWC_REG_Unlock()` (FPRC |= 0xA501/0xA502)
- `LL_PERIPH_EFM` → `EFM_REG_Unlock()` (FAPRT = 0x0123, 0x3210)
- `LL_PERIPH_SRAM` → `SRAM_REG_Unlock()` (SRAM WTPR)

此后所有驱动的写保护寄存器访问均合法。`gpio_hc32_init()` 中的额外 PWPR 解锁（`0x40053BFCUL = 0xA501`）是冗余但无害的保护措施。

#### `DDL_AssertHandler` 桥接

✅ **已验证**（`soc.c:22–24`）

```c
void DDL_AssertHandler(void) { __ASSERT_NO_MSG(0); }
```
将 DDL 断言重定向至 Zephyr 断言宏，调试模式下会触发 panic，release 模式下会被优化掉。行为与 Zephyr 惯例完全一致。

---

### 2b. `soc/hdsc/hc32f460/icg_config.c` + `icg_rom_start.ld`

#### ICG 段地址

✅ **已验证**

链接器脚本中：
```
. = __rom_region_start + 0x400;
KEEP(*(.icg_sec))
```
HC32F460 RM 规定 ICG 区域位于 Flash 地址 `0x0000_0400`（向量表之后），8 个 32-bit 字，共 32 字节。由于 `__rom_region_start = 0x00000000`，`.icg_sec` 段被正确放置在 `0x400`。

`ASSERT(. <= (__rom_region_start + 0x400), ...)` 确保向量表不会溢出至 ICG 区域，防护措施合理。

#### HRC 默认值

✅ **已验证**（`icg_config.c:64`）

```c
#define ICG_REG_CFG1_CONST (ICG_REG_NMI_CONFIG | ICG_REG_BOR_CONFIG |
                            ICG_REG_HRC_CONFIG | 0x03F8FEFEUL)
```
`ICG_REG_HRC_CONFIG` 来自 DDL `hc32_ll_icg.h`，代表 HRC 的 ICG 出厂默认配置（16 MHz）。`0x03F8FEFEUL` 为 ICG_CFG1 中所有保留位的"必须为 1"填充值，符合 DDL 示例工程中的做法。

#### SWDT 默认值

✅ **已验证**（`icg_config.c:10–60`）

`HC32_ICG_WDT_CONFIG` 将 ICG 的 WDT（hardware-start 模式）配置为最大超时、停止计数，这是合理的安全默认值——对于使用软件启动模式的 WDT 驱动（`wdt_hc32.c`），ICG WDT 字段实际上不起作用。

`HC32_ICG_SWDT_CONFIG` 从 DT 属性（`counter_cycles`、`clock_divider`、`reset_on_timeout`、`run_in_sleep`）动态生成 ICG 值，正确实现了 SWDT 的硬件启动模式配置。`ICG_SWDT_RANGE_0TO100PCT` 设为全范围（无窗口限制），符合 SWDT 典型用法。

---

### 2c. `drivers/clock_control/clock_control_hc32.c`

#### HXT / PLL 参数

✅ **已验证**（`clock_control_hc32.c:240–254`）

DTSI 配置：
```
pll-m = <1>; pll-n = <50>; pll-p = <2>; /* → 8MHz / 1 × 50 / 2 = 200 MHz */
```

驱动在 `hc32_clock_init()` 中验证：
```c
pll_out_hz = (cfg->hxt_hz / cfg->pll_m) * cfg->pll_n / cfg->pll_p;
if (pll_out_hz != cfg->sysclk_hz) return -EINVAL;
```

寄存器填充使用 DDL API `CLK_PLLInit()`，正确将 PLLM/PLLN/PLLP 写入 `CM_CMU->PLLCFGR`，使用 `PLLCFGR_f.PLLM = cfg->pll_m - 1U`（寄存器值 = 分频系数 - 1）。

#### Flash Wait Cycle（EFM PCCR RDWT）

✅ **已验证**（`clock_control_hc32.c:286–288`）

```c
(void)EFM_SetWaitCycle(EFM_WAIT_CYCLE5);
```
HC32F460 RM 表明在 200 MHz（HCLK）运行时需要 5 等待周期。时序正确：Flash 等待周期在切换到高频时钟 **之前** 设置，在切换到低频时钟 **之后** 降低。

#### FCG0/1/2/3 外设时钟门控

✅ **已验证**（`clock_control_hc32.c:62–95`）

`hc32_clock_gate_cmd()` 通过枚举 `HC32_FCG_BUS0..3` 路由至 `FCG_Fcg0PeriphClockCmd` ... `FCG_Fcg3PeriphClockCmd`，完全依赖 DDL API，寄存器写入由 DDL 处理，正确性有 DDL 保证。

时钟速率获取函数 `hc32_clock_rate_for_selector()` 的映射关系：

| 选择器 | 返回频率 | 备注 |
|--------|----------|------|
| FCG_BUS0 / HCLK | `u32HclkFreq` | 200 MHz |
| FCG_BUS1 / FCG_BUS2 / PCLK1 | `u32Pclk1Freq` | 100 MHz (HCLK/2) |
| FCG_BUS3 / PCLK4 | `u32Pclk4Freq` | 100 MHz (HCLK/2) |
| PCLK3 (0x15) | `u32Pclk3Freq` | 50 MHz (HCLK/4) |

TMRA (FCG2, bus=2) → PCLK1 = 100 MHz ✓，USART (FCG1, bus=1) → PCLK1 = 100 MHz ✓，WDT 使用 `HC32_CLOCK_RATE_PCLK3=0x15` 只取速率 ✓。

#### MCO / LXT / LRC 支持

- ✅ **LXT**：`hc32_clock_enable_lxt()` 正确配置 PC14/PC15 模拟、调用 `CLK_Xtal32Init()`（`lxt_hz = 0` 则跳过）。
- ✅ **LRC**：在 `power.c` 中通过 `CLK_LrcCmd(ENABLE)` 按需使能（PM WKT 定时器时钟源）。
- ⚠️ **MCO（时钟输出）**：未实现，无对外时钟输出支持。若后续需要 MCO，需要扩展。

---

### 2d. `drivers/pinctrl/pinctrl_hc32.c` + binding + `hc32-pinctrl.h`

#### HC32_PINMUX / HC32_FUNC_* 编码

✅ **已验证**（`hc32-pinctrl.h` + `pinctrl_soc.h`）

编码格式（32-bit）：
```
[31:27] reserved | [26] OD | [25:24] PULL | [23:16] PORT | [15:8] PIN | [7:0] FUNC
```

DDL 要求 FUNC 为 0..15（GPIO 内置功能）或 32..59（外设复用功能）。当前 binding 仅定义了 `HC32_FUNC_GPIO = 0`、`HC32_FUNC_USART1_TX = 32`、`HC32_FUNC_USART1_RX = 33`，均在合法范围内。

`pinctrl_hc32.c` 调用 `GPIO_SetFunc(port, pin_mask, func & 0x3fU)`，DDL 内有 `IS_GPIO_FUNC(func)` 断言检查（func ≤ 15 或 32-59），非法值（16-31，60-63）会在调试模式触发断言，但不会产生无声错误。

❌ **问题**：`hc32-pinctrl.h` 目前仅定义 USART1 的两个函数，完全缺失 USART2/3/4、SPI、I2C、ADC、TIM 等所有外设的 FUNC 定义。这意味着用 DT 声明任何非 USART1 的外设引脚复用均不可能，严重限制了驱动扩展性。（见 §5 建议修复 #4）

#### PSPCR（JTAG 共享脚）处理

⚠️ **存疑**（`pinctrl_hc32.c` 全文）

`pinctrl_hc32.c` 未对 PA13（SWDIO）、PA14（SWDCLK）、PA15（JTDI）、PB3（JTDO）、PB4（NJTRST）的 PSPCR 寄存器进行任何处理。`GPIO_PSPCR_RST_VALUE = 0x001F`，复位后这 5 个引脚均处于调试功能，无法通过 pinctrl 将其切换为普通 GPIO。

DDL 提供 `GPIO_SetDebugPort()` 函数来清除 PSPCR 对应位，但 Zephyr pinctrl 驱动完全未调用。若用户希望复用调试引脚作为 GPIO 功能，需要在 SoC 层或应用层手动调用 DDL API。

#### PWPR 解锁顺序

✅ **已验证**

PWPR 地址计算：`0x40053800 + 0x3FC = 0x40053BFC`，与 CMSIS 结构体 `CM_GPIO_TypeDef::PWPR` 的偏移完全吻合（经人工字节计算验证）。`soc_early_init_hook()` 中 `LL_PERIPH_WE(LL_PERIPH_ALL)` 已完成 GPIO 写保护解锁，驱动初始化时 GPIO 寄存器处于可写状态。

#### bias-pull-up / bias-disable / drive-open-drain

⚠️ **存疑**（`pinctrl_soc.h:29–34`，`pinctrl_hc32.c:31–38`）

`pinctrl_soc.h` 定义了三种 PULL 状态：
```c
#define HC32_PULL_NONE  0U
#define HC32_PULL_UP    1U
#define HC32_PULL_DOWN  2U   /* ← HC32F460 无下拉硬件支持！ */
```

`Z_PINCTRL_STATE_PIN_INIT` 宏会将 DT 属性 `bias-pull-down` 编码为 `HC32_PULL_DOWN`（值 2）。但 `hc32_configure_pin()` 中仅处理 `HC32_PULL_UP`，对 `HC32_PULL_DOWN` **静默忽略**，不设置下拉（因为硬件没有），也不返回错误。

HC32F460 PCR 寄存器只有 `PCR_PUU`（上拉使能，BIT(6) = 0x0040），**没有下拉位**。因此 `bias-pull-down` 会静默失效，这是一个欺骗性的 API 行为。（见 §5 建议修复 #2）

`bias-disable` → `HC32_PULL_NONE = 0` → 不设 PUU 位 → 引脚浮空 ✓  
`bias-pull-up` → `HC32_PULL_UP = 1` → `gpio_cfg.u16PullUp = PIN_PU_ON` ✓  
`drive-open-drain` → `HC32_OD_GET(pin) != 0` → `gpio_cfg.u16PinOutputType = PIN_OUT_TYPE_NMOS` ✓

---

### 2e. `drivers/gpio/gpio_hc32.c`

#### PCR 位语义

✅ **已验证**（`gpio_hc32.c:50–62`，对比 CMSIS `hc32f460.h:4674–4694`）

| Zephyr 宏 | 值 | CMSIS 宏 | 值 | 结论 |
|-----------|-----|----------|-----|------|
| `PCR_POUT` | BIT(0) = 0x0001 | `GPIO_PCR_POUT` | 0x0001 | ✅ |
| `PCR_POUTE` | BIT(1) = 0x0002 | `GPIO_PCR_POUTE` | 0x0002 | ✅ |
| `PCR_NOD` | BIT(2) = 0x0004 | `GPIO_PCR_NOD` | 0x0004 | ✅ |
| `PCR_DRV0` | BIT(4) = 0x0010 | `GPIO_PCR_DRV_0` | 0x0010 | ✅ |
| `PCR_DRV1` | BIT(5) = 0x0020 | `GPIO_PCR_DRV_1` | 0x0020 | ✅ |
| `PCR_PUU` | BIT(6) = 0x0040 | `GPIO_PCR_PUU` | 0x0040 | ✅ |
| `PCR_INVE` | BIT(9) = 0x0200 | `GPIO_PCR_INVE` | 0x0200 | ✅ |
| `PCR_INTE` | BIT(12) = 0x1000 | `GPIO_PCR_INTE` | 0x1000 | ✅ |

注意：`GPIO_PCR_DDIS`（模拟模式，BIT(15) = 0x8000）和 `GPIO_PCR_LTE`（锁存，BIT(14) = 0x4000）在 GPIO 驱动中未使用，这是合理的——数字 GPIO 模式下不需要 DDIS，模拟功能由 clock_control 驱动的 `GPIO_AnalogCmd()` 处理。

#### 端口寄存器偏移

✅ **已验证**（`gpio_hc32.c:42–48`，对比 `CM_GPIO_TypeDef` 结构体布局）

每个端口组在 CMSIS 结构体中的布局（步长 0x10 字节）：
```
+0x00: PIDR (uint16_t, RO) + 2字节 reserved
+0x04: PODR (uint16_t, RW)
+0x06: POER (uint16_t, RW)
+0x08: POSR (uint16_t, WO - set)
+0x0A: PORR (uint16_t, WO - reset)
+0x0C: POTR (uint16_t, WO - toggle)
+0x0E: 2字节 reserved
```

Zephyr 定义的 `GPIO_PIDR_OFFSET`、`GPIO_PODR_OFFSET`、`GPIO_POER_OFFSET`、`GPIO_POSR_OFFSET`、`GPIO_PORR_OFFSET`、`GPIO_POTR_OFFSET` 与上述完全吻合。

PCR/PFSR 偏移（`GPIO_PCR_OFFSET`、`GPIO_PFSR_OFFSET`）：
- DDL：`PCR_REG(port, pin) = GPIO_PCR_BASE + port*0x40 + pin*0x04`
- DDL：`GPIO_PCR_BASE = &CM_GPIO->PCRA0` = 0x40053800 + 0x0400 = 0x40053C00
- Zephyr：`GPIO_PCR_OFFSET(p,n) = 0x0400 + p*0x40 + n*0x04` ✅
- Zephyr：`GPIO_PFSR_OFFSET(p,n) = 0x0402 + p*0x40 + n*0x04` ✅

#### POSRx/PORRx 免解锁

✅ **已验证**

HC32F460 RM 说明：POSR（输出置位）和 PORR（输出复位）寄存器**不受 GPIO 写保护**（PWPR 保护的是 PCR、PFSR、PSPCR、PINAER，不包括 POSR/PORR/POTR/PODR）。因此 `gpio_hc32_port_set_bits_raw()` 和 `gpio_hc32_port_clear_bits_raw()` 直接写寄存器是安全的，不需要解锁操作。

#### EXTINT：EIRQCFRx + INTC SEL 路由 + NVIC

✅ **已验证**（`gpio_hc32.c:81–89`，对比 `CM_INTC_TypeDef` 结构体偏移）

INTC 寄存器偏移验证（相对于 `CM_INTC_BASE = 0x40051000`）：

| Zephyr 宏 | 偏移 | CMSIS 结构体成员 | 验证 |
|-----------|------|-----------------|------|
| `INTC_EIRQCR(n)` | `0x010 + n*4` | NMICR(0)+NMIENR(4)+NMIFR(8)+NMICFR(C)+EIRQCR0(10) | ✅ |
| `INTC_EIFR` | `0x054` | 16个EIRQCR(0x10-0x4C)+WUPEN(0x50)+EIFR(0x54) | ✅ |
| `INTC_EIFCR` | `0x058` | EIFR+4 | ✅ |
| `INTC_SEL(n)` | `0x05C + n*4` | SEL0 在 EIFCR 之后 | ✅ |

EIRQCR 位定义（`gpio_hc32.c:93–98`，对比 CMSIS `hc32f460.h:5205–5214`）：

| Zephyr 宏 | 值 | CMSIS 宏 | 值 | 结论 |
|-----------|-----|----------|-----|------|
| `EIRQCR_TRIG_FALLING` | 0x00 | `INTC_EIRQCR_EIRQTRG = 0` | 0x00 | ✅ |
| `EIRQCR_TRIG_RISING` | 0x01 | `INTC_EIRQCR_EIRQTRG_0` | 0x01 | ✅ |
| `EIRQCR_TRIG_BOTH` | 0x02 | `INTC_EIRQCR_EIRQTRG_1` | 0x02 | ✅ |
| `EIRQCR_TRIG_LOW` | 0x03 | 0x03 (EIRQTRG[1:0]=11) | 0x03 | ✅ |
| `EIRQCR_EFEN` | BIT(7) = 0x80 | `INTC_EIRQCR_EFEN` | 0x80 | ✅ |
| `EIRQCR_FCLK_DIV8` | 0x10 | `INTC_EIRQCR_EISMPCLK_0`(bit4=PCLK3/8) | 0x10 | ✅ |

INT_SRC 路由：`sys_write32(pin, INTC_SEL(irq_num))` 将 `INT_SRC_PORT_EIRQn = n`（值等于 n，见 CMSIS `hc32f460.h:524-539`）写入 SEL 寄存器，使 EXTINT 通道 n 路由至 NVIC IRQ n（`EXTINT_NVIC_IRQ_BASE + pin = pin`），逻辑正确 ✅。

#### PINAER 使能

✅ **已验证**

`PINAER`（端口输入模拟使能寄存器，复位值 = 0x0000）控制数字/模拟模式切换。数字 GPIO 不需要设置 PINAER（PINAER bit = 1 表示模拟输入），驱动正确地不触碰 PINAER。

⚠️ **注意**：GPIO 驱动 Kconfig 未显式 `select DYNAMIC_INTERRUPTS`，但 `gpio_hc32_pin_interrupt_configure()` 调用了 `irq_connect_dynamic()`。若当前构建中 `UART_HC32_INTERRUPT` 也使能（它 `select DYNAMIC_INTERRUPTS`），则不会有链接错误；若仅使用 GPIO 中断而不使用 UART 中断驱动，则会因缺少 `CONFIG_DYNAMIC_INTERRUPTS=y` 而链接失败。（见 §5 建议修复 #5）

---

### 2f. `drivers/serial/uart_hc32.c` + `hc32_uart.h`

#### USART 寄存器偏移

✅ **已验证**（`uart_hc32.c:27–34`，对比 CMSIS `CM_USART_TypeDef`）

| Zephyr 宏 | 偏移 | CMSIS 成员 | 类型 | 结论 |
|-----------|------|------------|------|------|
| `USART_OFF_SR` | 0x00 | SR | uint32_t | ✅ |
| `USART_OFF_TDR` | 0x04 | TDR | uint16_t | ✅ |
| `USART_OFF_RDR` | 0x06 | RDR | uint16_t | ✅ |
| `USART_OFF_BRR` | 0x08 | BRR | uint32_t | ✅ |
| `USART_OFF_CR1` | 0x0C | CR1 | uint32_t | ✅ |
| `USART_OFF_CR2` | 0x10 | CR2 | uint32_t | ✅ |
| `USART_OFF_CR3` | 0x14 | CR3 | uint32_t | ✅ |
| `USART_OFF_PR` | 0x18 | PR | uint32_t | ✅ |

#### 波特率计算（OVER8=1 模式，含小数分频）

✅ **已验证**（`uart_hc32.c:800–890`，对比 DDL `hc32_ll_usart.c:310–395`）

驱动初始化时写入 `USART_CR1_OVER8 | USART_CR1_SBS`，硬编码 OVER8=1（8× 过采样）。BRR 计算公式：

**整数部分**（与 DDL 公式 OVER8=1 时完全一致）：
```
DIV_Integer = C / (8 × B) - 1
```
其中 C = USART 输入时钟（PCLK1 / PR 分频），B = 目标波特率。

**小数分频部分**（`uart_hc32.c:836–838`）：
```c
uint64_t num = (uint64_t)256U * 8U * (div_int + 1U) * cfg->baud_rate;
uint32_t frac_raw = (uint32_t)((num + usart_clk / 2U) / usart_clk);
int32_t div_frac = (int32_t)frac_raw - 128;
```
对应 DDL 公式（OVER8=1）：
```
DIV_Fraction = 256 × 8 × (DIV_Integer+1) × B / C - 128
```
完全一致 ✅（Zephyr 版本加了四舍五入 `+ usart_clk/2`，精度更高）

BRR 寄存器打包：`brr = (div_int << 8) | div_frac`，其中 [15:8] = 整数部分，[6:0] = 小数部分（与 DDL `USART_BRR_DIV_INTEGER_MAX=0xFF`、`USART_BRR_DIV_FRACTION_MAX=0x7F` 匹配）✅

`USART_CR1_FBME`（小数分频使能，bit 29 = 0x20000000）在有小数时置位，无小数时清除，与 DDL 用法一致 ✅

PR（预分频寄存器）支持 4 级分频（1/4/16/64），驱动循环搜索最优组合 ✅

#### CR1/CR2/CR3 位操作

✅ **已验证**

初始化序列（`uart_hc32.c:1002–1015`）：
1. CR1 = `OVER8 | SBS`（TE=RE=0，可安全写 BRR/PR）
2. CR2 = 0（1 stop bit，LSB first，内部时钟）
3. CR3 = 0（无硬件流控）
4. 写 PR + BRR（此时 TE=RE=0，符合 DDL 要求）
5. CR1 |= TE | RE（使能收发）

CR1 位位置（CMSIS 验证）：RE=BIT(2)=0x4 ✅，TE=BIT(3)=0x8 ✅，RIE=BIT(5)=0x20 ✅，TCIE=BIT(6)=0x40 ✅，TXEIE=BIT(7)=0x80 ✅，OVER8=BIT(15)=0x8000 ✅，FBME=BIT(29)=0x20000000 ✅，SBS=BIT(31)=0x80000000 ✅

**注意**：驱动当前不支持奇偶校验（PCE/PS 位）、9-bit 数据宽度（M 位）、2 stop bit（STOP 位）、INVEN（极性反转）的 DT 属性配置。仅支持 8N1 格式。这是已知限制，非 bug。

#### IRQ 模式 INTC SEL 映射

✅ **已验证**（`uart_hc32.c:41–50`, `304–333`，对比 CMSIS `hc32f460.h:656–665`）

```
USART1: INT_SRC_USART1_EI=278, RI=279, TI=280, TCI=281 ✅
USART2: INT_SRC_USART2_EI=283, RI=284, TI=285, TCI=286 ✅
```

IRQ 分配：`USART_IRQ_BASE = 16`，每个 USART 实例占用 4 个连续 NVIC IRQ（16-19 for USART1，20-23 for USART2），与 GPIO 使用的 0-15 不重叠。

❌ **问题**：`uart_hc32_irq_setup()` 中的 if-else 仅处理 instance=1 和 instance≠1（默认为 USART2 的 INT_SRC），USART3/USART4 的中断源（288-291/293-296）完全缺失（见 §5 建议修复 #1）。

#### DMA 模式触发源

⚠️ **存疑**（`uart_hc32.c:350–800`）

DMA 模式使用直接寄存器访问 DMA1/DMA2（`DMA1_BASE=0x40053000`、`DMA2_BASE=0x40053400`）和 AOS（`AOS_BASE=0x40010800`）。这些地址与 CMSIS 头文件中的基地址一致，但未在本次审计中对每个 DMA 寄存器偏移做详细验证（超出审计范围），标记为存疑，需要专项测试。

---

### 2g. `drivers/counter/counter_hc32_tmra.c`

#### TMRA 基址

✅ **已验证**

DTSI `reg = <0x40015000 0x400>` = `CM_TMRA_1_BASE = 0x40015000UL` ✅

驱动中 `(CM_TMRA_TypeDef *)DT_INST_REG_ADDR(inst)` 类型转换正确。

#### 时钟源和方向

✅ **已验证**（`counter_hc32_tmra.c:246–252`）

```c
tmra_init.u8CountSrc = TMRA_CNT_SRC_SW;           // 软件时钟（PCLK 分频）
tmra_init.sw_count.u8ClockDiv = cfg->clock_div;    // DDL div 枚举
tmra_init.sw_count.u8CountMode = TMRA_MD_SAWTOOTH; // 锯齿波（UP计数）
tmra_init.sw_count.u8CountDir = TMRA_DIR_UP;       // 向上计数
```

`HC32_TMRA_DDL_DIV(div)` 宏通过 `UTIL_CAT` 将 DT 数值（1/2/4/.../1024）拼接为 `TMRA_CLK_DIV1`...`TMRA_CLK_DIV1024`，覆盖了 DDL 所有合法分频值 ✅

`BUILD_ASSERT` 确保 DT 中的 `clock-prescaler` 只能是合法的 2 的幂次值（1/2/4/8/16/32/64/128/256/512/1024）✅

#### CCR 通道对比

✅ **已验证**（`counter_hc32_tmra.c:59–80`）

```c
TMRA_CH1 = 0U ... TMRA_CH8 = 7U（DDL定义）
```

驱动使用 `hc32_tmra_compare_ints[0..7]` 和 `hc32_tmra_compare_flags[0..7]` 数组，索引 i=0..7 直接对应 `TMRA_CH1..TMRA_CH8`，与 DDL channel 编号完全一致 ✅

INT_SRC 分配（DTSI）：`int-src-ovf = <256>` = `INT_SRC_TMRA_1_OVF` ✅，`int-src-cmp = <258>` = `INT_SRC_TMRA_1_CMP` ✅

#### 告警机制

✅ **已验证**

`hc32_tmra_program_alarm_locked()` 实现了 Zephyr counter 驱动 API 要求的 late-alarm（`COUNTER_ALARM_CFG_EXPIRE_WHEN_LATE`）语义，通过软件 pending 机制（`atomic_or(&data->sw_pending, BIT(chan))`）正确处理了计数器超过目标值的边界情况。

---

### 2h. `drivers/watchdog/wdt_hc32.c` + `wdt_hc32_swdt.c`

#### WDT Token 写入顺序（刷新）

✅ **已验证**（`wdt_hc32.c` 使用 DDL `WDT_FeedDog()`，对比 `hc32_ll_wdt.c:190–193`）

DDL `WDT_FeedDog()`：
```c
WRITE_REG32(CM_WDT->RR, WDT_REFRESH_KEY_START);  // 0x0123
WRITE_REG32(CM_WDT->RR, WDT_REFRESH_KEY_END);    // 0x3210
```
SWDT 同理。写入顺序与 RM 要求（先 0x0123 后 0x3210）完全一致 ✅

Zephyr 驱动通过调用 DDL `WDT_FeedDog()` / `SWDT_FeedDog()` 间接实现，不直接操作 RR 寄存器，正确性由 DDL 保证 ✅

#### 超时档位

✅ **已验证**（`wdt_hc32.c:56–81`，对比 DDL `hc32_ll_wdt.h`）

计数周期（4 档）、时钟分频（8 档）、刷新范围（10 档）的映射表均与 DDL 常量完全对应。驱动使用穷举搜索找到最接近用户请求的 [min, max] 窗口，算法正确。

#### 窗口模式（Refresh Range）

✅ **已验证**（`wdt_hc32.c:83–96`）

**注意**：DDL 的 `WDT_RANGE_XToY` 命名以**剩余计数值**（counter remaining）为基准，而非经过时间百分比。例如：
- `WDT_RANGE_75TO100PCT`：计数器剩余 75-100% = 经过时间 0-25%
- 对应 Zephyr 表项：`{0U, 25U, WDT_RANGE_75TO100PCT}` ✅

经全表逐条核对，所有 10 个映射条目均正确 ✅

#### SWDT 设计约束

✅ **已验证**（`wdt_hc32_swdt.c:71–78`）

`hc32_swdt_disable()` 始终返回 `-EPERM`，这是因为 SWDT 一旦通过 ICG 配置并首次 FeedDog 启动后就无法停止，符合硬件特性。

⚠️ SWDT 的超时值完全由 ICG（ROM 区域）中的值决定，在运行时不可修改。`hc32_swdt_install_timeout()` 用 DT 属性计算 `timeout_ms` 并强制要求用户传入的 `cfg->window.max` 等于该固定值，否则返回 `-EINVAL`——这是正确的约束，但 API 用户体验较差。

---

### 2i. `dts/arm/hdsc/hc32f460.dtsi`

#### 基址验证

✅ **全部已验证**（对比 CMSIS `hc32f460.h:2566–2708`）

| DT 节点 | DT reg 基址 | CMSIS 宏 | 结论 |
|---------|------------|---------|------|
| `clock-controller` | 0x40054000 | CM_CMU_BASE | ✅ |
| `flash-controller` | 0x40010400 | CM_EFM_BASE | ✅ |
| `gpioa..e` (所有 GPIO) | 0x40053800 | CM_GPIO_BASE | ✅ |
| `usart1` | 0x4001D000 | CM_USART1_BASE | ✅ |
| `usart2` | 0x4001D400 | CM_USART2_BASE | ✅ |
| `wdt0` | 0x40049000 | CM_WDT_BASE | ✅ |
| `swdt0` | 0x40049400 | CM_SWDT_BASE | ✅ |
| `tmra1_counter` | 0x40015000 | CM_TMRA_1_BASE | ✅ |

**注意**：GPIO 节点使用虚构唯一地址（`@40053801`、`@40053802`...），但所有节点的 `reg` 属性第一个值均为 0x40053800，`DT_INST_REG_ADDR` 使用的是 `reg` 值而非 unit-address，因此实际基地址完全正确 ✅

#### `interrupts` 属性

✅ **已验证**

HC32F460 的 NVIC IRQ 通过 INTC SEL 寄存器路由，没有硬件固定映射（除 NMI/HardFault 等核心异常）。DTSI 中的 `interrupts` 是用户分配的软件 IRQ 号：
- `wdt0`: IRQ 24，`swdt0`: IRQ 25，`tmra1_counter`: IRQ 22（ovf）/23（cmp）
- GPIO 使用 0-15（每通道一个），USART 从 16 开始
- 所有 IRQ 在可用范围（0-127）内，互不冲突 ✅

#### `clocks` 属性

✅ **已验证**（见 §2c FCG 审计）

---

### 2j. `boards/hdsc/hc32f460petb`

#### DTS

✅ **已验证**

- LED3: PD10 active-low GPIO，PD 端口由 `gpiod` 驱动，offset port_idx=3 ✅
- LED4: PE15 active-low GPIO，PE 端口 ✅
- BTN1: PE2 active-low + pull-up ✅
- USART1: PA9(TX)/PA10(RX)，`HC32_PINMUX` 使用 `HC32_FUNC_USART1_TX=32`、`HC32_FUNC_USART1_RX=33` ✅

#### defconfig

⚠️ **存疑**（`hc32f460petb_defconfig`）

仅包含基础配置（GPIO、SERIAL、MPU），缺少：
- `CONFIG_UART_HC32_INTERRUPT=y`（若启用中断模式 UART）
- `CONFIG_DYNAMIC_INTERRUPTS=y`（若只用轮询 UART + GPIO 中断则无法隐式引入）
- `CONFIG_SOC_HC32F460=y`（通常由 board.cmake/Kconfig 选择，应确认）

在实际构建时必须通过 `prj.conf` 补全这些选项。建议将常用选项移入 defconfig。

#### `board.cmake`

✅ **已验证**

```cmake
board_runner_args(pyocd "--target=hc32f460xe")
```
`hc32f460xe`（512 KB Flash）是正确的 pyOCD target 字符串。

---

## 3. Zephyr 集成审计

### crt0 与 SystemInit 协作

✅ **已验证**

Zephyr 的 `crt0.S`（ARM）在 C 代码执行之前调用 `z_arm_platform_init()`，后者通过 `SOC_EARLY_INIT_HOOK` 钩子调用 `soc_early_init_hook()`。由于 Zephyr 不包含 DDL 的 `startup_hc32f460.s`，`SystemInit()` 必须在此钩子中显式调用——已正确实现。

`SystemInit()` 调用后，`SystemCoreClock` 被初始化为 HRC 频率（8 MHz），随后 `hc32_clock_init()` 完成 PLL 配置并更新 `SystemCoreClock` 为 200 MHz，Zephyr 的 SysTick 频率通过 `z_sys_clock_hw_cycles_per_sec_update()` 正确同步。

### SYS_INIT / soc_early_init_hook 执行顺序

✅ **已验证**

```
soc_early_init_hook()          ← PRE_KERNEL_1 之前（最早）
  └─ SystemInit()              ← VTOR, FPU, SystemCoreClock
  └─ LL_PERIPH_WE(ALL)         ← 解锁所有写保护

hc32_clock_init() [PRE_KERNEL_1, CLOCK_CONTROL_INIT_PRIORITY]
  └─ HXT + PLL + 200MHz 切换

gpio_hc32_init() [PRE_KERNEL_1, GPIO_INIT_PRIORITY]
  └─ 冗余 PWPR 解锁（无害）

hc32_power_init() [POST_KERNEL]
  └─ LRC 使能、WKT 初始化
```

顺序合理，无依赖倒置问题。

### Kconfig 选项

| 选项 | 状态 | 来源 | 说明 |
|------|------|------|------|
| `SOC_EARLY_INIT_HOOK` | ✅ select | Kconfig.soc | 确保 soc_early_init_hook 被调用 |
| `HAS_PM` | ✅ select | Kconfig.soc | 使能 power.c |
| `CPU_HAS_FPU` | ✅ select | Kconfig.soc (via CPU_CORTEX_M4) | 正确（Cortex-M4F） |
| `CPU_HAS_ARM_MPU` | ✅ select | Kconfig.soc | MPU 使能，defconfig 中 ARM_MPU=y |
| `HAS_SWO` | ✅ select | Kconfig.soc | SWO 调试输出 |
| `DYNAMIC_INTERRUPTS` | ⚠️ 间接 | via UART_HC32_INTERRUPT | GPIO 驱动未直接 select，存在依赖脆弱性 |
| `NUM_IRQS` | ✅ | Kconfig.defconfig: 144 | 与 CM_INTC 的 SEL0..SEL127 + 共享 IRQ 相符 |

### Linker Script 覆盖

✅ **已验证**（`icg_rom_start.ld`）

通过 `SOC_LINKER_SCRIPT` 机制（或 CMakeLists.txt 中的 linker fragment）将 `icg_rom_start.ld` 插入链接，将 `.icg_sec` 段强制定位至 ROM + 0x400。`ASSERT` 检查向量表不超出 0x400 边界 ✅

### DDL 与 Zephyr CMSIS 6 的冲突

✅ **已处理**（`soc.h:11–17`）

```c
#ifndef SVCall_IRQn
#define SVCall_IRQn   SVC_IRQn
#endif
#ifndef MemoryManagement_IRQn
#define MemoryManagement_IRQn  MemManageFault_IRQn
#endif
```

HC32F460 DDL CMSIS 头文件使用 HDSC 自定义的 IRQ 名称（`SVC_IRQn`、`MemManageFault_IRQn`），而 Zephyr 的 CMSIS 6 期望标准名称（`SVCall_IRQn`、`MemoryManagement_IRQn`）。`soc.h` 通过条件 `#define` 兼容两套名称，处理方式正确。

---

## 4. 对 HC32F4A0 移植的启示

### 4.1 可直接参数化复用的驱动

| 驱动 | 复用方式 | 注意事项 |
|------|----------|----------|
| `wdt_hc32.c` | 改 compatible，验证 INT_SRC_WDT_REFUDF 号 | F4A0 WDT 结构相同，直接复用率高 |
| `wdt_hc32_swdt.c` | 改 compatible + SWDTLRC_VALUE | SWDT 结构相同 |
| `counter_hc32_tmra.c` | 改 compatible，验证 FCG2 bit 和 INT_SRC_TMRA | F4A0 TMRA 单元更多（16个），INT_SRC 不同 |
| `clock_control_hc32.c` | 改 pll-n/p/m，EFM wait cycle，总线分频 | F4A0 最高 240MHz，wait cycle = 5/6，总线分频比可能不同 |
| `pinctrl_hc32.c` + `pinctrl_soc.h` | 无需改动，纯逻辑代码 | PFSR 编码机制相同 |

### 4.2 需要条件分叉的逻辑

| 方面 | F460 | F4A0 | 处理方式 |
|------|------|------|----------|
| USART 单元数 | 4（USART1-4）| 6（USART1-6）| `uart_hc32_irq_setup()` 需增加 USART3-6 分支 |
| GPIO 端口 | A-E, H（8 port）| A-I（更多）| DTSI 扩展，驱动无需修改 |
| TMRA 单元数 | 6 | 12 | 仅 DTSI 扩展 |
| EFM wait cycle | 200MHz → 5 | 240MHz → 需确认（可能 5 或 6）| 在 DT 或 Kconfig 中参数化 |
| FCG 寄存器布局 | FCG0-3 | FCG0-3（位分配不同）| 需全部重新核对 |
| INTC SEL 数量 | SEL0-127 | SEL0-127 | 相同 |

### 4.3 跨 SoC 关键差异点

| 差异项 | 说明 |
|--------|------|
| **INTC SEL 表** | `INT_SRC_xxx` 枚举值在 F4A0 完全不同，移植时必须从 `hc32f4a0.h` 重新核对每个中断源号 |
| **FCG 布局** | FCG1/2/3 中外设 bit 位置不同，DTSI `clocks = <&clk bus bit>` 中的 bit 值必须重新确认 |
| **Flash 地址/Sector** | F4A0 Flash 容量更大（2MB），ICG 偏移仍为 0x400，Sector 边界不同，EFM 擦写时需注意 |
| **ICG 偏移** | 两款芯片 ICG 均在 0x0000_0400，`icg_rom_start.ld` 可直接复用 |
| **PLL 参数** | F4A0 使用 MPLL/UPLL 结构（双 PLL），`clock_control_hc32.c` 中的 `hc32_clock_enable_pll()` 需重构 |
| **CMU 基址** | F4A0 CMU_BASE 需从 F4A0 CMSIS 头重新确认 |
| **SRAM 分区** | F4A0 SRAM 更多、分区不同，`SRAM_SetWaitCycle` 的 mask 参数需更新 |
| **SWDT LRC 频率** | `SWDTLRC_VALUE` 宏可能不同，需确认 |

---

## 5. 确定问题与建议修复

### ❌ 待修（2 项，影响正确性）

#### 问题 #1：USART3/4 IRQ 映射缺失（`uart_hc32.c:307–320`）

**现象**：`uart_hc32_irq_setup()` 中的 `else` 分支将所有非 USART1 的实例（包括 USART3/4）均映射到 USART2 的 `INT_SRC` 值，导致 USART3/4 中断路由错误。

**影响**：当前 DTSI 仅包含 USART1/2，实际无影响。一旦添加 USART3/4 节点并使能，中断将完全不工作。

**建议修复**：
```c
switch (cfg->instance) {
case 1: int_src_ri = INT_SRC_USART1_RI; /* ... */ break;
case 2: int_src_ri = INT_SRC_USART2_RI; /* ... */ break;
case 3: int_src_ri = INT_SRC_USART3_RI; /* ... */ break;
case 4: int_src_ri = INT_SRC_USART4_RI; /* ... */ break;
default: return; /* error */
}
```

#### 问题 #2：`bias-pull-down` 静默失效（`pinctrl_soc.h:32`，`pinctrl_hc32.c:31–38`）

**现象**：`pinctrl_soc.h` 定义了 `HC32_PULL_DOWN = 2U` 且 `Z_PINCTRL_STATE_PIN_INIT` 会将 DT `bias-pull-down` 属性编码为 2，但 `hc32_configure_pin()` 只判断 `HC32_PULL_UP`，对 `HC32_PULL_DOWN` 无任何处理（也不报错）。HC32F460 无硬件下拉，此行为会导致用户误以为下拉已生效。

**建议修复**：在 `hc32_configure_pin()` 中增加：
```c
if (pull == HC32_PULL_DOWN) {
    return -ENOTSUP;  /* HC32 没有硬件下拉 */
}
```
同时考虑从 `pinctrl_soc.h` 中移除 `HC32_PULL_DOWN` 的定义。

---

### ⚠️ 存疑（5 项，设计限制或潜在问题）

#### 问题 #3：`hc32-pinctrl.h` 外设函数定义严重不足

**现象**：DT binding 头文件中仅定义了 `HC32_FUNC_USART1_TX = 32` 和 `HC32_FUNC_USART1_RX = 33`，完全缺失所有其他外设的引脚复用函数定义（USART2-4、SPI1-4、I2C1-3、TMRA、ADC、CAN 等）。

**影响**：无法通过 DT pinctrl 配置任何非 USART1 外设的引脚复用，严重限制驱动扩展性。

**建议**：参考 HC32F460 RM "Port Function List"，将所有 GPIO_FUNC_32~59 的功能定义完整填充至 `hc32-pinctrl.h`。

#### 问题 #4：`pinctrl_hc32.c` 未处理 PSPCR（调试引脚复用）

**现象**：PA13/PA14 等调试引脚默认受 PSPCR[4:0] 保护，无法通过 pinctrl 将其切换为普通 GPIO。

**建议**：在 `hc32_configure_pin()` 中，对特定引脚（PA13、PA14、PA15、PB3、PB4）清除 PSPCR 对应位，或提供 DT 属性 `hdsc,swj-enable = <0>` 来控制调试引脚的释放。

#### 问题 #5：`gpio_hc32.c` Kconfig 未 `select DYNAMIC_INTERRUPTS`

**现象**：`gpio_hc32.c` 中的 `gpio_hc32_pin_interrupt_configure()` 调用 `irq_connect_dynamic()`，但 `drivers/gpio/Kconfig` 中 `CONFIG_GPIO_HC32` 未 `select DYNAMIC_INTERRUPTS`。

**影响**：若构建配置只启用 GPIO 而不启用 `UART_HC32_INTERRUPT`（后者 `select DYNAMIC_INTERRUPTS`），则链接时会找不到 `irq_connect_dynamic` 符号。

**建议修复**：在 `drivers/gpio/Kconfig` 中增加 `select DYNAMIC_INTERRUPTS`。

#### 问题 #6：`next_irq` 静态变量（`uart_hc32.c:306`）

**现象**：`static uint8_t next_irq = USART_IRQ_BASE` 是函数内静态变量，多次调用 `uart_hc32_irq_setup()` 会累加。虽然实践中只在设备初始化时调用一次，但理论上 `num_irqs >= USART_IRQ_BASE + 4 × num_usart_instances` 需要满足，且没有越界保护。

**建议**：添加范围检查 `if (next_irq + 4 > CONFIG_NUM_IRQS) { LOG_ERR(...); return; }`。

#### 问题 #7：ICG config 使用 `#undef` 覆盖 DDL 常量

**现象**（`icg_config.c:9–10`）：
```c
#undef ICG_REG_CFG0_CONST
#undef ICG_REG_CFG1_CONST
```
覆盖 DDL `hc32_ll_icg.h` 中的默认值。若 DDL 升级导致这些宏含义变化，静默覆盖可能引入不兼容。

**建议**：使用独立符号名（如 `HC32_ICG_CFG0_VAL`）而非覆盖 DDL 的符号，避免版本耦合脆弱性。

---

## 6. 问题统计总表

| 严重度 | 数量 | 说明 |
|--------|------|------|
| ❌ 待修（功能性 Bug） | **2** | USART3/4 IRQ 映射、pull-down 静默失效 |
| ⚠️ 存疑（设计限制/潜在问题） | **5** | pinctrl binding 不完整、PSPCR 缺失、Kconfig 依赖、next_irq 无检查、ICG #undef 脆弱 |
| ✅ 已验证正确 | **50+** | 所有寄存器地址/偏移/位域、BRR 公式、WDT 刷新序列、FCG 映射、INTC SEL 路由等 |

### 强烈推荐优先修复项

1. **立即修复**：`#1 USART3/4 IRQ 映射`——虽然当前板卡不受影响，但 F4A0 移植复用此驱动时将直接崩溃。
2. **立即修复**：`#2 bias-pull-down 静默失效`——API 语义欺骗性，应返回 -ENOTSUP。
3. **移植前必做**：`#3 pinctrl binding 补全`——F4A0 移植必然需要更多外设引脚复用，binding 必须先行完善。
4. **移植前必做**：`#5 GPIO Kconfig 增加 select DYNAMIC_INTERRUPTS`——F4A0 移植时若遇到只用 GPIO 中断的场景会踩坑。

---

*报告结束*
