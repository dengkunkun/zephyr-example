# Zephyr 启动到 `main()` 详解

本文结合当前工作区中的 `f411re/blinky`、`nucleo_f411re` 板级文件、Zephyr 官方文档以及内核/架构源码，梳理程序从**上电后第一条指令**到**用户 `main()` 函数**之间的完整路径。

重点回答以下问题：

- Cortex-M 上电后第一步到底执行什么？
- Reset Handler、异常向量表、VTOR 重定位分别在哪里实现？
- `.data` 拷贝、`.bss` 清零、FPU 初始化在哪里发生？
- Zephyr 的设备驱动和 `SYS_INIT()` 回调是如何分阶段初始化的？
- `main()` 为什么不是在复位上下文里直接调用，而是在内核线程环境里运行？
- 当前 `nucleo_f411re + blinky` 的设备树、Kconfig、构建产物如何映射到这条启动链？

---

## 1. 适用上下文

本文围绕当前工程上下文展开：

- 应用：`/home/firebot/zephyrproject/f411re/blinky`
- 板卡：`/home/firebot/zephyrproject/zephyr/boards/st/nucleo_f411re/nucleo_f411re.dts`
- 当前 sample 配置：`/home/firebot/zephyrproject/f411re/blinky/prj.conf`
- 当前构建生成设备树：`/home/firebot/zephyrproject/f411re/build/zephyr/zephyr.dts`

本次 sample 很简单，`prj.conf` 只显式启用了：

- `CONFIG_GPIO=y`

因此本文会把“Zephyr 通用启动机制”和“当前 sample 真正启用的内容”分开说明，避免把所有可选路径都写成“当前一定发生”。

---

## 2. 整体执行路径总览

对于当前 `ARM Cortex-M4 + Zephyr`，从复位到 `main()` 的主路径可以概括为：

1. CPU 从异常向量表读取**初始 MSP** 和 **Reset 向量**
2. 跳转到 `z_arm_reset` / `__start`
3. 设置堆栈、关闭中断、执行 SoC 早期 hook
4. 跳入 `z_prep_c()`，完成 C 运行时准备
5. 进行向量表重定位、`.bss` 清零、`.data` 拷贝、FPU/中断初始化
6. 跳入 `z_cstart()`，开始内核初始化
7. 执行 `EARLY` / `PRE_KERNEL_1` / `PRE_KERNEL_2` 初始化阶段
8. 创建主线程、idle 线程等内核线程
9. 切换到后台启动线程，执行 `POST_KERNEL` / `APPLICATION` 初始化阶段
10. 最终调用用户 `main()`

对应关键源码文件如下：

- 启动汇编：`zephyr/arch/arm/core/cortex_m/reset.S`
- 向量表：`zephyr/arch/arm/core/cortex_m/vector_table.S`
- C 准备阶段：`zephyr/arch/arm/core/cortex_m/prep_c.c`
- 内核启动主线：`zephyr/kernel/init.c`
- 当前用户程序：`f411re/blinky/src/main.c`

---

## 3. 第一个可见对象：异常向量表

### 3.1 向量表放在哪里

Cortex-M 上电后不会先“执行某个 C 函数”，而是先读取**异常向量表**。Zephyr 在 ARM Cortex-M 上的向量表定义位于：

- `zephyr/arch/arm/core/cortex_m/vector_table.S`

该文件中最关键的两项是：

1. 第 0 项：初始主堆栈指针（MSP）
2. 第 1 项：Reset Handler，即 `z_arm_reset`

在该文件中可以看到：

- 初始 SP 指向 `z_main_stack + CONFIG_MAIN_STACK_SIZE`
- Reset 向量指向 `z_arm_reset`
- 后续异常项包括 NMI、HardFault、SVC、PendSV、SysTick 等

这意味着：**CPU 复位后真正跳转的不是用户 `main()`，而是架构层的复位入口。**

### 3.2 当前板子的中断控制器来源

在当前构建后的设备树 `build/zephyr/zephyr.dts` 中可以看到：

- 根节点的 `interrupt-parent = <&nvic>`
- NVIC 节点：`interrupt-controller@e000e100`
- SysTick 节点：`timer@e000e010`

这表明当前板卡运行在标准 ARMv7-M 中断体系上：

- 内核异常和外设 IRQ 最终都由 NVIC 接管
- 系统时钟中断来自 SysTick

---

## 4. Reset Handler：从硬件复位到 C 世界入口

### 4.1 入口文件

Reset Handler 的主实现位于：

- `zephyr/arch/arm/core/cortex_m/reset.S`

该文件中的 `z_arm_reset` / `__start` 是真正的复位入口。

### 4.2 这个阶段做了什么

从源码看，复位入口会做几件关键事情：

1. 设置 MSP 到主栈顶
2. 在适用时执行 `soc_early_reset_hook()` 或 `soc_reset_hook()`
3. 关闭中断，避免系统尚未准备好时被打断
4. 设置 PSP（进程栈）到中断栈相关区域
5. 处理某些可选恢复路径，例如 PM S2RAM 恢复场景
6. 跳转到 `z_prep_c()`

这里有一个很重要的设计点：

- **Reset Handler 不直接初始化所有驱动**
- 它只负责把 CPU 从“复位后的裸硬件状态”带到“可安全进入 C 代码”的状态

也就是说，`reset.S` 更像“启动装配线的第一个工位”。

### 4.3 为什么这里要先关中断

因为此时还没有完成：

- `.data` 拷贝
- `.bss` 清零
- 中断向量重定位（若启用）
- SW ISR 表准备
- 驱动初始化

如果中断在这时进来，ISR 看到的系统状态可能还是半成品

---

## 5. `z_prep_c()`：真正进入 C 运行时准备阶段

### 5.1 实现位置

- `zephyr/arch/arm/core/cortex_m/prep_c.c`

该文件是从“汇编启动”迈向“C 语言初始化”的关键桥梁。

### 5.2 这里做的关键工作

从源码可以归纳出以下关键步骤。

#### 5.2.1 向量表重定位

`prep_c.c` 中的 `relocate_vector_table()` 负责根据配置决定是否要把向量表放到 SRAM，并更新 `SCB->VTOR`。

相关点：

- ROM 中的原始向量表来自 `vector_table.S`
- 若启用了 `CONFIG_SRAM_VECTOR_TABLE`，系统可能会把向量表复制到 RAM
- 重定位后，后续中断入口会从新的 VTOR 指向位置取表

这项设计的意义在于：

- ROM 向量表适合静态、只读场景
- RAM 向量表适合动态修改、重映射或某些低延迟/可更新场景

在调试中，如果你发现“我明明改了 ISR 表但中断没走到”，VTOR 是非常值得第一个确认的对象。

#### 5.2.2 `.bss` 清零

`arch_bss_zero()` 用来把未初始化全局/静态变量所在的 `.bss` 段清零。

如果这一步没做：

- `static int foo;` 不再保证从 0 开始
- 内核对象初始状态可能完全错误
- 后续调度、链表、超时队列都可能直接翻车

#### 5.2.3 `.data` 从 Flash 拷贝到 RAM

`arch_data_copy()` 负责把 `.data` 段从镜像加载地址复制到运行地址。

典型例子：

- `static int x = 123;`

它的初始值存放在 Flash 映像中，但程序运行时需要在 RAM 中读写，因此必须在启动时完成拷贝。

#### 5.2.4 FPU 与架构相关早期设置

`prep_c.c` 还会根据配置执行：

- FPU 初始化
- 中断控制器早期初始化
- 某些 SoC 自定义准备逻辑

对于 Cortex-M4F 这类带 FPU 的内核，这一步很关键，否则后续线程上下文切换和浮点指令使用都会出问题。

#### 5.2.5 中断子系统底层初始化

在 ARM Cortex-M 路径中，`z_prep_c()` 会调用：

- `z_arm_interrupt_init()`，或者在某些 SoC 场景下使用 `z_soc_irq_init()`

这一步不是“给所有设备注册业务回调”，而是把 NVIC、异常控制、基础 ISR 环境准备好。

### 5.3 `z_prep_c()` 的终点

完成上述准备后，`z_prep_c()` 会调用：

- `z_cstart()`

从这一刻开始，系统重心从“架构启动”切换到“内核初始化”。

---

## 6. `z_cstart()`：Zephyr 内核正式启动

### 6.1 实现位置

- `zephyr/kernel/init.c`

这是 Zephyr 启动主线最关键的 C 文件之一。

### 6.2 `z_cstart()` 的职责

从源码可以看到，`z_cstart()` 负责：

- 初始化内核基础对象
- 运行系统初始化级别（尤其是 `EARLY`、`PRE_KERNEL_1`、`PRE_KERNEL_2`）
- 准备主线程和 idle 线程
- 切入多线程环境
- 让后台启动线程继续执行剩余初始化并调用 `main()`

这里体现出 Zephyr 的一个核心设计：

> 用户 `main()` 不是“裸奔式入口函数”，而是一个**普通 Zephyr 线程上下文**中的函数。

这就是为什么在 `main()` 里可以直接使用：

- `k_msleep()`
- 信号量、队列、线程 API
- 大量已经初始化完毕的设备驱动

---

## 7. `SYS_INIT()` 与驱动初始化分层

### 7.1 初始化级别的设计

`kernel/init.c` 中的 `z_sys_init_run_level()` 负责执行不同 init level。

典型初始化级别包括：

- `EARLY`
- `PRE_KERNEL_1`
- `PRE_KERNEL_2`
- `POST_KERNEL`
- `APPLICATION`

这些级别的含义大致可以理解为：

- `EARLY`：极早期平台相关准备
- `PRE_KERNEL_*`：内核调度前、线程环境尚未完全就绪前的初始化
- `POST_KERNEL`：内核已基本起来，可以初始化更多依赖内核基础设施的驱动
- `APPLICATION`：应用层初始化钩子

### 7.2 为什么驱动不能一股脑都在 Reset Handler 里做

因为不同驱动依赖不同：

- 有的只依赖寄存器和时钟
- 有的依赖中断控制器已经准备好
- 有的依赖内核对象、工作队列、线程同步原语
- 有的甚至依赖其他设备先初始化完成

Zephyr 通过 `SYS_INIT()` 和设备模型把这些初始化拆层，有几个好处：

- 初始化顺序可控
- 驱动依赖关系清晰
- SoC、板级、内核、应用可以插入各自逻辑
- 不需要把所有平台差异硬编码在一个入口函数里

### 7.3 与设备模型的关系

驱动通常通过设备定义宏创建 `struct device`，并把初始化函数挂到对应 init level。

因此“驱动初始化”本质上不是一堆手写调用，而是：

- 编译期注册
- 链接期聚合
- 启动时按 level 执行

这也是 Zephyr 规模变大后仍能维持结构化启动流程的关键原因。

---

## 8. `main()` 为什么最后才执行

### 8.1 后台启动线程

`kernel/init.c` 中在准备好多线程环境后，会创建后台启动线程（常见入口是 `bg_thread_main()` 这条链）。

它会继续完成：

- `POST_KERNEL` 初始化
- `APPLICATION` 初始化
- 最终调用用户 `main()`

### 8.2 这意味着什么

当你的 `main()` 开始运行时，一般已经具备：

- 调度器可用
- idle 线程存在
- 大部分必须的内核对象已初始化
- 已启用的设备驱动已经按顺序初始化
- 常用内核 API 可正常使用

因此 `main()` 更像“第一个应用线程入口”，而不是“CPU 复位入口”。

---

## 9. 结合当前 `blinky` sample 看启动终点

当前用户代码位于：

- `f411re/blinky/src/main.c`

逻辑非常简洁：

1. 从设备树别名 `led0` 获取 LED GPIO 描述
2. 检查设备是否 ready
3. 配置引脚为输出
4. 循环翻转 LED
5. 调用 `k_msleep(1000)` 休眠

这个 sample 很适合说明一个事实：

- `main()` 运行时，GPIO 驱动已经完成初始化
- 设备树别名已经转换成编译期常量
- 调度器已经可用，所以 `k_msleep()` 可以直接使用

若没有前面整条启动链，这 5 行应用逻辑一行都跑不起来。

---

## 10. 当前板级设备树如何参与启动

### 10.1 板级 DTS

当前板子的 DTS 文件为：

- `zephyr/boards/st/nucleo_f411re/nucleo_f411re.dts`

其中可以看到：

- `chosen` 指定了 `zephyr,console = &usart2`
- `aliases` 指定了 `led0 = &green_led_2`
- `aliases` 指定了 `sw0 = &user_button`
- LED 实际连接在 `gpioa 5`
- 用户按键实际连接在 `gpioc 13`

这几个定义影响非常直接：

- 控制台初始化依赖 `chosen` 的 UART 选择
- `GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios)` 依赖 `aliases/led0`
- 中断和 GPIO 示例常用 `sw0`，依赖 `aliases/sw0`

### 10.2 构建后设备树是最终真相

虽然源 DTS 很重要，但真正建议调试时优先看：

- `f411re/build/zephyr/zephyr.dts`

因为它是：

- SoC `.dtsi`
- 板级 `.dts`
- overlay
- Kconfig 条件

综合展开后的最终结果。

当前生成设备树中已经可见：

- `nvic` 中断控制器
- `systick` 系统定时器
- `exti` 外部中断控制器
- `gpioa` / `gpioc`
- 多个 `st,stm32-timers`
- `chosen` 和 `aliases` 的最终值

这份文件是理解“为什么最终选中了这些硬件资源”的最佳入口之一。

---

## 11. Kconfig 在启动链中的作用

### 11.1 板级默认配置

当前板子的默认配置位于：

- `zephyr/boards/st/nucleo_f411re/nucleo_f411re_defconfig`

其中可见：

- `CONFIG_ARM_MPU=y`
- `CONFIG_HW_STACK_PROTECTION=y`
- `CONFIG_SERIAL=y`
- `CONFIG_CONSOLE=y`
- `CONFIG_UART_CONSOLE=y`
- `CONFIG_GPIO=y`

这些配置会影响：

- 启动早期是否配置 MPU
- 串口控制台是否初始化
- GPIO 子系统是否编译进系统

### 11.2 应用配置

当前 `f411re/blinky/prj.conf` 中只有：

- `CONFIG_GPIO=y`

因此当前应用层显式需求非常小，它大量依赖的是：

- 板级默认配置
- Zephyr 内核/架构默认配置
- 设备树板级描述

---

## 12. 中断向量表重定位与 SW ISR 表的关系

很多人会把“向量表”和“ISR 表”当成一回事，实际上 Zephyr 在 Cortex-M 上通常拆成两层：

1. **硬件向量表**：CPU 直接读取，决定先跳到哪里
2. **软件 ISR 表**：Zephyr 再根据 IRQ 号查找具体 ISR 和参数

在当前构建目录中已经生成了：

- `f411re/build/zephyr/isr_tables.c`

可以看到：

- `_irq_vector_table[]` 中很多项指向统一包装入口 `_isr_wrapper`
- `_sw_isr_table[]` 中保存真正的处理函数和参数

这说明在很多 Zephyr ARM 场景下：

- 硬件先跳到统一 wrapper
- wrapper 再根据 IRQ 号索引 `_sw_isr_table`
- 最终调用具体 ISR

因此：

- `vector_table.S` 解决“CPU 先跳到哪里”
- `isr_tables.c` / `_sw_isr_table` 解决“Zephyr 再转发到哪个具体处理函数”

这也是 Zephyr 能同时支持普通 ISR、动态 ISR、共享入口包装的关键基础。

---

## 13. 从调试视角看，建议按这几个断点追启动

如果你想亲自把启动链跑一遍，建议断点顺序如下：

1. `zephyr/arch/arm/core/cortex_m/reset.S` 中 `z_arm_reset`
2. `zephyr/arch/arm/core/cortex_m/prep_c.c` 中 `z_prep_c`
3. `zephyr/kernel/init.c` 中 `z_cstart`
4. `zephyr/kernel/init.c` 中 `z_sys_init_run_level`
5. `f411re/blinky/src/main.c` 中 `main`

配合观察以下对象，效果最好：

- `SCB->VTOR`
- `.bss` / `.data` 相关符号地址
- `build/zephyr/zephyr.dts`
- `build/zephyr/isr_tables.c`
- `build/zephyr/include/generated/zephyr/autoconf.h`
- `build/zephyr/include/generated/zephyr/devicetree_generated.h`

---

## 14. 一个贴近当前工程的“启动事实表”

针对当前 `nucleo_f411re + blinky`，可以把关键事实压缩成下面这张表：

| 阶段 | 关键实现 | 当前工程中的落点 |
| --- | --- | --- |
| 复位向量读取 | `vector_table.S` | Reset 向量指向 `z_arm_reset` |
| 复位入口 | `reset.S` | 设置栈、关中断、跳 `z_prep_c` |
| C 运行时准备 | `prep_c.c` | 向量表重定位、`.bss/.data`、中断/FPU 准备 |
| 内核启动 | `kernel/init.c` | 执行 init levels，创建线程 |
| 板级硬件描述 | `nucleo_f411re.dts` | `led0 -> PA5`，`sw0 -> PC13`，console `-> usart2` |
| 最终生效硬件视图 | `build/zephyr/zephyr.dts` | 包含 `nvic`、`systick`、`exti`、GPIO、timers |
| 用户程序入口 | `f411re/blinky/src/main.c` | GPIO ready / configure / toggle / `k_msleep()` |

---

## 15. 小结

从 Zephyr 的设计上看，`main()` 只是启动流程的最后一站，而不是第一站。

在当前 Cortex-M 平台上，这条链可以概括为：

- `vector_table.S` 决定复位跳转入口
- `reset.S` 负责把 CPU 带入可进入 C 的状态
- `prep_c.c` 负责建立 C 运行时和基础中断环境
- `kernel/init.c` 负责完成内核与驱动初始化，并在多线程环境中调用 `main()`
- 板级 DTS、Kconfig、构建生成文件共同决定“当前这块板子到底启用了什么”

理解这条路径之后，再看中断、GPIO、定时器、低功耗子系统时，就不会把它们误以为是“独立突然出现的模块”，而能清楚知道：

- 它们何时初始化
- 它们依赖什么前提
- 它们为什么能在 `main()` 中直接使用

这也是 Zephyr 启动设计最值得掌握的主线。


---

## 13. ARM 架构规定了什么，MCU 厂商又能改什么

这一节专门回答一个很常见、也很容易越想越绕的问题：

> Cortex-M 的异常/中断体系里，到底哪些是 ARM 架构写死的，哪些又是 MCU 厂商自己加的？

最不容易出错的理解方式，是把它分成 3 层：

1. **ARM 架构层**：定义 Cortex-M 的异常模型、向量表格式、NVIC 编程模型、优先级比较规则
2. **SoC/芯片实现层**：决定这颗 MCU 具体有多少个外设 IRQ、每个 IRQ 对应哪个外设、有没有 `EXTI` 这类前级模块
3. **板级/软件配置层**：决定当前工程启用了哪些外设、哪个 GPIO 被接成按键、给哪些 IRQ 配了什么优先级

### 13.1 向量表前几项与后续外设 IRQ：谁说了算？

可以先直接给结论：

- **向量表前面那一段核心异常项，主要由 ARM 架构规定**
- **后面的外部中断项（external interrupts），数量、编号含义、和外设的对应关系由 MCU 厂商/SoC 集成决定**

对于 Cortex-M，向量表开头这些槽位具有架构定义的含义：

1. 第 0 项：初始 MSP
2. 第 1 项：Reset
3. 后续若干项：NMI、HardFault、MemManage、BusFault、UsageFault、SVCall、PendSV、SysTick 等系统异常

也就是说，**CPU 硬件知道“第几号槽位代表哪类核心异常”**，这一点不是 STM32、NXP、Nordic 各写各的，而是 Cortex-M 异常模型的一部分。

但从“外部 IRQ 区域”开始，事情就进入了 SoC 世界：

- IRQ0 到底是 `WWDG`、`EXTI0`、`UART0` 还是别的什么
- 一共有 32 个、64 个还是上百个外设 IRQ
- 某些中断是“一外设一 IRQ”，还是多个事件复用到一个 IRQ

这些都不是 ARM 架构统一规定的，而是芯片厂商在 SoC 集成时决定的。

所以更准确的理解是：

- **ARM 规定“表的前半段是什么语义、CPU 如何取表、异常号如何解释”**
- **厂商规定“表的后半段有哪些外设中断槽，以及每个槽代表哪个硬件事件”**

在当前 `STM32F411` 语境下，像 `EXTI0`、`EXTI1`、`EXTI15_10`、`USART2`、`TIM3` 这些名字和编号关系，就是 STM32 这一代 SoC 自己定义的，不是所有 Cortex-M 都通用。

### 13.2 中断优先级是不是 MCU 厂商自行设计？

这个问题最合适的回答是：**不是“完全由厂商自行设计”，而是“ARM 规定规则，具体实现参数部分可变，软件再在这个框架里配置”。**

ARM 架构规定了优先级机制的骨架，包括：

- NVIC/系统异常优先级寄存器的编程模型
- **数值越小，优先级越高** 这一比较规则
- 抢占、挂起、尾链接（tail-chaining）等异常处理基本行为
- 某些核心异常的特殊地位，例如 `Reset`、`NMI`、`HardFault` 具有更强的架构优先级约束

但是，具体芯片并不是把这套机制“1:1 毫无差别”地全部展开。常见可变部分包括：

- **真正实现了多少个优先级 bit**
- 具体支持多少个外部 IRQ 输入
- 上电后的默认优先级初值

例如在很多 `STM32F4` 上，你会看到优先级字段虽然是 8 bit 宽度，但真正实现出来供软件使用的有效高位 bit 没有 8 个那么多，常见是 4 bit。也就是说：

- **优先级模型是 ARM 的**
- **实现了多少级、多少 bit，是具体 Cortex-M 实现/SoC 集成参数的一部分**

另外还要区分两件事：

1. **硬件能力边界**：这颗 MCU 实现了几位优先级
2. **软件运行时配置**：Zephyr/应用最终把某个 IRQ 配成什么优先级

因此说“中断优先级由 MCU 厂商自行设计”并不准确，更准确的说法是：

- ARM 规定优先级机制和比较规则
- MCU 厂商/集成商决定实现规模的一部分参数
- RTOS/应用在这个框架里设置实际优先级值

顺手补一个很容易踩坑的小点：

- `PendSV` 在很多 RTOS 里会被设得很低，这是 **OS 设计选择**，不是 ARM 架构硬编码它必须最低
- `NMI` / `HardFault` 这类核心异常则带有更强的架构约束，软件自由度明显小得多

### 13.3 NVIC 是 ARM 的，`EXTI` 这类前级模块是谁的？

这个问题可以比较明确地回答：**是的，NVIC 属于 Cortex-M 架构的一部分；而 `EXTI` 这类额外中断前端/汇聚模块通常是 MCU 厂商自行设计的 SoC 逻辑。**

先看 NVIC：

- NVIC（Nested Vectored Interrupt Controller）是 Cortex-M 内核侧的标准中断控制器
- CPU 的异常入口、优先级仲裁、抢占/挂起等核心机制最终都要经过 NVIC
- 因此在设备树里，根上的 `interrupt-parent = <&nvic>` 很合理：**最终送到 CPU 的根中断控制器就是它**

再看 `EXTI`：

- `EXTI` 不是 ARM 架构要求“所有 Cortex-M 都必须有”的通用组件
- 它是 STM32 SoC 里常见的一个**外部中断/事件线路控制模块**
- 它负责的通常不是最终 CPU 级别的异常仲裁，而是更前面那层事情，比如：
  - 哪条 GPIO/外部线连接到哪条 EXTI line
  - 上升沿/下降沿触发配置
  - mask/unmask
  - pending 位管理

然后，`EXTI` 再把结果送到一个或多个 NVIC IRQ 输入上，比如：

- `EXTI0`
- `EXTI1`
- `EXTI15_10`

所以它更像是：

- **NVIC 上游的厂商自定义中断前端/路由器/事件汇聚器**
- 而不是“另一个用来替代 NVIC 的根中断控制器”

这也是为什么在当前文档里会同时看到：

- Zephyr/设备树把 `nvic` 作为根中断控制器
- 生成设备树里又出现了 `exti` 这种 STM32 特有节点

两者并不冲突，它们分工不同。

### 13.4 放到当前 `nucleo_f411re + blinky` 里怎么理解

如果把上面的抽象结论落到当前工程，可以得到一个很实用的判断表：

| 层次 | ARM 架构规定 | STM32F411/板级自定义 |
| --- | --- | --- |
| 向量表前部 | 初始 MSP、Reset、NMI、HardFault、SVCall、PendSV、SysTick 等核心异常槽位语义 | 不能随意改成别的语义 |
| 外部 IRQ 区域 | “这里开始是 external interrupts” 这一机制 | `EXTI0`、`USART2`、`TIM3` 等具体 IRQ 编号和含义由 STM32 定义 |
| 根中断控制器 | NVIC 编程模型、异常优先级比较规则 | 具体实现多少优先级 bit、接入多少 IRQ 线由实现参数决定 |
| 前级事件/路由模块 | ARM 不要求必须有 `EXTI` | `EXTI` 是 STM32 自己的 SoC 模块 |
| 板级连线 | ARM 不关心 LED/按键接哪根脚 | 当前板上 `led0 -> PA5`，`sw0 -> PC13` |

对当前 `sw0` 按键来说，实际链路可以近似理解为：

1. 板级 DTS 里定义 `sw0 = &user_button`
2. `user_button` 实际连到 `PC13`
3. STM32 的 GPIO/EXTI 逻辑负责把这个外部引脚事件映射成 EXTI line
4. EXTI 再把事件送到相应 NVIC IRQ
5. NVIC 按 ARM 规定的异常机制把中断递送给 Cortex-M 内核

一句话总结就是：

> **ARM 规定“CPU 如何理解和处理异常”，STM32 规定“这颗芯片上到底有哪些中断源、怎么接进 NVIC”。**