# FMR_CC 裸机工程业务逻辑参考手册

> **目标读者**：熟悉 Zephyr RTOS 但未接触过本裸机工程的嵌入式工程师。  
> **原始工程**：`/mnt/d/embed/HC32/FMR_CC_SW/v1_2/src`，共 24 个 `.c` 文件，约 5900 行。  
> **芯片**：HDSC HC32F4A0PGTB（LQFP100，Cortex-M4F，240 MHz）  
> **DDL 版本**：Rev2.4.0

---

## 目录

1. [总览](#1-总览)
2. [引脚映射表](#2-引脚映射表)
3. [时钟/系统配置](#3-时钟系统配置)
4. [各模块业务逻辑](#4-各模块业务逻辑)
5. [软定时器任务表](#5-软定时器任务表)
6. [协议帧格式](#6-协议帧格式)
7. [中断映射表](#7-中断映射表)
8. [Zephyr 迁移映射](#8-zephyr-迁移映射)
9. [重写关键注意点](#9-重写关键注意点)

---

## 1. 总览

### 1.1 整体架构

本工程是一个纯裸机（bare-metal）超级循环架构，无 RTOS，无任务抢占。主循环以及轮询驱动的软定时器共同承担所有业务。

```
main()
├── 系统初始化：io_init → lsc_init → debug_init → stimer_init
├── 外设初始化：moto_init → wbus_init → imu_init → servo_init
│              → comm_485_1_init → usbul_init → battery_init
├── 注册软定时器（6 个，见第 5 章）
└── 主循环（for;;）
    ├── msg_poll()          ← 弹出消息队列，分发到已注册的回调
    ├── lsc_update()        ← LED 闪烁状态机
    ├── stimer_poll()       ← 软定时器超时检查，触发对应回调
    ├── manual_ctrl_poll()  ← RC 遥控 → 运动学解算 → moto_cmd
    ├── moto_poll()         ← Modbus 请求/应答状态机
    ├── imu_poll()          ← IMU 帧解析状态机
    ├── wbus_poll()         ← WBUS/SBUS 帧解析状态机
    ├── servo_poll()        ← FT 舵机请求/应答状态机
    ├── comm_485_1_poll()   ← 升降机请求/应答状态机
    ├── usbul_poll()        ← USB CDC 下行命令分发
    └── usb_dev_cdc_poll()  ← USB CDC 底层轮询
```

**软定时器（soft_timer）**：基于 `sys_get_tick()`（100 µs 分辨率）实现，最多注册 32 个。无中断，全部在主循环中 `stimer_poll()` 轮询触发。

**消息队列（msg）**：单向循环队列，最大 512 条，128 个回调槽位。消息仅含 `id` 与 `void *arg`，在 `msg_poll()` 中按 FIFO 顺序同步执行。当前注册的消息：

| 消息 ID | 枚举名 | 注册回调 |
|---------|-------|---------|
| 1 | `MSG_ID_DEBUG_RX_EXEC` | `debug_recv_exec()` in `debug.c` |
| 2 | `MSG_ID_UPLOAD` | `upload()` in `usb_uplink.c` |
| 3 | `MSG_ID_UPLOAD_IMU` | `upload_imu()` in `usb_uplink.c` |

### 1.2 模块清单

| 文件 | 职责 | 使用外设 |
|------|------|---------|
| `main.c` | 系统入口、初始化编排、软定时器注册 | — |
| `system_hc32f4a0pgtb.c` | PLL/FCG/Flash wait 配置，SysTick（DCU1+Timer0_1_A） | DCU1, TMR0_1 |
| `io.c` | 全局 GPIO 配置，提供 485 方向脚回调实现 | GPIO |
| `soft_timer.c` | 软件定时器（轮询）驱动 | （依赖 `sys_get_tick()`） |
| `msg.c` | 轻量消息队列 | — |
| `uart.c` | UART 环形缓冲抽象层 | — |
| `debug.c` | 调试串口（USART3），实现 `__msl_printf` 输出 | USART3 |
| `mix_std_lib.c` | 自实现 memcpy/memcmp/printf（不依赖 libc） | — |
| `crc16_modbus.c` | Modbus CRC16 表查找法 | — |
| `doraemon_pack.c` | 字节序转换工具（大/小端互转） | — |
| `exio_595.c` | 74HC595 软件 SPI 驱动（8 路 GPIO 扩展） | GPIO PB7/8/9 |
| `led_statu_ctrl.c` | LED 状态管理（亮/灭/闪烁，100 ms 刷新） | 依赖 exio_595 |
| `wbus.c` | WBUS/SBUS 遥控接收（USART2，100 kbps，8E2，反相） | USART2 |
| `imu5115.c` | IMU5115 惯性测量单元驱动（USART1，115200，8N1） | USART1 |
| `moto.c` | 双轮驱动电机控制，Modbus RTU（USART6，RS485） | USART6, TMR0_2_A |
| `servo.c` | FT 舵机控制，FT 协议（USART7，RS485） | USART7, TMR0_2_B |
| `comm_485_1.c` | 升降机控制，自定义帧（USART5，RS485） | USART5 |
| `manual_ctrl.c` | RC 通道 → 运动学解算 → 电机速度指令 | （依赖 wbus/moto） |
| `battery.c` | 电池电压 ADC 采样（ADC1，通道 0，PA0） | ADC1 |
| `usb_bsp.c` | USB 硬件初始化（USBHS，内置 PHY） | USBHS |
| `usb_dev_cdc_class.c` | USB CDC VCP 类驱动（基于 HC32 DDL） | USBHS |
| `usb_dev_desc.c` | USB 描述符（VID=0x7676，PID=0x2302） | — |
| `usb_dev_user.c` | USB 用户回调（枚举事件） | — |
| `usb_uplink.c` | USB CDC 上行/下行协议解析与数据上报 | USBHS |

### 1.3 数据流图（ASCII）

```
┌──────────────┐  SBUS帧 100kbps 8E2   ┌───────────────┐
│  RC遥控(WBUS)│ ─────────────────────▶│  wbus.c       │
└──────────────┘   USART2 反相         │  ch_dat[10]   │
                                        └──────┬────────┘
                                               │ wbus_getch()
                                               ▼
                                       ┌───────────────┐
                                       │ manual_ctrl.c │
                                       │ 运动学解算    │
                                       └──────┬────────┘
                                              │ moto_cmd_set_all_speed()
                              ┌───────────────▼──────────────┐
                              │  moto.c  Modbus RTU  USART6  │
                              │  RS485半双工  115200          │
                              └──────────────────────────────┘
                                              │
                              ┌───────────────▼──────────────┐
                              │  servo.c  FT协议  USART7     │
                              │  RS485半双工  115200          │
                              └──────────────────────────────┘
                                              │
                              ┌───────────────▼──────────────┐
                              │  comm_485_1.c 升降机 USART5  │
                              │  RS485半双工  115200          │
                              └──────────────────────────────┘

┌─────────────┐  115200 8N1 USART1   ┌───────────────────────┐
│  IMU5115    │ ─────────────────────▶│  imu5115.c  imu_dat   │
└─────────────┘                       └──────────┬────────────┘
                                                 │ imu_get_dat()
                                                 ▼
┌──────────────────────────────────────────────────────────────┐
│                        usb_uplink.c                          │
│  upload()      → UPLOAD_REP   (l/r speed, bat, servo, lift) │
│  upload_imu()  → UPLOAD_IMU_REP (gx,gy,gz,ax,ay,az)         │
└──────────┬───────────────────────────────────────────────────┘
           │ usb_dev_cdc_send()                ▲
           │                                    │ usb_dev_cdc_on_recv()
           ▼                                    │
    ┌──────────────────────────────────────────────────┐
    │          USB HS CDC  (PB13/14/15)                │
    └──────────────────────────────────────────────────┘
           │                                    │
           ▼                                    │
    上位机/doraemon (上行)         下位机命令分发 (下行):
                                   SET_MOTO / SET_SERVO_BY_ID
                                   LIFT_SET_POS / LIFT_CALIBRATE 等

┌─────────────┐  ADC1 CH0 PA0   ┌───────────────────────────┐
│  电池分压   │ ────────────────▶│ battery.c  IRQ122         │
└─────────────┘                  │ 625/1024*11 mV 换算       │
                                 └──────────────┬────────────┘
                                                │ battery_get_adc_value()
                                                ▼
                                         usb_uplink → UPLOAD_REP[bat]
```

---

## 2. 引脚映射表

> 数据直接来自 `src/io.c`，PCR 寄存器各位含义参见下方注解。

### 2.1 PCR 寄存器位语义（来自 debug.c 注释，通用于所有 GPIO）

| 位 | 功能 | 说明 |
|----|------|------|
| b15 | DDIS（数字功能禁止） | 0=数字模式有效；1=数字关闭（模拟模式） |
| b14 | 输出锁存 | 0=关闭；1=有效 |
| b12 | 外部中断许可 | 0=禁止；1=有效 |
| b10 | 输入类型 | 0=施密特触发；1=CMOS 输入 |
| b9 | 反相 | 0=禁止；1=输入/输出取反 |
| b8 | 输入数据（只读） | 同 PIDRx |
| b6 | 内部上拉 | 0=无；1=有内部上拉 |
| b5~b4 | 驱动能力 | 00=低驱动；01=中驱动；10,11=高驱动 |
| b2 | 开漏输出 | 0=正常推挽；1=开漏 |
| b1 | 输出使能（POERx） | 0=禁止；1=使能 |
| b0 | 输出数据（PODRx） | 0=低电平；1=高电平 |

**常见 PCR 值解析：**

| PCR 值 | 含义 |
|--------|------|
| `0x0050` | b6=1(上拉), b4=1(中驱动) → GPIO 输出，中驱动，有上拉 |
| `0x0440` | b10=1(CMOS输入), b6=1(上拉) → GPIO 输入，CMOS 类型，上拉 |
| `0x0444` | b10=1(CMOS输入), b6=1(上拉), b2=1(开漏) → 开漏输入，带上拉 |
| `0x8000` | b15=1(数字关闭) → 模拟模式（用于 ADC/USB D±） |
| `0x0000` | 全默认：施密特输入，无上拉，低驱动 |

### 2.2 引脚映射总表

| 功能模块 | 信号 | 引脚 | PFSR 值 | PCR 值 | 方向 | 备注 |
|---------|------|------|---------|--------|------|------|
| **USART3（调试）** | TX | PE15 | 32 | 0x0050 | OUT | 中驱动，上拉 |
| **USART3（调试）** | RX | PE14 | 33 | 0x0440 | IN | CMOS 输入，上拉 |
| **USART1（IMU5115）** | TX | PD10 | 32 | 0x0050 | OUT | 中驱动，上拉 |
| **USART1（IMU5115）** | RX | PD11 | 33 | 0x0440 | IN | CMOS 输入，上拉 |
| **USART2（WBUS/SBUS）** | TX | PD9 | 34 | 0x0050 | OUT | 中驱动，上拉 |
| **USART2（WBUS/SBUS）** | RX | PD8 | 35 | 0x0440 | IN | CMOS 输入，上拉 |
| **USART6（电机 RS485）** | TX | PD7 | 36 | 0x0050 | OUT | Modbus RTU |
| **USART6（电机 RS485）** | RX | PD4 | 37 | 0x0444 | IN | 开漏，上拉 |
| **USART6（电机 RS485）** | RE（接收使能，低有效） | PD5 | — | 0x0050 | OUT | PORRD |= 0x0060（默认 RX） |
| **USART6（电机 RS485）** | DE/TE（发送使能，高有效） | PD6 | — | 0x0050 | OUT | 同 RE |
| **USART7（舵机 RS485）** | TX | PE5 | 38 | 0x0050 | OUT | FT 协议 |
| **USART7（舵机 RS485）** | RX | PE2 | 39 | 0x0444 | IN | 开漏，上拉 |
| **USART7（舵机 RS485）** | RE（低有效） | PE3 | — | 0x0050 | OUT | PORRE |= 0x0018（默认 RX） |
| **USART7（舵机 RS485）** | DE/TE（高有效） | PE4 | — | 0x0050 | OUT | 同 RE |
| **USART5（升降机 RS485）** | TX | PA11 | 34 | 0x0050 | OUT | 自定义帧 |
| **USART5（升降机 RS485）** | RX | PA8 | 35 | 0x0444 | IN | 开漏，上拉 |
| **USART5（升降机 RS485）** | RE（低有效） | PA9 | — | 0x0050 | OUT | PORRA |= (3<<9)（默认 RX） |
| **USART5（升降机 RS485）** | DE/TE（高有效） | PA10 | — | 0x0050 | OUT | 同 RE |
| **LED 595 (SCK)** | 移位时钟 | PB9 | — | 0x0050 | OUT | 软件 SPI，中驱动 |
| **LED 595 (RCK)** | 锁存时钟 | PB8 | — | 0x0050 | OUT | 软件 SPI |
| **LED 595 (DAT)** | 串行数据 | PB7 | — | 0x0050 | OUT | 软件 SPI，MSB 先 |
| **USB HS VBUS** | VBUS 检测 | PB13 | 12 | 0x0000 | IN | 复用功能 12，数字模式 |
| **USB HS D−** | USB 差分负 | PB14 | — | 0x8000 | ANALOG | b15=1，模拟模式 |
| **USB HS D+** | USB 差分正 | PB15 | — | 0x8000 | ANALOG | b15=1，模拟模式 |
| **ADC1 CH0** | 电池电压分压 | PA0 | — | 0x8000 | ANALOG | b15=1，模拟模式 |
| **测试引脚** | 示波器探针（wbus_poll 标记） | PB5 | — | 0x0050 | OUT | 备用 |

### 2.3 JTAG/SWD 全局特殊引脚寄存器

```c
// src/io.c
CM_GPIO->PSPCR = 0x0003;
// b4: NJTRST   = 0 → 禁用
// b3: JTDI     = 0 → 禁用
// b2: JTDO/SWO = 0 → 禁用
// b1: JTMS/SWDIO = 1 → 启用  ← SWD 调试接口
// b0: JTCK/SWCLK  = 1 → 启用  ← SWD 调试接口
// 结论：仅保留 SWD，JTAG 完整接口关闭
```

### 2.4 全局 GPIO 配置

```c
// src/io.c
CM_GPIO->PCCR &= 0x8fff;
CM_GPIO->PCCR |= 0x4000;
// RDWT[14:12] = 4 → GPIO 读等待时间档位 4：适用于 200~250 MHz

CM_GPIO->PINAER |= 0x0008;
// bit3 = 1 → 端口 D 输入常开（允许 PD8/PD9 等 USART2 RX 在低功耗时也能接收）
```

### 2.5 RS485 方向控制引脚操作（io.c 实现）

```c
// 电机 485（USART6）
void moto_485_set_tx(void) { CM_GPIO->POSRD |= 0x0060; } // PD5/PD6 置高 → TX 模式
void moto_485_set_rx(void) { CM_GPIO->PORRD |= 0x0060; } // PD5/PD6 置低 → RX 模式

// 舵机 485（USART7）
void servo_485_set_tx(void) { CM_GPIO->POSRE |= 0x0018; } // PE3/PE4 置高 → TX 模式
void servo_485_set_rx(void) { CM_GPIO->PORRE |= 0x0018; } // PE3/PE4 置低 → RX 模式

// 升降机 485（USART5）
void comm_485_1_set_tx(void) { CM_GPIO->POSRA |= 0x0600; } // PA9/PA10 置高 → TX 模式
void comm_485_1_set_rx(void) { CM_GPIO->PORRA |= 0x0600; } // PA9/PA10 置低 → RX 模式
```

> **注意**：RE 和 DE 共用两个 GPIO，均用 POSRD/PORRD 同时切换；RE 低有效，DE 高有效，因此两脚总是反相关系，此处同步设置是正确的。

---

## 3. 时钟/系统配置

### 3.1 PLL 配置（`system_hc32f4a0pgtb.c`）

```c
// 外部晶振 XTAL = 8 MHz
CM_CMU->XTALCFGR = 0xa0;
// b7=1(固定)，b5~b4=10 → 振荡器驱动：8~16 MHz 模式

CM_CMU->PLLHCFGR = 0x22205900;
// pllhm[1:0]  = 0x0 → 输入分频 /1   → VCO 输入 = 8 MHz
// pllhn[15:8] = 0x59 = 89 → 倍频系数 90 → VCO = 8×90 = 720 MHz
// pllhr[23:20]= 0x2 → 输出分频 /3  → PLLH/R = 720/3 = 240 MHz
// pllhq[27:24]= 0x2 → 输出分频 /3  → PLLH/Q = 240 MHz
// pllhp[31:28]= 0x2 → 输出分频 /3  → PLLH/P = 240 MHz
// bit7 = 0 → 时钟源：XTAL

CM_CMU->SCFGR = 0x00112210;
// hclk  [26:24]=0 → HCLK  = PLLH/1   = 240 MHz （CPU）
// pclk0 [2:0] =0 → PCLK0 = HCLK/1   = 240 MHz
// pclk1 [6:4] =1 → PCLK1 = HCLK/2   = 120 MHz （USART 时钟源）
// pclk2 [10:8]=2 → PCLK2 = HCLK/4   =  60 MHz
// pclk3 [14:12]=2→ PCLK3 = HCLK/4   =  60 MHz
// pclk4 [18:16]=1→ PCLK4 = HCLK/2   = 120 MHz
// exclk [22:20]=1→ EXCLK = HCLK/2   = 120 MHz

CM_CMU->USBCKCFGR = 0x40;
// bit[7:4] = 4 → USB 时钟 = SYSCLK/5 = 240/5 = 48 MHz ✓

CM_CMU->CKSWR = 0x05; // 切换系统时钟源为 PLLH
```

### 3.2 Flash Wait State 与 SRAM Wait

```c
CM_EFM->FRMC = 0x5;
// Flash 读等待：5 → 适用于 (200MHz, 240MHz]

CM_SRAMC->WTCR = 0x11111111;
// 各 SRAM 区域均设 1 个等待周期（240 MHz 下稳定）
```

### 3.3 FCG 时钟门控

```c
// 复位 FCG 后选择性开启外设时钟
CM_PWC->FCG0 = 0xfef52a0e;
// 按位 0 = 使能，具体外设包括：DCU1/2、TMR0_1/2、ADC1 等
// 其余外设（USART、USB）在各模块 hw_init() 中逐一开启
```

各模块按需使能：
```c
bCM_PWC->FCG3_b.USART1 = 0;  // IMU
bCM_PWC->FCG3_b.USART2 = 0;  // WBUS
bCM_PWC->FCG3_b.USART3 = 0;  // 调试
bCM_PWC->FCG3_b.USART5 = 0;  // comm_485_1
bCM_PWC->FCG3_b.USART6 = 0;  // 电机
bCM_PWC->FCG3_b.USART7 = 0;  // 舵机
bCM_PWC->FCG2_b.TMR0_1 = 0;  // SysTick
bCM_PWC->FCG2_b.TMR0_2 = 0;  // USART6/7 超时
bCM_PWC->FCG3_b.ADC1   = 0;  // 电池 ADC
bCM_PWC->FCG1_b.USBHS  = 0;  // USB HS
```

### 3.4 ICG 段（芯片初始化配置字）

```c
// system_hc32f4a0pgtb.c，放置在 .icg_sec 节
const uint32_t u32ICG[] __attribute__((section(".icg_sec"))) = {
    0xFFFFFFFFUL, // × 24 个字
    // 全 0xFF → 所有 ICG 选项保持默认/禁止
    // 包括：SWDT、HRC 配置、BOR、LVD、OTP 均为默认状态
};
// ICG 段地址由链接脚本固定在 flash 0x000001C0 处
```

### 3.5 SysTick 实现（Timer0_1_A + DCU1）

本工程**不使用 Cortex-M SysTick**，而是利用 HC32 的 DCU（数据运算单元）实现高精度 64 位毫秒计数器（100 µs 分辨率）。

```
PCLK1(120 MHz) → Timer0_1_A（分频/32 → 3.75 MHz，CMPAR=373）
                → 每 ≈ 100 µs 触发一次 AOS 事件
                → AOS.TMR0_TRGSEL → 触发 DCU1 加 1
                → DCU1.DATA0 溢出（0xFFFF→0）时触发 IRQ038
                → sys_tick_msb += 0x0000000100000000
```

```c
uint64_t sys_get_tick(void) {
    return sys_tick_msb | CM_DCU1->DATA0; // 单位：100 µs
}
void sys_delay_ms(uint32_t ms) {
    uint64_t stop = CM_DCU1->DATA0 + ms * 10; // 1 ms = 10 ticks
    while (CM_DCU1->DATA0 < stop) {}
}
```

> **Zephyr 对应**：`k_uptime_get()`（ms）或 `k_cycle_get_32()`（CPU 时钟周期）。

### 3.6 NVIC 优先级分组

```c
NVIC_SetPriorityGrouping(0); // 全部位用于抢占优先级，无子优先级
```

| 优先级常量 | 数值 | 使用者 |
|-----------|------|-------|
| `INT_PRIORITY_SYSTICK` | 0 | IRQ038（DCU1 SysTick，最高） |
| `INT_PRIORITY_IMU` | 1 | IRQ086/087（USART1 TI/RI） |
| `INT_PRIORITY_MANUAL` | 2 | IRQ088/089（USART2 TI/RI，WBUS） |
| `INT_PRIORITY_UART_OVERTIME` | 3 | IRQ102/106（USART6/7 RTO） |
| `INT_PRIORITY_UART` | 4 | IRQ092/093/098/099/100/101/104/105 |
| `INT_PRIORITY_USB` | 5 | IRQ030（USBHS） |
| `INT_PRIORITY_BAT_ADC` | 6 | IRQ122（ADC1 EOCA） |

---

## 4. 各模块业务逻辑

### 4.1 `main.c`

**职责**：系统初始化编排、软定时器注册、主循环驱动。

**公开 API**：无（入口函数 `main()`）。

**关键逻辑**：
- 上电后等待 3 秒 `countdown(3, 1000)` 让外设上电稳定
- 电机在等待期间初始化（`moto_init()` 包含阻塞式 Modbus 握手）
- LED 0 号在启动时以周期 5（500 ms）闪烁作为心跳指示

**线程安全**：所有模块 poll 函数均在主循环串行调用，无并发问题。但注意各 UART 中断回调直接写入环形缓冲，与 poll 函数共享，存在读写竞态（原代码未加锁）。

---

### 4.2 `debug.c`

**职责**：通过 USART3（PE14/PE15）输出调试字符串，实现 `__mslex_usart_write()` 挂接 `mix_std_lib` 的 `__msl_printf()`。

**公开 API**：
```c
void debug_init(void);
void debug_log(char *s);          // 发送字符串（阻塞等待 TX 缓冲区有空间）
void debug_log_hex(uint8_t *s, uint16_t len); // 发送二进制
char *debug_num2hex(uint64_t num); // 数值转十六进制字符串（静态缓冲，非线程安全）
```

**中断向量**：
- `IRQ092_Handler`（USART3 TI，SEL92=0x14E）：TX 空中断，推送 TX 缓冲区下一字节
- `IRQ093_Handler`（USART3 RI，SEL93=0x14D）：接收中断，存入 RX 环形缓冲

**USART3 配置**：115200 bps，8N1，无奇偶校验，16 倍过采样。

**缓冲区**：TX 256 B，RX 512 B（来自 `uart.h` 定义）。

**关键**：`debug_log()` 若 TX 缓冲区满会调用 `uart_suspend_handle()`（空函数，实际会原地自旋等待）。在高频调试输出时可能拖慢主循环。

---

### 4.3 `soft_timer.c`

**职责**：基于 `sys_get_tick()` 实现的软件定时器，支持单次/周期触发、动态启停。

**公开 API**：
```c
void stimer_init(void);
bool stimer_register(stimer_t *st);
void stimer_del(uint16_t id);
void stimer_ctrl(uint16_t id, bool enable, uint8_t pos);
void stimer_set_period(uint16_t id, uint32_t period);
void stimer_poll(void);  // 必须在主循环中调用
```

**数据结构**：
```c
typedef struct {
    uint16_t id;
    uint8_t  statu;         // ENABLE|PERIOD|LOCKED|WAITING 标志位
    uint8_t  statu_waiting; // 影子寄存器（中断期间修改安全）
    uint64_t old, now;      // 上次触发/当前时刻（100 µs 单位）
    uint32_t period;        // 定时周期（100 µs 单位）
    void    (*handle)(void);
} stimer_t;
```

**最大定时器数**：32（`STIMER_NUM_MAX`）。

**线程安全**：通过 `STIMER_STATU_POS_LOCKED` 位防止在 `handle()` 执行期间 `stimer_ctrl()` 直接修改状态（写入 `statu_waiting` 影子缓冲，`handle()` 返回后应用）。  
> ⚠️ 此实现仅对主循环内调用安全；若中断中调用 `stimer_ctrl()`，仍有竞态风险。

**关键状态机**：无独立状态机，`stimer_poll()` 直接遍历所有槽位，检查 `now - old >= period` 后触发。

---

### 4.4 `msg.c`

**职责**：轻量级单向消息队列，解耦事件产生与处理。

**公开 API**：
```c
bool msg_queue_push(uint32_t id, void *arg);   // 生产者，可在 ISR 中调用
msg_id_queue_t *msg_queue_pop(void);            // 消费者，内部使用
bool msg_func_register(MSG_ID_SIZE id, msg_exec_func_ptr func);
void msg_poll(void);  // 主循环调用，FIFO 消费所有待处理消息
```

**缓冲区**：队列深度 512（`MSG_ID_LIST_MAX`，必须为 2 的幂），索引用位掩码取模。

**关键细节**：`msg_poll()` 一次性消费队列中**所有**消息，非仅一条。若 `handle` 回调中再次 `push`，当次 `poll` 不会处理新消息（因为循环条件在入口处已取出所有项，但实际上有 `pop` 逻辑，新 push 的会在下一轮 `poll` 处理）。

> ⚠️ `msg_poll()` 中有 bug：循环末尾 `else { break; }` 依赖 `msg == NULL` 但 `msg` 可能在 id≥`MSG_FUNC_MAX` 时不为 NULL，导致提前退出。实际效果：消息 id 超出范围时停止处理。

---

### 4.5 `io.c`

**职责**：上电时一次性初始化所有 GPIO；提供 RS485 方向脚操作函数（供 moto/servo/comm_485_1 的 `__WEAK` 函数覆盖）。

**公开 API**：见第 2 章引脚表；RS485 方向函数详见 2.5 节。

**中断向量**：无。

**__WEAK 机制**：`moto.c`、`servo.c`、`comm_485_1.c` 各自声明：
```c
__WEAK void moto_485_set_tx(void);  // 弱符号
__WEAK void moto_485_set_rx(void);  // 弱符号
```
`io.c` 提供强符号实现，链接时覆盖弱符号，实现硬件无关解耦。

---

### 4.6 `exio_595.c`

**职责**：驱动 74HC595 移位寄存器，软件 SPI，MSB 先发，扩展 8 路 GPIO 输出（用于 LED）。

**公开 API**：
```c
void exio_595_send_byte(uint8_t dat); // 移入 8 位数据（仅 shift，不 latch）
void exio_595_output(void);           // 发出 RCK 脉冲，锁存输出
```

**时序**：
```
SCK 拉低 → [MSB 优先，逐位操作 DAT] → SCK 拉高(10µs) → SCK 拉低 → ...
→ RCK 拉高(10µs) → RCK 拉低 → 输出更新
```

**线程安全**：无保护，所有操作都在主循环中。

---

### 4.7 `led_statu_ctrl.c`

**职责**：管理 8 路 LED 的亮/灭/闪烁状态，每 100 ms 刷新一次（`LED_STATU_UPDATE_PERIOD=1000`，单位 100 µs）。

**公开 API**：
```c
void lsc_init(void);
void lsc_ctrl(uint8_t num, uint8_t statu);   // LED_STATU_ON=0, LED_STATU_OFF=1
void lsc_set_period(uint8_t num, uint32_t period); // 闪烁半周期（100 ms 为单位）
void lsc_update(void);   // 主循环调用
void lsc_flush(void);    // 立即刷新（不等 100 ms）
```

**LED 表**：`lsc_led_t led_table[8]`，每项含 `statu`/`count`/`period`。

> `period=0` 表示静态（不闪烁）；`period=N` 表示每 N×100ms 翻转一次。

---

### 4.8 `uart.c`

**职责**：UART 环形缓冲通用抽象层，通过函数指针实现多实例。

**数据结构**：
```c
typedef struct {
    volatile char  dat[UART_BUF_TX_MAX]; // TX 缓冲 256 B
    volatile uint32_t head, tail;
} uart_tx_buf_t;

typedef struct {
    volatile char  dat[UART_BUF_RX_MAX]; // RX 缓冲 512 B
    volatile uint32_t head, tail;
} uart_rx_buf_t;
```

**`uart_t` 函数指针**（初始化后由 `uart_init()` 填充）：

| 函数指针 | 说明 |
|---------|------|
| `buf_write_char(u, c)` | 写入 1 字节并触发 TX |
| `buf_write_s(u, str)` | 写入字符串并触发 TX |
| `buf_write_n(u, ptr, n)` | 写入 n 字节并触发 TX |
| `buf_read_c(u)` | 读取 1 字节（-1=空） |
| `start_tx()` | 使能 TX 空中断 |
| `hw_init()` | 硬件寄存器初始化 |
| `uart_suspend_handle()` | TX 缓冲区满时的等待函数 |

---

### 4.9 `wbus.c`

**职责**：接收 WBUS/SBUS 遥控帧，解析 10 路通道数据。

**USART2 配置**（100 kbps，8E2，反相起始位检测）：
```c
CM_USART2->CR1 = 0x80000000; // bit31=SBS=1 → 起始位检测：下降沿模式（反相信号兼容）
CM_USART2->CR2 = 0x00000600; // bit[10:9]=11（固定），STOP=0（1 stop bit，后由 bCM_USART2->CR2_b.STOP=1 设为 2）
CM_USART2->CR3 = 0x00000000;
CM_USART2->BRR = 0x00004aff; // baud=100000
// 然后：PCE=1（偶校验），STOP=2stop bits，TE+RE+RIE 使能
```

**帧格式**：25 字节，以 `0x0F` 开头，以 `0x00` 结束：
```
[0x0F][ch1_L][ch1_H...][...16通道位字段...][flags][0x00]
```

**通道解包**（11 位/通道，线性打包）：
```c
// 示例：ch[0] 位于 dat[1] 低8位 + dat[2] 低3位
channel[0] = ((w_buf.dat[2] & 0x07) << 8) | w_buf.dat[1];
// 范围 0~2047，中点 1024；归一化：(ch - 1024) * 100 / 671 → ±100%
ch_dat[i] = (int16_t)channel[i];
ch_dat[i] = (ch_dat[i] - 1024) * 100 / 671; // ch_min=351, ch_max=1697
```

**公开 API**：
```c
void  wbus_init(void);
void  wbus_poll(void);        // 主循环调用，解析接收缓冲
int16_t wbus_getch(int8_t ch); // ch=1..10，返回 -100~+100；无效返回 0xFFFF
uint8_t wbus_get_statu(void); // NORMAL/NO_SIGNAL/NO_DEVICE
```

**状态机**：`START → GETTING(24字节) → END → START`。

**超时**：5000 个 tick（500 ms）无帧 → `WBUS_STATU_NO_DEVICE`，并清除 USART 错误标志。

**中断向量**：
- `IRQ088`（USART2 TI，SEL88=0x133）：发送空中断
- `IRQ089`（USART2 RI，SEL89=0x132）：接收中断

**线程安全**：`ch_dat[]` 由 `wbus_poll()` 写（主循环），`wbus_getch()` 由主循环读（`manual_ctrl_poll()`），无竞态；USART RX ISR 写 `r_buf`，`wbus_poll()` 读 `r_buf`，存在弱竞态但在 Cortex-M4 单核下通常安全（ISR 优先级 2）。

---

### 4.10 `imu5115.c`

**职责**：接收 IMU5115 惯性测量单元的 34 字节帧，解析角速度与加速度。

**USART1 配置**：115200 bps，8N1，CR1|=0x8000002C（SBS=1，TE，RE，RIE）。

**公开 API**：
```c
void imu_init(void);
void imu_poll(void);            // 主循环调用，状态机解析
void imu_get_dat(imu_dat_t *d); // 拷贝最新数据（非线程安全，建议关中断）
```

**数据结构**：
```c
typedef struct {
    uint32_t gx, gy, gz;  // 角速度（原始值，LSB 优先从 dat[3/7/11/15/19/23]）
    uint32_t ax, ay, az;  // 加速度
    uint64_t tstamp;       // 系统时戳（100 µs 单位）
} imu_dat_t;
```

**帧解析状态机**：`HEAD1(0xBD) → HEAD2(0xDB) → HEAD3(0x0A) → DATA(30字节) → VAL(XOR校验)`。

**超时处理**：1000 tick（100 ms）无帧则清除 USART1 错误标志（FE/PE/ORE）。

**中断向量**：
- `IRQ086`（USART1 TI，SEL86=0x12E）
- `IRQ087`（USART1 RI，SEL87=0x12D）

---

### 4.11 `moto.c`

**职责**：通过 RS485（USART6）以 Modbus RTU 协议控制双轮驱动电机控制器（从机 ID=0x01）。实现请求队列、超时重试、速度读取。

**USART6 配置**：115200 bps，8N1，半双工（DE/RE 由 `moto_485_set_tx/rx()` 切换）。

**关键宏**：
```c
#define MOTO_TASK_QUEUE_BUF_MAX 64
#define MOTO_TASK_QUEUE_MAX 5    // 命令队列深度
#define MOTO_RX_OVERTIME 150     // 超时（tick，即 15 ms）
```

**公开 API**：
```c
void  moto_init(void);                           // 含 Modbus 握手，阻塞
void  moto_poll(void);                           // 主循环调用
void  moto_cmd_set_all_speed(int16_t r, int16_t l); // 入队设速命令
void  moto_cmd_get_all_speed(void);              // 入队读速命令
void  moto_cmd_clr_err(void);                    // 入队清错命令
int16_t moto_get_l_speed(void);                  // 获取最后读到的左轮速
int16_t moto_get_r_speed(void);
```

**初始化 Modbus 序列**（阻塞等待 ACK）：
```
FC06 写 0x200D = 0x0003  (控制模式配置)
FC06 写 0x2080 = 0x0064  (参数 P 速度环 = 100)
FC06 写 0x2081 = 0x0064
FC06 写 0x2082 = 0x0064
FC06 写 0x2083 = 0x0064
FC06 写 0x200E = 0x0008  (清除错误)
```

**关键状态机**（`moto_poll()`）：
```
IDLE → CMD（发送命令，设发送时戳）→ RSP（等待 RTO 触发的应答帧）
     → 超时重试（最多 3 次）→ ERR（丢弃，复位 IDLE）
```

**RS485 半双工控制**：
- `start_tx()`：等待 TC=1（上次发送完成）→ `moto_485_set_tx()` → 使能 TXEIE
- `IRQ100_Handler`（TI）：TX 空时 → TXEIE=0 → 等待 TC=1 → `moto_485_set_rx()`

**RTO（接收超时）机制**：通过 TMR0_2_A（与 USART6 联动）检测帧间隔，触发 `IRQ102_Handler`，将已接收字节打包压入 `rbq`（3 深度 C_QUEUE）。

**中断向量**：
- `IRQ100`（USART6 TI，SEL100=0x173）
- `IRQ101`（USART6 RI，SEL101=0x172）
- `IRQ102`（USART6 RTO，SEL102=0x175）

---

### 4.12 `servo.c`

**职责**：通过 RS485（USART7）以 Feetech（FT）舵机协议控制 3 个舵机（ID=1/2/3）。

**USART7 配置**：115200 bps，8N1，半双工（DE/RE 由 `servo_485_set_tx/rx()` 切换）。

**舵机限位**（`servo_status` 数组）：
```c
servo[0] (ID=1): 摄像头舵机   pos_min=1500, pos_max=2700
servo[1] (ID=2): 消灭者舵机   pos_min=2047, pos_max=3118
servo[2] (ID=3): 消灭者舵机   pos_min=2047, pos_max=3118
```

**公开 API**：
```c
void  servo_init(void);
void  servo_poll(void);
void  servo_cmd_set_pos_by_id(uint8_t id, int16_t pos, uint16_t time, int16_t speed);
void  servo_cmd_get_all_pos(void);  // 逐个读取 3 个舵机位置
int16_t servo_get_pos_by_id(uint8_t id); // id=1..3
```

**队列深度**：10（`SERVO_TASK_QUEUE_MAX`），缓冲区 64 B/条目。

**状态机**：与 `moto.c` 完全相同的 IDLE→CMD→RSP→ERR 四状态机。

**中断向量**：
- `IRQ104`（USART7 TI，SEL104=0x188）：TX 空 → 发完后切 RX
- `IRQ105`（USART7 RI，SEL105=0x187）：接收
- `IRQ106`（USART7 RTO，SEL106=0x18A）：帧超时，打包入 `servo_rbq`（使用 TMR0_2_B）

---

### 4.13 `comm_485_1.c`

**职责**：通过 RS485（USART5）控制升降机（DEVICE_ID_LIFT=0x01），使用**自定义帧格式**（非标准 Modbus RTU）。

**USART5 配置**：115200 bps，8N1，半双工，**无 RTO 中断**（接收解析在 `_execute_rx_data()` 轮询中完成）。

**公开 API**：
```c
void     comm_485_1_init(void);
void     comm_485_1_poll(void);
void     comm_485_1_cmd_set_lift_pos(int32_t pos);  // 设置升降位置（0~max）
void     comm_485_1_cmd_get_lift_pos(void);
void     comm_485_1_cmd_calibrate(void);
void     comm_485_1_cmd_get_lift_max(void);
void     comm_485_1_cmd_go_zero(void);
uint32_t comm_485_1_get_lift_pos(void);
uint32_t comm_485_1_get_lift_calibrate_value(void);
```

**接收解析**：`_execute_rx_data()` 在 `comm_485_1_poll()` 内调用，轮询 RX 缓冲，实现 8 状态帧解析机（HEAD1→HEAD2→ID→CMD→LEN→DATA→CRCH→CRCL）。

**超时**：2000 tick（200 ms）无数据则重置解析状态机。

**队列深度**：10 条命令。

**中断向量**：
- `IRQ098`（USART5 TI，SEL98=0x16F）
- `IRQ099`（USART5 RI，SEL99=0x16E）

---

### 4.14 `manual_ctrl.c`

**职责**：将 RC 遥控通道映射为差速驱动电机速度指令，并进行运动学解算（差速转向模型）。

**轮询周期**：每 1000 tick（100 ms）执行一次。

**通道映射**：
```c
ch1 → 转向（角速度）
ch2 → 前进/后退（线速度）
ch5 → 使能开关（<0 则停止，不驱动）
ch6 → 速度档位（>50: 高速; <-50: 低速; 中间: 中速）
```

**速度档位**：

| ch6 | 线速度上限 | 角速度上限 |
|-----|-----------|-----------|
| >50 | 2.5 m/s（高速） | 3.0 rad/s |
| 中间 | 1.0 m/s（中速） | 2.0 rad/s |
| <-50 | 0.5 m/s（低速） | 1.0 rad/s |

**运动学解算**（差速驱动）：
```c
// 机器人参数（robot_parament.h）
ROBOT_P_AXLE_LEN = 0.52f;   // 轴距 0.52 m
ROBOT_P_WHEEL_R  = 0.20f;   // 车轮半径 0.20 m
ROBOT_P_WHELL_MAX_SPEED = 200; // 最大转速 200 rad/min（约 3.33 rad/s）

l_speed_f = (v - ω * L/2) / R;  // 左轮角速度（rad/s）
r_speed_f = (v + ω * L/2) / R;  // 右轮角速度（rad/s）
// 转换到 rpm×(60/2π)：乘以 9.549724（= 60/(2π)）
l_speed_out = round(l_speed_f * 9.549724); // 单位：rad/min × (60/2π) = RPM
```

**安全保护**：WBUS 无信号或无设备时发送停车命令并清除电机错误，停车后用 `stop_check` 防止重复发送。

---

### 4.15 `battery.c`

**职责**：ADC1 通道 0（PA0）采集电池电压（经分压网络，分压比 1:11），中断回调换算为毫伏值。

**公开 API**：
```c
void     battery_init(void);
void     battery_adc_start(void);   // 软件触发 ADC 转换（轮询安全：先等 STR=0）
uint32_t battery_get_adc_value(void); // 返回电压值（mV，乘以 11 的分压比）
```

**换算公式**：
```c
battery_adc_value = CM_ADC1->DR0 * 625 / 1024 * 11;
// ADC 参考 3.3V，12 bit：DR0/4096 * 3.3V = DR0 * 625/1024 mV（实际精度按 10bit 计算）
// 分压 1:11 → 乘以 11 得到实际电压
```

**ADC 配置**：
```c
CM_ADC1->CR0 = 0x0600; // 12 位精度，序列 A 转换
CM_ADC1->CHSELRA = 0x00000001; // 通道 0（PA0）
CM_ADC1->AVCHSELR = 0x00000001; // 通道 0 使能平均采样
CM_ADC1->SSTR0 = 0x0B; // 采样时间
```

**中断向量**：`IRQ122`（ADC1 EOCA，SEL122=EVT_SRC_ADC1_EOCA，优先级 6）。

---

### 4.16 `usb_bsp.c`

**职责**：USB HS 硬件初始化（时钟已在 `sys_clk_init()` 配置），NVIC 配置。

**关键**：GPIO 配置已在 `io.c` 完成（PB13/14/15）；`usb_bsp_init()` 仅使能 FCG 时钟（`bCM_PWC->FCG1_b.USBHS=0`）。

**USBHS 选项**：内置 PHY，仅支持 FS（12 Mbps）。注释说明片内 PHY 只支持 FS/LS，外接 ULPI 才能达到 HS 480 Mbps。

**中断向量**：`IRQ030`（USBHS，SEL30=297，优先级 5）。

---

### 4.17 `usb_dev_cdc_class.c`

**职责**：HC32 DDL USB CDC VCP 类驱动，处理 USB 枚举、控制传输、数据收发。

**公开 API（对外层）**：
```c
void usb_dev_cdc_send(usb_core_instance *pdev, uint8_t *buf, uint32_t len);
void usb_dev_cdc_poll(void);  // 主循环调用，处理 TX ring buffer
void usb_dev_cdc_on_recv(const uint8_t *p, uint32_t n); // 由 ISR 调用，写入 rx 解析
```

**缓冲区**：`_usb_rx_buffer[MAX_CDC_PACKET_SIZE]`，内部 `usb_packet_buffer_t` 环形包缓冲（`USB_PACKET_RING_BUFFER_SIZE` 个包）。

**`usb_dev_cdc_poll()`**：检查 TX ring buffer，若有待发数据则调用底层发送。

---

### 4.18 `usb_dev_desc.c`

**职责**：USB 设备描述符定义。

| 字段 | 值 |
|------|---|
| VID | `0x7676` |
| PID | `0x2302` |
| 制造商 | "LeiLong" |
| 产品名 | "USBCAN" |
| 配置 | "USBCAN Config" |
| 语言 | 英语（0x0409） |

---

### 4.19 `usb_dev_user.c`

**职责**：USB 枚举事件回调（连接/断开/配置/挂起），当前实现均为空函数。

---

### 4.20 `usb_uplink.c`

**职责**：实现上位机↔MCU 的应用层协议（基于 USB CDC），上行数据周期性上报，下行解析命令分发。

**公开 API**：
```c
void usbul_init(void);      // 注册 MSG_ID_UPLOAD 和 MSG_ID_UPLOAD_IMU 回调
void usbul_poll(void);      // 主循环调用，处理接收命令队列（深度 5）
void usbul_send(const uint8_t *packet, uint32_t len);
void usbul_upload_lift_calibrate(void); // 由 comm_485_1 回调触发
```

**RX 解析**：`__execute_rx_data()` 为 8 状态机（0x39→0x93→CMD→LEN_H→LEN_L→DATA→CRC_H→CRC_L），解析完整包后压入 `rx_packet_queue`（深度 5，`C_QUEUE`）。

**缓冲区**：`usbul_rx_data[128]`，TX 临时缓冲 `usbul_tx_buffer[8+128]`。

---

### 4.21 `doraemon_pack.c`

**职责**：字节序转换工具库，用于协议打包/解包。

**公开 API**：
```c
uint32_t dp_u8_2_u32_lsb(uint8_t *dat); // 小端（LSB first）→ uint32
uint32_t dp_u8_2_u32_msb(uint8_t *dat); // 大端（MSB first）→ uint32
uint16_t dp_u8_2_u16_lsb(uint8_t *dat);
uint16_t dp_u8_2_u16_msb(uint8_t *dat);
// 类似函数：i32/i16/u64/i64 各大小端版本
```

**宏**（用于打包）：
```c
DP_UINT16_H(x)   // 取高字节
DP_UINT16_L(x)   // 取低字节
DP_UINT32_B3(x)  // 取最高字节（MSB）
DP_UINT32_B2(x)
DP_UINT32_B1(x)
DP_UINT32_B0(x)  // 取最低字节（LSB）
DP_GET_U32(x)    // 将 x 视为 uint32 读取（用于 int→uint 类型双关）
```

---

### 4.22 `crc16_modbus.c`

**职责**：标准 Modbus CRC16 计算（初值 0xFFFF，多项式 0xA001，256 项查表法）。

**公开 API**：
```c
unsigned short calc_modbus_crc16(const unsigned char *p, int n);
```

---

### 4.23 `mix_std_lib.c`

**职责**：自实现标准库替代品（不依赖 newlib），包含 printf/memcpy/memset/memcmp/strlen/strstr。

**公开 API**：
```c
void __msl_memset(void *mem, uint8_t value, uint32_t len);
void __msl_memclr(void *mem, uint32_t len);
void __msl_memcpy(void *dst, void *src, uint32_t len);
int8_t __msl_memcmp(void *mem1, void *mem2, uint32_t len);
uint32_t __msl_strlen(char *str);
char *__msl_strstr(char *str1, char *str2);
void __msl_printf(const char *fmt, ...); // 输出到 debug_log()（通过弱符号 __mslex_usart_write）
```

**`__msl_printf` 限制**：
- 最大输出缓冲 256 B（`__MSL_PRINTF_BUF_MAX`）
- 支持：`%d %u %x %X %o %s %c %p %n %%`；**不支持** `%f/%e/%g`（浮点，留有框架但未实现）
- 编译为 32 位模式（`__MSL_SYSTEM_BIT_32`），`%d` 按 `int32_t` 处理

---

## 5. 软定时器任务表

注册于 `main.c`，time 单位为 100 µs（`sys_get_tick()` 精度）：

| ID | 宏名 | 周期（tick） | 实际周期 | 回调函数 | 职责 |
|----|------|------------|---------|---------|------|
| 1 | `TIMER_TEST_ID` | 10000 | 1 s | `__moto_test()` | **禁用**（`stimer_ctrl` 未调用 enable），仅测试用 |
| 2 | `TIMER_READ_SPEED` | 200 | 20 ms | `__read_speed()` | 向电机发起读速度 Modbus 命令（入队） |
| 3 | `TIMER_UPLOAD` | 200 | 20 ms | `__upload()` | `msg_queue_push(MSG_ID_UPLOAD)` 触发状态上报 |
| 4 | `TIMER_UPLOAD_IMU` | 50 | 5 ms | `__upload_imu()` | `msg_queue_push(MSG_ID_UPLOAD_IMU)` 触发 IMU 上报 |
| 5 | `TIMER_BAT_ADC` | 10000 | 1 s | `__bat_adc()` | 触发电池 ADC 采样，调试输出升降机位置 |
| 6 | `TIMER_READ_SERVO_AND_LIFT` | 1000 | 100 ms | `__read_servo_and_lift()` | 向 3 个舵机及升降机发起位置读取命令 |

> **注**：`TIMER_TEST_ID` 注册后未调用 `stimer_ctrl(..., true, ...)` 使能，实际不会触发。

---

## 6. 协议帧格式

### 6.1 Modbus RTU（电机，moto.c）

**电气层**：USART6，115200 bps，8N1，RS485 半双工。

**从机 ID**：0x01（左右电机共享同一从机控制器）。

**通用帧格式**：
```
[ADDR][FC][DATA...][CRC_L][CRC_H]
```
CRC16 Modbus，小端（低字节在前）。

**常用功能码**：

| FC | 名称 | 请求格式 | 响应格式 |
|----|------|---------|---------|
| 0x03 | 读多个寄存器 | `01 03 [REG_H][REG_L][NUM_H][NUM_L][CRC]` | `01 03 [BYTES][DATA...][CRC]` |
| 0x06 | 写单个寄存器 | `01 06 [REG_H][REG_L][VAL_H][VAL_L][CRC]` | 回显请求帧 |
| 0x10 | 写多个寄存器 | `01 10 [REG_H][REG_L][NUM_H][NUM_L][BYTES][DATA...][CRC]` | `01 10 [REG_H][REG_L][NUM_H][NUM_L][CRC]` |

**关键寄存器映射**：

| 地址 | 方向 | 含义 |
|------|------|------|
| 0x200D | 写 | 控制模式，初始化写 0x0003 |
| 0x200E | 写 | 清除错误（0x0006=clr, 0x0008=clr） |
| 0x2080~0x2083 | 写 | PID 参数（各写 0x0064=100） |
| 0x2088~0x2089 | 写 | 速度设定（左轮 H/L，右轮 H/L，int16，rpm×(60/2π)） |
| 0x20AB~0x20AC | 读 | 实际速度反馈（左/右，int16） |

**设速命令示例**（左轮 100，右轮 -100）：
```
01 10 20 88 00 02 04 00 64 FF 9C [CRC_L CRC_H]
```

---

### 6.2 FT 舵机协议（servo.c）

**电气层**：USART7，115200 bps，8N1，RS485 半双工。

**帧格式**：
```
[0xFF][0xFF][ID][LEN][CMD][DATA...][CHK]
```
- `LEN`：CMD + DATA + CHK 的总字节数（`sizeof(DATA)+2`）
- `CHK`：`~((ID + LEN + CMD + Σ DATA) & 0xFF)`

**常用命令**：

| CMD | 名称 | DATA 内容 |
|-----|------|----------|
| 0x01 | PING | 无 |
| 0x02 | READ_DATA | `[addr][count]` |
| 0x03 | WRITE_DATA | `[addr][val...]` |

**位置设定命令**（WRITE_DATA，地址 0x2A）：
```
FF FF [ID] 0B 03 2A [pos_L pos_H] [time_L time_H] [speed_L speed_H] [CHK]
```
- pos：16 位有符号（摄像头 1500~2700，消灭者 2047~3118）
- time：运动时间（0=最大速度）
- speed：速度限制（0=无限制）

**位置读取命令**（READ_DATA，地址 0x38，count=2）：
```
FF FF [ID] 04 02 38 02 [CHK]
```
响应：
```
FF FF [ID] 04 00 [pos_L pos_H] [CHK]
```

**初始化 PING 序列**（servo_init 阻塞等待）：
```
→ ID=1 PING：期望响应 FF FF 01 02 00 FC
→ ID=2 PING：期望响应 FF FF 02 02 00 FB
→ ID=3 PING：期望响应 FF FF 03 02 00 FA
→ 全部设回 pos=2047（中点）
```

---

### 6.3 WBUS/SBUS 帧（wbus.c）

**电气层**：USART2，100 kbps，8E2（偶校验，2 停止位），**起始位下降沿检测**（CR1[31]=SBS=1，兼容反相信号）。

**帧结构**（25 字节）：
```
字节 0:  0x0F（帧头标识）
字节 1~22: 16 通道数据（11 位/通道，紧密位打包）
字节 23: 标志位（bit3=1 → NO_SIGNAL）
字节 24: 0x00（帧尾）
```

**通道解包算法**（以 ch0~ch9 为例，共 10 通道被使用）：
```c
channel[0] = ((dat[2] & 0x07) << 8) | dat[1];
channel[1] = ((dat[3] & 0x3F) << 5) | (dat[2] >> 3);
channel[2] = ((dat[5] & 0x01) << 10) | (dat[4] << 2) | (dat[3] >> 6);
// ... 依此类推（11 位紧密打包，跨字节）
```

**归一化**：原始值 351~1697，中点 1024 → `(raw - 1024) * 100 / 671` → 范围约 ±100。

**帧率**：14 ms/帧（约 71 Hz，SBUS 标准帧率）。

**通道含义**（manual_ctrl.c 中使用）：

| 通道 | 索引（1-based）| 用途 |
|------|--------------|------|
| ch1 | 1 | 转向（偏航角速度） |
| ch2 | 2 | 前进/后退（线速度） |
| ch5 | 5 | 使能开关 |
| ch6 | 6 | 速度档位（三档） |

---

### 6.4 IMU5115 帧格式

**电气层**：USART1，115200 bps，8N1，单工接收。

**帧结构**（34 字节）：
```
偏移  字节   内容
0     1      0xBD（帧头 1）
1     1      0xDB（帧头 2）
2     1      0x0A（帧头 3）
3     4      gx（uint32，小端 LSB first）
7     4      gy
11    4      gz
15    4      ax（uint32，小端）
19    4      ay
23    4      az
27    4      （未使用，填充）
31    2      帧序号（uint16，小端）
33    1      XOR 校验（XOR of bytes 0..32）
```

**校验**：`xor_sum(dat, 33) == dat[33]`。

**数据单位**：原始值（物理量换算需查 IMU5115 数据手册）。

---

### 6.5 升降机自定义帧（comm_485_1.c）

**电气层**：USART5，115200 bps，8N1，RS485 半双工，从机 ID=0x01。

**请求帧格式**（pack_data）：
```
字节 0-1: 0x39 0x93（帧头，固定）
字节 2:   ID（设备地址）
字节 3:   CMD（命令码）
字节 4:   LEN（DATA 字节数）
字节 5~5+LEN-1: DATA
字节 5+LEN: CRC_H（CRC16 Modbus 高字节，大端）
字节 6+LEN: CRC_L（CRC16 Modbus 低字节）
CRC 覆盖范围：字节 2 到 字节 4+LEN（即 ID+CMD+LEN+DATA）
```

**响应帧格式**（_execute_rx_data 解析）：
```
字节 0-1: 0x39 0x93
字节 2:   ID
字节 3:   CMD
字节 4:   LEN
字节 5~:  DATA[LEN]
最后 2 字节: CRC_H CRC_L（大端）
```

**命令列表**：

| CMD | 方向 | LEN | DATA | 响应 CMD | 说明 |
|-----|------|-----|------|---------|------|
| 0x08 | →MCU | 2 | `00 00` | — | 初始化查询 |
| 0x03 | →设备 | 4 | `pos[4]` MSB | 0x83 + `00` | 设置升降位置（int32，大端） |
| 0x05 | →设备 | 0 | — | `05` + 4字节 | 读取当前位置（uint32，大端） |
| 0x07 | →设备 | 0 | — | 0x87 + `00` | 零点标定 |
| 0x08 | →设备 | 0 | — | 0x88 + `00` | 回零点 |
| 0x09 | →设备 | 0 | — | `09` + 4字节 | 读取最大行程 |

---

### 6.6 USB CDC Uplink 协议（usb_uplink.c/h）

**传输层**：USB CDC VCP，Host 视为串口，无固定波特率。

**帧格式**（上行与下行通用）：
```
字节 0:    0x39（帧头 1）
字节 1:    0x93（帧头 2）
字节 2:    CMD（命令码，uint8）
字节 3:    LEN_H（数据长度高字节，uint8）
字节 4:    LEN_L（数据长度低字节，uint8）
字节 5~5+LEN-1: DATA（payload）
字节 5+LEN:    CRC_H（CRC16 Modbus of DATA，高字节）
字节 6+LEN:    CRC_L（低字节）
总长：LEN + 7 字节
```

**命令码列表**：

| 枚举值 | CMD | 方向 | DATA 长度 | 说明 |
|--------|-----|------|----------|------|
| `USBLINK_PACK_CMD_IDEL` | 0x00 | — | — | 空闲/错误 |
| `USBLINK_PACK_CMD_UPLOAD_REP` | 0x01 | MCU→Host | 40 B | 状态上报（含速度/电池/舵机/升降） |
| `USBLINK_PACK_CMD_UPLOAD_IMU_REP` | 0x02 | MCU→Host | 24 B | IMU 数据上报 |
| `USBLINK_PACK_CMD_SET_MOTO` | 0x03 | Host→MCU | 4 B | 设置电机速度 |
| `USBLINK_PACK_CMD_SET_MOTO_ACK` | 0x04 | MCU→Host | — | 未实现 |
| `USBLINK_PACK_CMD_SET_SERVO_BY_ID` | 0x05 | Host→MCU | 5 B | 设置舵机位置 |
| `USBLINK_PACK_CMD_CLR_MOTO_ERR` | 0x06 | Host→MCU | 0 B | 清除电机错误 |
| `USBLINK_PACK_CMD_LIFT_SET_POS` | 0x07 | Host→MCU | 4 B | 设置升降位置 |
| `USBLINK_PACK_CMD_LIFT_CALIBRATE` | 0x08 | Host→MCU | 0 B | 升降标定 |
| `USBLINK_PACK_CMD_LIFT_GET_MAX` | 0x09 | Host→MCU | 0 B | 请求读取最大行程 |
| `USBLINK_PACK_CMD_LIFT_GET_MAX_REP` | 0x0A | MCU→Host | 4 B | 最大行程响应 |
| `USBLINK_PACK_CMD_LIFT_GO_ZERO` | 0x0B | Host→MCU | 0 B | 升降回零 |

**UPLOAD_REP DATA 布局**（40 字节，小端 native 序）：
```
[0:1]   l_speed   int16   左轮速度（Modbus 读取值，RPM*转换系数）
[2:3]   r_speed   int16   右轮速度
[4:5]   bat_value uint16  电池电压（mV，已含 11× 分压换算）
[6:7]   servo_pos_1 int16 舵机 1 位置（ID=1，摄像头）
[8:9]   servo_pos_2 int16 舵机 2 位置
[10:11] servo_pos_3 int16 舵机 3 位置
[12:15] lift_pos  uint32  升降位置
[16:39] 未使用（零）
```

**UPLOAD_IMU_REP DATA 布局**（24 字节，小端 native 序）：
```
[0:3]   gx   uint32  角速度 X
[4:7]   gy   uint32  角速度 Y
[8:11]  gz   uint32  角速度 Z
[12:15] ax   uint32  加速度 X
[16:19] ay   uint32  加速度 Y
[20:23] az   uint32  加速度 Z
```

**SET_MOTO DATA**（4 字节，大端序，`dp_u8_2_i16_msb`）：
```
[0:1] ls  int16  左轮速度指令（MSB first）
[2:3] rs  int16  右轮速度指令（MSB first）
```

**SET_SERVO_BY_ID DATA**（5 字节）：
```
[0]   id       uint8   舵机 ID（1/2/3）
[1:2] pos      int16   目标位置（MSB first）
[3:4] speed    int16   速度限制（MSB first，0=无限制）
```

**LIFT_SET_POS DATA**（4 字节，大端）：
```
[0:3] pos  int32  升降目标位置（MSB first）
```

---

## 7. 中断映射表

| IRQ 号 | Handler 名 | SEL 寄存器 | 事件源（十六进制） | 所在文件 | 功能 | 优先级 |
|--------|-----------|-----------|-----------------|---------|------|-------|
| IRQ030 | `IRQ030_Handler` | SEL30=297（0x129） | USBHS 全局 | `usb_bsp.c` | USB HS 全局中断，调用 `usb_isr_handler()` | 5 |
| IRQ038 | `IRQ038_Handler` | SEL38=0x37（DCU1） | DCU1 溢出 | `system_hc32f4a0pgtb.c` | SysTick 上溢，`sys_tick_msb += 2^32` | 0（最高）|
| IRQ086 | `IRQ086_Handler` | SEL86=0x12E | USART1 TI | `imu5115.c` | IMU USART1 发送空中断，推送 TX 缓冲下一字节 | 1 |
| IRQ087 | `IRQ087_Handler` | SEL87=0x12D | USART1 RI | `imu5115.c` | IMU USART1 接收中断，存入 RX 环形缓冲 | 1 |
| IRQ088 | `IRQ088_Handler` | SEL88=0x133 | USART2 TI | `wbus.c` | WBUS USART2 发送空中断 | 2 |
| IRQ089 | `IRQ089_Handler` | SEL89=0x132 | USART2 RI | `wbus.c` | WBUS USART2 接收中断，存入 RX 环形缓冲 | 2 |
| IRQ092 | `IRQ092_Handler` | SEL92=0x14E | USART3 TI | `debug.c` | 调试串口 USART3 发送空中断 | 4 |
| IRQ093 | `IRQ093_Handler` | SEL93=0x14D | USART3 RI | `debug.c` | 调试串口 USART3 接收中断 | 4 |
| IRQ098 | `IRQ098_Handler` | SEL98=0x16F | USART5 TI | `comm_485_1.c` | 升降机 USART5 发送空中断，发完后切 RX 方向 | 4 |
| IRQ099 | `IRQ099_Handler` | SEL99=0x16E | USART5 RI | `comm_485_1.c` | 升降机 USART5 接收中断 | 4 |
| IRQ100 | `IRQ100_Handler` | SEL100=0x173 | USART6 TI | `moto.c` | 电机 USART6 发送空中断，发完后切 RX（`moto_485_set_rx()`） | 4 |
| IRQ101 | `IRQ101_Handler` | SEL101=0x172 | USART6 RI | `moto.c` | 电机 USART6 接收中断，存入 RX 缓冲 | 4 |
| IRQ102 | `IRQ102_Handler` | SEL102=0x175 | USART6 RTO | `moto.c` | 电机接收超时（TMR0_2_A 触发），将缓冲帧打包入 `rbq` | 3 |
| IRQ104 | `IRQ104_Handler` | SEL104=0x188 | USART7 TI | `servo.c` | 舵机 USART7 发送空中断，发完后切 RX（`servo_485_set_rx()`） | 4 |
| IRQ105 | `IRQ105_Handler` | SEL105=0x187 | USART7 RI | `servo.c` | 舵机 USART7 接收中断 | 4 |
| IRQ106 | `IRQ106_Handler` | SEL106=0x18A | USART7 RTO | `servo.c` | 舵机接收超时（TMR0_2_B 触发），打包入 `servo_rbq` | 3 |
| IRQ122 | `IRQ122_Handler` | SEL122=EVT_SRC_ADC1_EOCA | ADC1 序列 A 完成 | `battery.c` | ADC 转换完成，换算电压值 | 6 |

> **注**：USART 事件源 ID 即 `CM_INTC->SELxx` 的写入值（十六进制）。0x12D=301=USART1_RI，0x132=306=USART2_RI，依此类推，对应 HC32F4A0 参考手册中断事件列表。

---

## 8. Zephyr 迁移映射

### 8.1 模块对应 Zephyr 子系统

| 原始模块 | 原始机制 | Zephyr 替代 |
|---------|---------|------------|
| `soft_timer.c` | 主循环轮询 + `sys_get_tick()` | `k_timer` + `k_work_delayable`（或 `k_work_poll`）|
| `msg.c` 消息队列 | 自实现环形队列（512 深度） | `k_msgq`（定义：`K_MSGQ_DEFINE(name, size, count, align)`）|
| `uart.c` TX/RX 缓冲 | 手写环形缓冲 + TX 空中断 | `uart_rx_enable()` + `ring_buf` 或 `uart_irq_callback_set()` |
| `debug.c` 调试输出 | USART3 + 手写 printf | `printk()` / `LOG_INF()` + Zephyr UART 驱动 |
| `wbus.c` SBUS 接收 | USART2 RX ISR + 手写状态机 | Zephyr UART async API + 应用层状态机，或使用 sbus 库 |
| `imu5115.c` IMU | USART1 RX ISR + 状态机 | Zephyr UART IRQ 模式 + `k_fifo` 传递完整帧 |
| `moto.c`/`servo.c` RS485 | USART6/7 + RTO + 任务队列 | Zephyr UART async + `k_work` + RS485 GPIO DT 规范 |
| `comm_485_1.c` 升降机 | USART5 轮询 + 任务队列 | 同上 |
| `io.c` RS485 方向脚 | `__WEAK` + 直接寄存器操作 | `gpio_dt_spec` + `gpio_pin_set_dt()` 封装在 uart driver 配置中 |
| `exio_595.c` 软件 SPI | bit-bang GPIO | `gpio_dt_spec` 数组 + `gpio_pin_set_dt()` 或使用 `spi_transceive()` |
| `led_statu_ctrl.c` | 主循环 100ms 刷新 | `k_timer` + `k_work`，或直接用 Zephyr LED 驱动（`led.h`）|
| `battery.c` ADC | ADC1 序列 A 中断 | `adc_dt_spec` + `adc_read()` + `k_work_delayable` 定期触发 |
| `usb_uplink.c` USB CDC | HC32 DDL USB CDC 类 | Zephyr USB CDC-ACM：`uart_dev = device_get_binding("CDC_ACM_0")` |
| `system_hc32f4a0pgtb.c` 时钟 | 手写 PLL 配置 | Zephyr HC32 时钟驱动（需 port），`CONFIG_CLOCK_CONTROL` |
| `sys_get_tick()` 计时 | DCU1+TMR0_1 100µs 计数器 | `k_uptime_get()`（ms）或 `k_cycle_get_32()`（CPU 周期）|
| `sys_delay_ms()` 延时 | 忙等（polling DCU1） | `k_msleep()` / `k_usleep()` |
| `mix_std_lib.c` | 自实现 C 库 | Zephyr 内置 `string.h` + `printk()` |
| `crc16_modbus.c` | 查表 CRC16 | Zephyr `crc16_ansi()` / `crc16()` in `<zephyr/sys/crc.h>` |
| `doraemon_pack.c` | 字节序工具 | `sys_be16_to_cpu()` / `sys_le32_to_cpu()` in `<zephyr/sys/byteorder.h>` |
| NVIC 优先级 | `NVIC_SetPriority()` | `CONFIG_*_INIT_PRIORITY` DTS 中断优先级属性 |
| ICG 配置字 | `.icg_sec` 节 | HC32 port 通过 DTS `option_bytes` 或 flash 布局节点 |

### 8.2 RS485 半双工 Zephyr 方案

```c
// DTS 中声明 DE/RE GPIO
&usart6 {
    de-gpios = <&gpiod 6 GPIO_ACTIVE_HIGH>; // DE
    re-gpios = <&gpiod 5 GPIO_ACTIVE_LOW>;  // RE
    // 或使用 rs485-rts-active-high = ...
};

// 代码中
const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(usart6));
// Zephyr 的 uart_drv_cmd(uart, UART_CMD_HALF_DUPLEX_SET_TX/RX, ...) 或
// 手动在 uart_tx() 完成回调（UART_TX_DONE 事件）中切换 GPIO 方向
```

### 8.3 消息队列迁移示例

```c
// 原代码
msg_queue_push(MSG_ID_UPLOAD, NULL);
// msg_poll() 中调用 upload(NULL)

// Zephyr 替代：定义工作项
K_WORK_DEFINE(upload_work, upload_handler);
// 触发：
k_work_submit(&upload_work);
```

### 8.4 软定时器迁移示例

```c
// 原代码：stimer_register + stimer_ctrl 每 20ms 触发 __upload
// Zephyr 替代：
static void upload_timer_cb(struct k_timer *t) {
    k_work_submit(&upload_work);
}
K_TIMER_DEFINE(upload_timer, upload_timer_cb, NULL);
k_timer_start(&upload_timer, K_MSEC(20), K_MSEC(20));
```

---

## 9. 重写时的关键注意点

### 9.1 RS485 半双工：TX 完成后才能释放 DE

**原始机制**：在 `IRQ100_Handler`（USART6 TI）中：
```c
void IRQ100_Handler(void) {
    if (moto_uart.t_buf->head == moto_uart.t_buf->tail) { // TX 缓冲区空
        bCM_USART6->CR1_b.TXEIE = 0;   // 关闭 TX 空中断
        while (bCM_USART6->SR_b.TC == 0) {} // ⚠️ 阻塞等待 TC（移位寄存器排空）
        moto_485_set_rx();               // TC=1 后再切 RX → 防止最后字节被截断
    }
    // ...
}
```

**Zephyr 注意点**：
- 必须在 `UART_TX_DONE` 事件（异步 API）或 TC 中断后才能拉低 DE
- 不能在 TXE 空时立即切换（最后一字节还在移位寄存器中）
- Zephyr UART async 的 `uart_tx()` 完成回调 `UART_TX_DONE` 对应 HC32 的 TC 标志
- 若 UART 驱动不自动处理 RS485，需手动 hook `uart_irq_tx_complete` 或使用 `uart_set_rs485_flow_ctrl()`

### 9.2 WBUS 反相串口（HC32 USART CR1 SBS 位）

**原始机制**：HC32F4A0 USART 的 `CR1[31]=SBS=1` 将起始位检测模式改为"下降沿"，配合 WBUS（SBUS）物理反相信号（空闲=低，起始位=高）直接工作，**无需硬件反相器**。

**Zephyr 注意点**：
- 若 HC32 Zephyr UART 驱动未暴露 SBS 位，需自行 `sys_write32()` 操作寄存器
- STM32 USART 有类似的 `RXINV`/`TXINV` 位（CR2），可用 `uart_drv_cmd(UART_CMD_INVERT_RX, ...)`
- 备选方案：外加 SN74LVC1G04 反相器，UART 正常模式接收

### 9.3 环形缓冲与 Zephyr UART async 替代

**原始机制**：每个 UART 拥有独立的 TX（256 B）/ RX（512 B）环形缓冲，ISR 直接读写，主循环轮询消费。

**Zephyr 替代方案**：
```c
// 推荐：使用 Zephyr ring_buf + uart async
RING_BUF_DECLARE(rx_ring, 512);

// 在 UART 回调中
void uart_cb(const struct device *dev, struct uart_event *evt, void *ud) {
    switch (evt->type) {
    case UART_RX_RDY:
        ring_buf_put(&rx_ring, evt->data.rx.buf + evt->data.rx.offset, evt->data.rx.len);
        break;
    case UART_TX_DONE:
        // RS485: 切换 DE/RE
        gpio_pin_set_dt(&re_gpio, 1); // RE=1 → RX 模式
        break;
    }
}
```

**关键**：Zephyr UART async 的 `uart_rx_buf_req` 事件需要应用层提供新的 RX 缓冲区，务必实现双缓冲轮换。

### 9.4 `__WEAK` 方向脚回调 → Zephyr `gpio_dt_spec`

**原始机制**：
```c
// moto.c
__WEAK void moto_485_set_tx(void); // 弱符号声明
__WEAK void moto_485_set_rx(void);
// io.c 提供强符号，链接时覆盖
```

**Zephyr 替代**：使用 DTS + `gpio_dt_spec` 在驱动层统一管理，不再需要弱符号：
```c
static const struct gpio_dt_spec de_gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(moto_485), de_gpios);
static const struct gpio_dt_spec re_gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(moto_485), re_gpios);

// TX 模式：
gpio_pin_set_dt(&de_gpio, 1);
gpio_pin_set_dt(&re_gpio, 0);
// RX 模式：
gpio_pin_set_dt(&de_gpio, 0);
gpio_pin_set_dt(&re_gpio, 1);
```

### 9.5 RTO（接收超时）帧边界检测

**原始机制**：USART6/7 使用 HC32 的 RTO（Receive Timeout）功能联合 TMR0_2 实现帧间隔检测：一旦 RX 线静默超过一定字符时间，触发 RTO 中断，将接收缓冲区中的数据打包。

**Zephyr 注意点**：
- Zephyr STM32 UART 驱动支持 `uart_rx_enable(dev, buf, size, timeout_us)`，其中 `timeout_us` 即帧超时（类似 RTO）
- HC32 port 需确认 UART 驱动是否实现了 RTO 超时参数；若无，需手动配置 TMR0 并在超时中断中调用 `uart_rx_buf_rsp()`
- Modbus 协议要求帧间隔 ≥ 3.5 字符时间（在 115200 bps 下约 300 µs）

### 9.6 USB CDC 设备枚举

**原始机制**：基于 HC32 DDL 的 USB CDC 类，VID=0x7676，PID=0x2302。

**Zephyr 注意点**：
```c
// prj.conf
CONFIG_USB_DEVICE_STACK=y
CONFIG_USB_CDC_ACM=y
CONFIG_USB_DEVICE_VID=0x7676
CONFIG_USB_DEVICE_PID=0x2302
CONFIG_USB_DEVICE_MANUFACTURER="LeiLong"
CONFIG_USB_DEVICE_PRODUCT="USBCAN"

// app.c
const struct device *cdc_dev = device_get_binding("CDC_ACM_0");
usb_enable(NULL);
uart_irq_callback_set(cdc_dev, cdc_data_cb);
```

### 9.7 SysTick 精度

**原始**：100 µs（由 Timer0_1_A + DCU1 实现），`soft_timer` 最小周期 100 µs。

**Zephyr**：
- `k_uptime_get()` 精度 1 ms
- 若需 100 µs 精度：使用 `k_cycle_get_32()` + `sys_clock_hw_cycles_per_sec()` 换算
- 或配置 `CONFIG_SYS_CLOCK_TICKS_PER_SEC=10000`（100 µs/tick）

### 9.8 阻塞初始化序列

**原始问题**：`moto_init()` 包含多轮 Modbus 握手，每次等待最多 100 ms，总初始化时间约 600 ms。若任何握手失败，会调用 `__moto_error()` 进入死循环。

**Zephyr 重写建议**：
- 在专用初始化工作线程（`k_thread`）中执行，使用 `k_sem` 等待 ACK
- 超时失败记录错误码并上报，不进入死循环
- 使用 `k_msgq`（或 `k_fifo`）替代原始的 C_QUEUE（泛型宏实现的循环队列）

### 9.9 浮点运算注意事项

`manual_ctrl.c` 使用浮点运算进行运动学解算（`float` 类型，使用 FPU）。HC32F4A0 包含 Cortex-M4F 的单精度 FPU，初始化时已使能：
```c
SCB->CPACR |= ((3UL << 20) | (3UL << 22)); // system_hc32f4a0pgtb.c
```
Zephyr 默认在 Cortex-M4 上开启 FPU，但需确认 `CONFIG_FPU=y` 及 `CONFIG_FPU_SHARING=y`（多线程共享 FPU 时必须开启）。

### 9.10 C_QUEUE 泛型宏 → Zephyr k_fifo/k_msgq

原始代码使用宏生成的泛型循环队列（`C_QUEUE_DECLARE(type, size)`），在 Zephyr 中替换为：
- 固定大小消息：`k_msgq`（零拷贝，静态分配）
- 可变大小消息：`k_fifo` + 动态分配（或 `k_mem_slab`）
- moto/servo 的 RX 帧队列（深度 3）：`K_MSGQ_DEFINE(moto_rbq, sizeof(rx_frame_t), 3, 4)`

---

*文档生成时间：基于 FMR_CC_SW v1.2 源代码直接分析。所有行为描述均来自源码精确阅读，非推测。*
