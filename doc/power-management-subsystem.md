# Zephyr 低功耗子系统实现详解

Zephyr 的低功耗并不是一个单点功能，而是一整套由**内核 idle 路径、系统级 PM、设备级 PM、运行时 PM、SoC 低功耗入口实现**组成的机制集合。

本文结合当前工作区中的 Zephyr 源码、`nucleo_f411re` 构建产物、PM latency sample，以及 ST 的低功耗 blinky sample，说明：

- Zephyr 为什么能在系统空闲时自动进入低功耗状态
- `k_cpu_idle()`、idle 线程、PM policy 之间是什么关系
- system PM、device PM、runtime PM 各自解决什么问题
- STM32F4 在当前代码里如何进入 `PM_STATE_SUSPEND_TO_IDLE`
- 实际应用如何通过 sample 观察这些机制

---

## 1. 先建立低功耗的分层模型

Zephyr 的低功耗建议从四层去理解：

1. **CPU idle 层**：CPU 没事干时如何等待中断
2. **system PM 层**：系统空闲时该进入哪种功耗状态
3. **device PM 层**：系统切换功耗状态时设备如何 suspend/resume
4. **device runtime PM 层**：设备在运行期按需自动启停

如果只理解 `WFI` / `WFE`，你会看到“CPU 休眠”；
如果只理解 `CONFIG_PM_DEVICE_RUNTIME`，你会看到“某个外设时钟关掉”；
但只有把整条链连起来，才能真正理解 Zephyr 的低功耗子系统。

---

## 2. 当前工程相关文件

### 2.1 内核与 PM 核心

- `zephyr/kernel/idle.c`
- `zephyr/arch/arm/core/cortex_m/cpu_idle.c`
- `zephyr/subsys/pm/pm.c`
- `zephyr/subsys/pm/device.c`
- `zephyr/subsys/pm/device_runtime.c`

### 2.2 当前 STM32F4 SoC PM 实现

- `zephyr/soc/st/stm32/stm32f4x/power.c`

### 2.3 相关 sample

- `zephyr/samples/subsys/pm/latency/src/main.c`
- `zephyr/samples/subsys/pm/latency/src/pm.c`
- `zephyr/samples/boards/st/power_mgmt/blinky/src/main.c`
- `zephyr/samples/boards/st/power_mgmt/blinky/prj.conf`

### 2.4 当前构建设备树

- `f411re/build/zephyr/zephyr.dts`

---

## 3. 最底层：CPU idle 并不等于完整 PM

### 3.1 架构空闲实现

ARM Cortex-M 上与 CPU 空闲最相关的文件是：

- `zephyr/arch/arm/core/cortex_m/cpu_idle.c`

其中核心函数为：

- `arch_cpu_idle()`
- `arch_cpu_atomic_idle()`

这些函数会利用 Cortex-M 的低功耗等待指令，例如：

- `WFI`（Wait For Interrupt）
- 或某些场景下使用 `WFE`

### 3.2 这一步解决什么问题

它解决的是：

- **CPU 当前没有工作时，如何低成本等待下一次中断**

但这还不等于完整系统级低功耗，因为它未必会：

- 切换 SoC 电源状态
- 挂起外设
- 恢复时钟树
- 管理设备 suspend/resume

所以 CPU idle 是 PM 的基础，但不是全部。

---

## 4. idle 线程：系统为什么会“自动想睡觉”

### 4.1 关键文件

- `zephyr/kernel/idle.c`

这个文件实现了 Zephyr 的 idle 线程逻辑。

### 4.2 idle 线程在做什么

当系统里没有其他可运行线程时，调度器会切到 idle 线程。

在源码中可以看到，idle 路径会根据配置决定：

- 直接调用 `k_cpu_idle()`
- 或者调用 `pm_system_suspend(_kernel.idle)` 让 PM 框架参与决策

这点非常关键：

> Zephyr 的 system PM 一般不是“某个业务代码主动调用”，而是在系统进入 idle 时自动触发决策。

也就是说，低功耗往往不是应用在 while 循环里“手动睡”，而是内核在空闲窗口里自动做功耗优化。

---

## 5. system PM 核心：`pm.c`

### 5.1 关键文件

- `zephyr/subsys/pm/pm.c`

其中最重要的流程函数包括：

- `pm_system_suspend`
- `pm_system_resume`

### 5.2 它解决的核心问题

system PM 解决的是：

- 当系统进入空闲窗口时，应该选择哪个 PM state
- 进入该状态前后要做哪些系统级动作
- 恢复时如何通知各层

换句话说，`pm_system_suspend()` 相当于低功耗流程的总调度器之一。

### 5.3 典型流程

可抽象理解为：

1. idle 线程发现系统空闲
2. 调用 `pm_system_suspend()`
3. PM policy 决定一个候选状态，例如 `PM_STATE_RUNTIME_IDLE` 或 `PM_STATE_SUSPEND_TO_IDLE`
4. 执行相应设备/系统操作
5. 调用 SoC 的 `pm_state_set()` 进入低功耗
6. 被中断唤醒后恢复
7. 调用 `pm_system_resume()` 做恢复后处理

这就是 Zephyr system PM 的主线。

---

## 6. device PM：系统级挂起/恢复设备

### 6.1 关键文件

- `zephyr/subsys/pm/device.c`

这里的核心能力包括：

- `pm_device_action_run`
- `pm_device_driver_init`
- 设备 busy 标志管理
- wakeup capability 管理

### 6.2 它和 system PM 的关系

当系统决定进入某种功耗状态时，不能只让 CPU 休眠，还要考虑：

- 哪些设备要挂起
- 哪些设备必须保持唤醒能力
- 哪些设备当前正忙，不能贸然断电/关时钟

这部分逻辑由 device PM 来支撑。

### 6.3 为什么设备 busy 标志很重要

如果某设备正在：

- DMA 传输
- 正在发串口
- 正在执行 flash 操作

这时直接 suspend 设备，轻则丢数据，重则系统状态损坏。

因此 device PM 的存在，是为了保证“低功耗不是简单粗暴地一刀切”。

---

## 7. device runtime PM：按需启停设备

### 7.1 关键文件

- `zephyr/subsys/pm/device_runtime.c`

关键接口包括：

- `pm_device_runtime_get`
- `pm_device_runtime_put`
- `pm_device_runtime_put_async`
- runtime PM enable/disable 相关逻辑

### 7.2 它和 system PM 的区别

- **system PM**：整个系统空闲时，系统级进入低功耗
- **runtime PM**：系统还在正常运行，但某个设备暂时不用，就单独把它降功耗

举例来说：

- 系统仍在跑线程、串口打印还在工作
- 但某个 GPIO 端口、I2C 控制器、传感器暂时不用
- 那么 runtime PM 可以只管理这些设备，而不要求整个 SoC 进入更深睡眠

### 7.3 这层非常实用

因为在实际产品中，很多时候：

- 系统并没有完全 idle
- 但局部外设有很多节能空间

runtime PM 就是在解决这类“细粒度省电”问题。

---

## 8. STM32F4 上的 SoC 低功耗入口实现

### 8.1 关键文件

- `zephyr/soc/st/stm32/stm32f4x/power.c`

这是当前 STM32F4 SoC 系列最重要的低功耗实现文件。

### 8.2 实现了什么状态

从源码可以看出，该文件明确处理的主要状态是：

- `PM_STATE_SUSPEND_TO_IDLE`

这说明在当前 STM32F4 这条 Zephyr SoC PM 实现路径里，重点支持的是 suspend-to-idle 类低功耗模式。

### 8.3 进入低功耗时做了什么

从实现上可以看到几个关键动作：

- 进入 STM32 STOP low-power regulator 模式
- 调用 `k_cpu_idle()` 真正等待唤醒事件

也就是说，这一层把：

- Zephyr PM state
- STM32 具体低功耗模式
- CPU idle 指令路径

连接在了一起。

### 8.4 恢复时做了什么

唤醒后，代码里还会做：

- `stm32_clock_control_init(NULL)` 恢复时钟
- `irq_unlock(0)` 重新打开中断

这说明 SoC 低功耗实现不能只负责“睡下去”，还必须正确“醒过来”。

而恢复时钟树，恰恰是很多 STM32 低功耗调试里最容易忽视、也最容易出问题的部分。

---

## 9. `pm/latency` sample：理解 PM policy 与延迟约束

### 9.1 sample 文件

- `zephyr/samples/subsys/pm/latency/src/main.c`
- `zephyr/samples/subsys/pm/latency/src/pm.c`

### 9.2 这个 sample 的价值

它不是简单演示“睡一觉”，而是更深入地演示：

- PM policy latency constraint
- `pm_policy_latency_request_add/update/remove`
- 不同功耗状态在延迟约束下如何被允许或禁止

### 9.3 `pm.c` 在这里做什么

sample 自带的 `src/pm.c` 里实现了：

- `pm_state_set()`
- `pm_state_exit_post_ops()`

它在 `pm_state_set()` 中打印进入的 PM 状态，并调用：

- `k_cpu_idle()`

这相当于给 sample 提供了一个简化版 SoC PM 入口，用来演示 Zephyr PM policy 框架如何工作。

### 9.4 这个 sample 带来的理解

它告诉我们：

- Zephyr 不是“固定进入某种省电模式”
- 系统可以根据延迟要求决定：
  - 只进入 `RUNTIME_IDLE`
  - 还是可以进入 `SUSPEND_TO_IDLE`
  - 甚至更深状态

所以 PM 不只是“有没有休眠”，更是“选哪种休眠”。

---

## 10. ST 的低功耗 blinky sample：runtime PM 的实际味道

### 10.1 sample 文件

- `zephyr/samples/boards/st/power_mgmt/blinky/src/main.c`
- `zephyr/samples/boards/st/power_mgmt/blinky/prj.conf`

### 10.2 这个 sample 开启了什么配置

从 `prj.conf` 可以看到：

- `CONFIG_PM=y`
- `CONFIG_PM_DEVICE=y`
- `CONFIG_PM_DEVICE_RUNTIME=y`
- `CONFIG_PM_DEVICE_SYSTEM_MANAGED=y`
- `CONFIG_GPIO=y`

这说明它同时打开了：

- system PM
- device PM
- runtime PM

### 10.3 sample 的关键做法

该 sample 做了一个很有代表性的动作：

- 对每个 STM32 GPIO 端口调用 `pm_device_runtime_enable(DEVICE_DT_GET(node_id))`

然后在 LED 开关过程中：

- 需要使用 GPIO 时重新配置
- 不使用时把引脚 `GPIO_DISCONNECTED`

这背后的含义是：

- GPIO 设备在不用时可以释放资源/时钟
- runtime PM 可以降低外设空闲功耗

这比“单纯让 CPU 睡眠”更贴近真实产品节能策略。

### 10.4 为什么这个 sample 很适合配合当前主题阅读

因为它把下面几件事都连起来了：

- GPIO 子系统
- runtime PM
- 设备树遍历
- STM32 板级低功耗实践

这正说明 Zephyr 的低功耗不是孤立子系统，而是和驱动模型紧密耦合的。

---

## 11. 当前工程里 PM 是否已经启用

这里要特别区分：

- **Zephyr 代码库支持什么**
- **当前 `f411re/blinky` 这个 sample 实际启用了什么**

当前用户应用配置文件：

- `f411re/blinky/prj.conf`

只显式包含：

- `CONFIG_GPIO=y`

也就是说，当前这个 sample 本身并没有显式开启完整 PM 功能。

因此本文更多是在说明：

- 当前平台上的 PM 机制如何实现
- 若开启相关配置，它会如何工作
- 官方 sample 如何使用它

这点非常重要，否则容易误以为“当前 blinky 已经自动演示了所有低功耗路径”。实际上并没有，blinky 还是很朴素的，就是负责闪灯，不负责哲学性打盹。

---

## 12. 当前构建设备树中与低功耗相关的线索

在当前 `f411re/build/zephyr/zephyr.dts` 中，可以看到一些与低功耗/唤醒相关的重要线索，例如：

- `zephyr,system-timer-companion = &rtc`
- `wkup-gpios = < &gpioa 0x0 0x1 >`

这些线索表明：

- 平台时间源与低功耗场景可能存在 RTC 等辅助角色
- SoC/板级描述里已经包含唤醒相关硬件能力描述

也就是说，设备树不仅决定“有哪些外设”，还会影响系统在低功耗和唤醒路径上的能力边界。

---

## 13. 从 idle 到唤醒的完整逻辑链

把前面的代码串起来，当前 Zephyr 低功耗主链可以概括为：

1. 系统没有可运行工作线程
2. 调度器切到 idle 线程
3. `kernel/idle.c` 决定是否走 PM 路径
4. 若启用 PM，则调用 `pm_system_suspend()`
5. PM policy 选择候选状态
6. device PM 参与设备状态切换
7. SoC `pm_state_set()` 执行具体低功耗入口
8. `k_cpu_idle()` / `WFI` 等待中断
9. 外设或唤醒源触发中断
10. 系统返回，恢复时钟/中断/设备状态
11. `pm_system_resume()` 完成恢复后的逻辑
12. 线程继续执行

这条链把：

- 调度器
- PM policy
- 设备管理
- 架构 idle
- SoC 电源控制
- 唤醒中断

全部连在了一起。

---

## 14. 调试低功耗问题时建议优先看哪里

如果你发现系统“根本不进低功耗”或“睡下去醒不来”，建议按下面顺序排查：

1. `f411re/blinky/prj.conf` 或目标 sample `prj.conf`
   - 先确认 `CONFIG_PM`、`CONFIG_PM_DEVICE`、`CONFIG_PM_DEVICE_RUNTIME` 是否真的启用
2. `zephyr/kernel/idle.c`
   - 看 idle 是否真正走到 `pm_system_suspend()`
3. `zephyr/subsys/pm/pm.c`
   - 看 PM policy 是否允许进入目标状态
4. `zephyr/subsys/pm/device.c`
   - 看设备是否因为 busy/wakeup 等原因被阻止挂起
5. `zephyr/subsys/pm/device_runtime.c`
   - 看 runtime PM 引用计数是否正确
6. `zephyr/soc/st/stm32/stm32f4x/power.c`
   - 看 SoC 低功耗入口/恢复是否正确
7. `f411re/build/zephyr/zephyr.dts`
   - 看 RTC、wakeup GPIO、相关节点状态是否符合预期

STM32 平台上常见问题通常集中在：

- 时钟恢复不完整
- 某设备一直 busy
- 唤醒源未配置正确
- debug 配置导致低功耗效果不明显

---

## 15. 一个面向当前工程的 PM 事实表

| 层次 | 当前工程相关实现 |
| --- | --- |
| idle 线程入口 | `zephyr/kernel/idle.c` |
| ARM CPU idle | `zephyr/arch/arm/core/cortex_m/cpu_idle.c` |
| system PM 核心 | `zephyr/subsys/pm/pm.c` |
| device PM | `zephyr/subsys/pm/device.c` |
| runtime PM | `zephyr/subsys/pm/device_runtime.c` |
| STM32F4 SoC PM | `zephyr/soc/st/stm32/stm32f4x/power.c` |
| PM policy / latency 示例 | `samples/subsys/pm/latency` |
| ST 运行时 PM 示例 | `samples/boards/st/power_mgmt/blinky` |
| 当前用户 sample 实际显式配置 | `f411re/blinky/prj.conf` 仅 `CONFIG_GPIO=y` |

---

## 16. 小结

Zephyr 低功耗子系统的关键不在于某一个 API，而在于**分层协作**：

- `idle.c` 负责在系统空闲时触发低功耗机会
- `pm.c` 负责 system PM 的总调度与状态选择
- `device.c` 负责设备级 suspend/resume 管理
- `device_runtime.c` 负责设备按需自动启停
- `cpu_idle.c` 负责 CPU 级等待中断
- `soc/st/stm32/stm32f4x/power.c` 负责把 Zephyr PM 状态映射到 STM32F4 的真实低功耗模式

对当前 `nucleo_f411re` 平台来说，最值得重点掌握的是：

1. idle 线程才是 system PM 的典型入口
2. `PM_STATE_SUSPEND_TO_IDLE` 是当前 STM32F4 实现里最明确的一条 SoC 低功耗路径
3. runtime PM 与 system PM 不是替代关系，而是互补关系
4. 真正的工程低功耗往往需要把 GPIO、RTC、唤醒源、设备 busy 状态一起考虑

理解这几点后，再看 GPIO、中断、定时器与 PM 的关系，就会非常自然：

- GPIO 可能是唤醒源
- 定时器/RTC 可能是唤醒时间基准
- 中断是从低功耗恢复的关键入口
- 驱动模型决定设备能否优雅地 suspend / resume

这正是 Zephyr 低功耗子系统最核心的系统性价值。