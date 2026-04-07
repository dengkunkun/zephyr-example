# Zephyr 定时器子系统实现详解

Zephyr 里的“定时器”并不是单一概念。很多人第一次接触时，容易把下面三类东西混在一起：

1. **内核定时器对象**：`k_timer`
2. **系统时钟驱动**：提供 tick、中断、超时推进能力
3. **硬件定时器/计数器外设**：例如 STM32 TIM、counter 设备

本文的目标就是把这三层彻底拆开，再结合当前 `nucleo_f411re` 工程、Cortex-M SysTick、STM32 timer 设备树，以及具体 sample 说明它们如何协同工作。

---

## 1. 先建立一个最重要的心智模型

### 1.1 `k_timer` 不是“某个硬件定时器实例”

Zephyr 的 `k_timer` 是**内核对象**。

它依赖的不是某一个具体 TIM 外设，而是依赖：

- 内核 timeout 队列
- 系统时钟驱动持续推进时间
- 系统时钟中断在到期时触发超时处理

所以：

- 你调用 `k_timer_start()`，并不等于“占用了一个 STM32 TIMx”
- 你调用 `k_msleep()`，也不是“自动给你分配了一个单独硬件 timer”

### 1.2 硬件 timer 驱动是“底座”之一

Zephyr 要维持时间流动，必须有一个**系统时钟驱动**。对当前 Cortex-M 平台，它通常来自：

- `zephyr/drivers/timer/cortex_m_systick.c`

这个驱动负责：

- 周期性产生系统时钟中断，或在 tickless 模式下安排下一次唤醒
- 在中断中调用 `sys_clock_announce()`
- 由内核 timeout 子系统消费这些 tick，推进超时队列

### 1.3 `counter` 又是另一套外设驱动模型

Zephyr 还有单独的 counter 子系统，用于暴露具体硬件计数器/定时器外设。比如 sample：

- `zephyr/samples/drivers/counter/alarm/src/main.c`

它使用的不是 `k_timer`，而是 `counter_set_channel_alarm()` 这类硬件计数器 API。

因此理解定时器子系统时，必须分清：

- 内核定时器：`k_timer`
- 系统时钟：驱动内核时间基准
- 外设计数器：给应用直接使用的硬件定时器设备

---

## 2. 当前工程相关文件

### 2.1 内核时间/超时核心

- `zephyr/kernel/timer.c`
- `zephyr/kernel/timeout.c`

### 2.2 系统时钟驱动

- `zephyr/drivers/timer/cortex_m_systick.c`

### 2.3 设备树与当前构建

- `f411re/build/zephyr/zephyr.dts`

### 2.4 相关 sample

- `zephyr/samples/subsys/debug/debugmon/src/main.c`（使用 `K_TIMER_DEFINE`）
- `zephyr/samples/drivers/counter/alarm/src/main.c`（使用 counter alarm）

---

## 3. `k_timer` 的实现：`kernel/timer.c`

### 3.1 核心文件

- `zephyr/kernel/timer.c`

这个文件实现了 Zephyr 的内核定时器对象逻辑。

里面最关键的函数包括：

- `k_timer_init`
- `z_impl_k_timer_start`
- `z_impl_k_timer_stop`
- `z_timer_expiration_handler`
- `k_timer_status_sync`

### 3.2 `k_timer` 的基本运行方式

当你创建并启动一个 `k_timer` 时，Zephyr 实际上做的是：

1. 初始化 `struct k_timer`
2. 设定首次到期时间和周期
3. 把它挂入全局 timeout 队列
4. 等待系统时钟驱动推进时间
5. 到期后由超时处理逻辑执行 timer 的 expiry callback

这说明 `k_timer` 本质上是**timeout 子系统的一个上层对象封装**。

### 3.3 expiry callback 在哪里执行

Zephyr 官方文档强调，timer expiry 回调运行在：

- **system clock interrupt context**

也就是系统时钟中断上下文。

这意味着：

- 回调应尽量短小
- 不适合做长时间阻塞操作
- 若需要复杂处理，通常应投递 workqueue 或唤醒线程

这是使用 `k_timer` 时非常重要的一条规则。

---

## 4. timeout 队列：`kernel/timeout.c`

### 4.1 为什么 `timer.c` 还不够

如果只看 `timer.c`，你会知道“有个定时器对象”；
但如果想知道“时间到底是怎么被推进的”，必须看：

- `zephyr/kernel/timeout.c`

### 4.2 关键函数

源码中最关键的函数包括：

- `z_add_timeout`
- `z_abort_timeout`
- `sys_clock_announce`
- `sys_timepoint_calc`
- `sys_timepoint_timeout`

### 4.3 这套机制的本质

可以这样理解：

- 所有基于内核超时的对象（包括 `k_timer`、`k_sleep` 等）最终都会挂到 timeout 队列上
- 系统时钟驱动每次告诉内核“已经过去了多少 tick”
- 内核根据这些 tick 推进队列，找出到期对象并执行相应处理

所以 Zephyr 的时间管理不是“每个对象自己轮询时间”，而是：

- 一个统一的时间推进核心
- 多个内核对象共享这套机制

这样设计的优势很明显：

- 时间管理统一
- 资源占用低
- 可以支持 tickless 优化
- `k_timer`、`k_sleep`、超时等待等能力可以共享基础设施

---

## 5. 系统时钟驱动：`cortex_m_systick.c`

### 5.1 当前平台的系统时钟来源

在当前构建设备树 `f411re/build/zephyr/zephyr.dts` 中，可以看到：

- `systick: timer@e000e010`
- `compatible = "arm,armv7m-systick"`

这说明当前 Cortex-M 平台的系统时钟驱动基于：

- **SysTick**

### 5.2 驱动文件

- `zephyr/drivers/timer/cortex_m_systick.c`

其关键函数包括：

- `sys_clock_isr`
- `sys_clock_set_timeout`
- `sys_clock_elapsed`
- `sys_clock_idle_exit`
- `sys_clock_driver_init`

### 5.3 它做了什么

这个驱动负责：

1. 初始化 SysTick
2. 在每次时钟中断中进入 `sys_clock_isr`
3. 计算过去了多少 tick
4. 调用 `sys_clock_announce()` 通知内核
5. 在 tickless 模式下根据下一个超时点重新编排下一次唤醒时间

这说明系统时钟驱动和 `k_timer` 的关系是：

- `k_timer` 负责“描述一个定时需求”
- `cortex_m_systick.c` 负责“持续推动时间前进”

二者缺一不可。

---

## 6. 从系统时钟 IRQ 到 `k_timer` 回调的完整链路

对于当前 Cortex-M 架构，完整路径可以概括为：

1. SysTick 到期
2. CPU 进入 SysTick 异常入口
3. 最终执行 `sys_clock_isr`
4. `sys_clock_isr` 调用 `sys_clock_announce()`
5. `kernel/timeout.c` 推进 timeout 队列
6. 到期的 `k_timer` 触发 `z_timer_expiration_handler`
7. 执行用户定义的 timer expiry callback

这条链是理解 Zephyr 时间子系统的核心主线。

如果把它简化成一句话，就是：

> `k_timer` 只是“挂在 timeout 队列上的一个对象”，真正推动它到期的是系统时钟驱动。

---

## 7. `debugmon` sample：一个简单的 `k_timer` 示例

示例文件：

- `zephyr/samples/subsys/debug/debugmon/src/main.c`

其中可以看到：

- `K_TIMER_DEFINE(led_timer, timer_handler, NULL);`
- `k_timer_start(&led_timer, K_NO_WAIT, K_SECONDS(1));`

并且在 `timer_handler()` 里：

- `gpio_pin_toggle_dt(&led);`

### 7.1 这个 sample 说明了什么

它清楚展示了 `k_timer` 的常见使用范式：

1. 定义 timer 对象
2. 定义 expiry callback
3. 在 `main()` 中启动 timer
4. 后续由内核定时触发 callback

### 7.2 它没有直接碰硬件 timer

这个 sample 并没有：

- 选择某个 TIMx
- 配置计数寄存器
- 申请某个硬件通道

这再次说明：

- `k_timer` 是内核对象，不是硬件 timer 句柄

它依赖的是 Zephyr 时间内核与系统时钟驱动，而不是应用显式管理某个底层定时器外设。

---

## 8. `counter/alarm` sample：硬件计数器的另一条路径

示例文件：

- `zephyr/samples/drivers/counter/alarm/src/main.c`

这个 sample 使用的是 counter 驱动模型，而不是 `k_timer`。

其关键行为包括：

- 选择一个 counter 设备
- 调用 `counter_set_channel_alarm()`
- 在 alarm callback 中读取当前计数值
- 动态重新设置下一次 alarm

### 8.1 它和 `k_timer` 有什么不同

`counter/alarm` sample 处理的是：

- **硬件计数器设备**
- 与具体设备驱动绑定
- 可能有通道、计数宽度、捕获/比较等硬件属性

而 `k_timer` 处理的是：

- 内核抽象时间对象
- 与具体硬件 timer 设备解耦
- 由系统时钟统一驱动

### 8.2 为什么 Zephyr 两套都要有

因为两类需求不同：

- 想实现线程/内核对象级超时、定时唤醒：用 `k_timer`
- 想使用某个具体硬件计数器特性：用 `counter`

这也是很多 RTOS/驱动框架都会同时保留“内核定时器”和“外设定时器”的根本原因。

---

## 9. 当前 `nucleo_f411re` 上设备树中的 timer 资源

在当前 `build/zephyr/zephyr.dts` 中，可以看到多类 timer 相关节点：

- `systick: timer@e000e010`
- `timers1: timers@40010000`
- `timers2: timers@40000000`
- `timers3: timers@40000400`
- `timers4: timers@40000800`
- `timers5: timers@40000c00`
- `timers9` / `timers10` / `timers11`

这些 `st,stm32-timers` 节点下面还可派生出：

- `pwm`
- `counter`
- `qdec`

这说明在 STM32 平台上：

- 同一类硬件定时器外设可以被 Zephyr 映射到不同子系统功能
- 它既可能被当成 PWM 输出资源
- 也可能被当成 counter 设备
- 还可能被某些特定场景复用为其他功能

但要注意：

- **这些 TIMx 节点的存在，不等价于 `k_timer` 直接使用它们**

---

## 10. 当前构建中还有一个“系统定时伴随源”

在当前生成设备树 `f411re/build/zephyr/zephyr.dts` 的 `chosen` 节点中，还能看到：

- `zephyr,system-timer-companion = &rtc`

这说明当前平台构建中还存在系统时间相关的伴随时钟资源。它不改变本文的核心结论，但提醒我们：

- Zephyr 的时间体系不总是只有一个简单 SysTick
- 某些平台可能引入 RTC / LPTIM 等低功耗时间源作为辅助或替代机制
- 尤其在低功耗与 tickless 场景中，这类时间源会变得很重要

这也是为什么理解“内核时间抽象”和“底层时间源”要分层看，不能把它们混成一个概念。

---

## 11. Tick、Cycle、Timeout 的关系

Zephyr 官方 timing/clocks 文档对几个概念区分得很清楚：

- **cycle**：CPU 或硬件计数级别的底层时间单位
- **tick**：内核时间推进使用的基础单位
- **uptime**：系统运行时间
- **`k_timeout_t`**：内核对超时时间的抽象表达

这些概念在代码中对应的操作体现在：

- `k_msleep()` / `K_MSEC()`：高层时间表达
- `k_cycle_get_32()`：更底层的 cycle 计数读取
- `sys_clock_elapsed()`：系统时钟已过时间查询

理解这层区别有助于避免以下常见误区：

- 误把 cycle 当成 tick
- 误以为 `k_timer` 的精度总等于某个硬件定时器分辨率
- 误以为所有时间 API 都直接来自同一个硬件计数器

---

## 12. 驱动如何把硬件 timer 接入 Zephyr 时间体系

一个具体、典型的 IRQ 接入例子来自：

- `zephyr/drivers/timer/stm32_lptim_timer.c`

其中可以看到：

- `IRQ_CONNECT(DT_INST_IRQN(0), DT_INST_IRQ(0, priority), lptim_irq_handler, 0, 0);`
- `irq_enable(DT_INST_IRQN(0));`

这说明底层系统 timer/low-power timer 驱动通常会：

1. 从设备树读取 IRQ 号和优先级
2. 用 `IRQ_CONNECT()` 注册 ISR
3. 打开中断
4. 在 ISR 中调用时间核心接口，推进内核时间

因此，底层 timer 驱动的中断接入方式与其他驱动一样，也是走 Zephyr 标准中断注册框架，而不是“偷偷走私一条私有通道”。

---

## 13. 调试 Zephyr 定时器问题时建议分三层排查

如果你发现：

- `k_timer` 不回调
- `k_msleep()` 时间异常
- counter alarm 不触发

建议分层排查，而不是把所有“时间问题”一锅炖。

### 13.1 先查内核层

- `zephyr/kernel/timer.c`
- `zephyr/kernel/timeout.c`

看 timer 是否真正加入 timeout 队列、是否被推进。

### 13.2 再查系统时钟层

- `zephyr/drivers/timer/cortex_m_systick.c`

看 `sys_clock_isr` 是否进来、`sys_clock_announce()` 是否被调用。

### 13.3 最后查外设层

- `build/zephyr/zephyr.dts` 中 timer/counter 节点
- 具体 counter/timer 驱动

如果问题发生在 `counter/alarm` 这类 sample，更要优先检查外设驱动和设备树，而不是只盯着 `k_timer`。

---

## 14. 一个面向当前工程的“定时器事实表”

| 层次 | 当前工程相关实现 |
| --- | --- |
| 内核定时器对象 | `zephyr/kernel/timer.c` |
| 全局超时队列 | `zephyr/kernel/timeout.c` |
| 当前系统时钟驱动 | `zephyr/drivers/timer/cortex_m_systick.c` |
| 当前系统时钟设备树节点 | `systick: timer@e000e010` |
| 当前板上可见硬件 TIM 资源 | `timers1/2/3/4/5/9/10/11` |
| `k_timer` 示例 | `samples/subsys/debug/debugmon/src/main.c` |
| 硬件 counter 示例 | `samples/drivers/counter/alarm/src/main.c` |

---

## 15. 小结

Zephyr 的定时器子系统最关键的结论可以总结为三句话：

1. **`k_timer` 是内核对象，不是具体硬件 TIM 句柄**
2. **真正推动时间流逝的是系统时钟驱动，例如当前平台上的 SysTick 驱动**
3. **硬件 timer/counter 外设属于另一类设备驱动模型，与 `k_timer` 既有关联，又不是一回事**

对当前 `nucleo_f411re` 工程来说，最推荐的理解顺序是：

- 先读 `kernel/timer.c`，知道 `k_timer` 是什么
- 再读 `kernel/timeout.c`，知道它为什么会到期
- 再读 `cortex_m_systick.c`，知道谁在推进时间
- 最后对照 `counter/alarm` sample，理解“硬件计数器”和“内核定时器”是两条不同使用路径

把这几层分清后，Zephyr 的定时器系统就会从“概念打架”变成“分层清楚、职责明确”。