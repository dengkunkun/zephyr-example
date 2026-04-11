# HC32F460 启动流程对比：裸机 vs Zephyr

本文对比 HC32F460 裸机启动文件 `startup_hc32f460.S` 与 Zephyr 的 ARM Cortex-M 启动流程（`vector_table.S` + `reset.S` + `prep_c.c`），说明两者在向量表、堆栈初始化、数据段拷贝、BSS清零、中断管理等方面的差异。

---

## 1. 向量表（Vector Table）

### 裸机

```asm
; startup_hc32f460.S — .vectors section
__Vectors:
    .long   __StackTop              ; 第0个字：MSP初始值
    .long   Reset_Handler           ; 第1个字：复位向量
    .long   NMI_Handler             ; -14: NMI
    .long   HardFault_Handler       ; -13: HardFault
    .long   MemManage_Handler       ; -12: MemManage
    .long   BusFault_Handler        ; -11: BusFault
    .long   UsageFault_Handler      ; -10: UsageFault
    .long   0                       ;      Reserved ×4
    .long   0
    .long   0
    .long   0
    .long   SVC_Handler             ; -5:  SVCall
    .long   DebugMon_Handler        ; -4:  DebugMon
    .long   0                       ;      Reserved
    .long   PendSV_Handler          ; -2:  PendSV
    .long   SysTick_Handler         ; -1:  SysTick
    ; HC32F460 有 144 个外设中断
    .long   IRQ000_Handler
    .long   IRQ001_Handler
    ...
    .long   IRQ143_Handler
```

特点：
- MSP 地址在链接脚本中定义（`__StackTop`），8KB 栈空间
- 所有异常/中断 handler 弱定义，默认指向 `Default_Handler`（死循环 `b .`）
- 144 个外设中断向量，固定在 Flash 中
- HC32F460 通过 INTC SEL 寄存器动态映射中断源到这 144 个 IRQ 线

### Zephyr

```asm
; vector_table.S — exc_vector_table section
_vector_table:
    .word   z_main_stack + CONFIG_MAIN_STACK_SIZE    ; MSP → 主线程栈顶
    .word   z_arm_reset                               ; 复位向量
    .word   z_arm_nmi                                 ; NMI（Zephyr 异常处理）
    .word   z_arm_hard_fault                          ; HardFault（带调试信息）
    .word   z_arm_mpu_fault                           ; MemManage
    .word   z_arm_bus_fault                           ; BusFault
    .word   z_arm_usage_fault                         ; UsageFault
    ...
    .word   z_arm_svc                                 ; SVCall（系统调用）
    .word   z_arm_debug_monitor                       ; DebugMon
    .word   z_arm_pendsv                              ; PendSV（上下文切换）
    .word   sys_clock_isr                             ; SysTick（系统时钟）
```

特点：
- MSP 初始值指向 `z_main_stack` 栈顶，而**不是**中断栈（启动代码稍后切换到 PSP）
- 异常 handler 由 Zephyr 内核提供，包含完整的错误诊断信息
- 外设中断向量通过软件中断表（`_sw_isr_table`）实现，支持动态注册
- 可选：将向量表拷贝到 SRAM 以支持运行时修改（`CONFIG_SRAM_VECTOR_TABLE`）

**关键差异：** 裸机用单一栈，Zephyr 启动时先用 MSP（main stack），然后切换到 PSP（process stack），MSP 后续专用于中断处理——这是 RTOS 的标准做法。

---

## 2. 复位处理（Reset Handler）

### 裸机 Reset_Handler

```asm
Reset_Handler:
    ; 1) 清除 SRAM 控制器状态标志
    ldr     r0, =0x40050810     ; SRAMC_CKSR 寄存器
    ldr     r1, =0x1F
    str     r1, [r0]

    ; 2) 从 Flash 拷贝 .data 到 SRAM
    ldr     r1, =__etext        ; Flash 中 .data 的 LMA
    ldr     r2, =__data_start__ ; SRAM 中 .data 的 VMA
    ldr     r3, =__data_end__
CopyLoop:
    cmp     r2, r3
    ldrlt   r0, [r1], #4
    strlt   r0, [r2], #4
    blt     CopyLoop

    ; 3) 清零 .bss
    ldr     r1, =__bss_start__
    ldr     r2, =__bss_end__
    movs    r0, 0
ClearLoop:
    cmp     r1, r2
    strlt   r0, [r1], #4
    blt     ClearLoop

    ; 4) 配置 SRAM3 等待周期
    ;    (直接写硬编码的寄存器地址)
    ldr     r0, =0x40050804
    mov     r1, #0x77
    str     r1, [r0]
    ...

    ; 5) 调用 C++ 静态构造函数
    bl      __libc_init_array

    ; 6) 调用 SystemInit（配置 VTOR、FPU、时钟）
    bl      SystemInit

    ; 7) 跳转到 main
    bl      main
    bx      lr
```

流程简单直接：清SRAM状态 → 拷贝.data → 清零.bss → 配SRAM → C++构造 → SystemInit → main。

### Zephyr z_arm_reset (reset.S)

```asm
z_arm_reset:
__start:
    ; 1) [可选] 清除 CONTROL 寄存器（CONFIG_INIT_ARCH_HW_AT_BOOT）
    movs.n  r0, #0
    msr     CONTROL, r0
    isb

    ; 2) [可选] SoC 早期复位钩子（CONFIG_SOC_EARLY_RESET_HOOK）
    ;    在任何栈或RAM操作之前执行
    bl      soc_early_reset_hook

    ; 3) 设置 MSP 指向 z_main_stack 栈顶
    ldr     r0, =z_main_stack + CONFIG_MAIN_STACK_SIZE
    msr     msp, r0

    ; 4) [可选] 清除 z_sys_post_kernel 标志（CONFIG_DEBUG_THREAD_INFO）
    ;    用于 RTOS-aware 调试器识别内核状态

    ; 5) [可选] SoC 复位钩子（CONFIG_SOC_RESET_HOOK）
    bl      soc_reset_hook

    ; 6) [可选] 初始化架构硬件（CONFIG_INIT_ARCH_HW_AT_BOOT）
    ;    禁用 MPU，初始化核心架构寄存器
    bl      z_arm_init_arch_hw_at_boot

    ; 7) 锁定中断（通过 BASEPRI，不是 PRIMASK）
    movs.n  r0, #_EXC_IRQ_DEFAULT_PRIO
    msr     BASEPRI, r0
    ;    注意：使用 BASEPRI 而非 PRIMASK/CPSID 是为了允许高优先级
    ;    中断（优先级 0）仍能响应，类似于 NMI 的行为

    ; 8) [可选] 栈内存填充 0xAA（CONFIG_INIT_STACKS）
    ;    用于运行时检测栈溢出
    ldr     r0, =z_interrupt_stacks
    ldr     r1, =0xaa
    ldr     r2, =CONFIG_ISR_STACK_SIZE + MPU_GUARD_ALIGN_AND_SIZE
    bl      arch_early_memset

    ; 9) 切换到 PSP（进程栈指针）
    ldr     r0, =z_interrupt_stacks      ; ISR 栈
    adds    r0, r0, CONFIG_ISR_STACK_SIZE
    msr     PSP, r0
    mrs     r0, CONTROL
    orrs    r0, #2                        ; CONTROL.SPSEL = 1 → 使用 PSP
    msr     CONTROL, r0
    isb

    ; 10) 跳转到 z_prep_c（C 语言初始化）
    bl      z_prep_c
```

### Zephyr z_prep_c (prep_c.c)

```c
void z_prep_c(void) {
    soc_prep_hook();                // SoC 准备钩子
    relocate_vector_table();        // 设置 VTOR / 拷贝向量表到 SRAM
    z_arm_floating_point_init();    // 配置 FPU（CP10/CP11 访问权限）
    arch_bss_zero();                // 清零 .bss
    arch_data_copy();               // 拷贝 .data（XIP 场景）
    z_arm_interrupt_init();         // 初始化 NVIC
    z_cstart();                     // 启动内核 → 驱动初始化 → main
}
```

`z_cstart()` 内部会：
1. 初始化内核对象（调度器、定时器、内存等）
2. 调用 `soc_early_init_hook()`（我们在这里调 `LL_PERIPH_WE`）
3. 执行所有驱动初始化（PRE_KERNEL_1/2, POST_KERNEL）
4. 启动 main 线程

---

## 3. 数据段初始化（.data / .bss）

| 步骤 | 裸机 | Zephyr |
|------|------|--------|
| .data 拷贝 | 在 `Reset_Handler` 中，手写汇编循环 | `arch_data_copy()` 在 `z_prep_c()` 中调用，由链接脚本提供段地址 |
| .bss 清零 | 在 `Reset_Handler` 中，手写汇编循环 | `arch_bss_zero()` 在 `z_prep_c()` 中调用 |
| ret_ram 段 | 额外拷贝 `__data_start_ret_ram__` | Zephyr 没有 ret_ram 概念（可通过自定义链接脚本段实现） |
| SRAM3 等待 | 硬编码寄存器配置 | 在 `soc_early_init_hook()` 中处理（如需要） |
| 时机 | 在栈可用后立即执行 | 在 MSP→PSP 切换和 VTOR 设置之后 |

---

## 4. 栈管理

### 裸机
- **单栈模型**：MSP 用于所有代码（包括 main 和 ISR）
- 栈大小在启动文件中硬编码：`Stack_Size = 0x2000`（8KB）
- 堆也在启动文件中定义：`Heap_Size = 0x2000`（8KB）

### Zephyr
- **双栈模型**：
  - **MSP** → 中断栈（`z_interrupt_stacks`），仅 ISR 使用
  - **PSP** → 线程栈，每个线程有独立的栈空间
- 栈大小可配置：
  - `CONFIG_MAIN_STACK_SIZE = 2048`（主线程栈）
  - `CONFIG_ISR_STACK_SIZE = 2048`（中断栈，默认）
- **栈溢出检测**：
  - `CONFIG_INIT_STACKS`：填充 0xAA 标记字节
  - `CONFIG_HW_STACK_PROTECTION`：利用 MPU 硬件保护
- 无传统堆（Zephyr 使用内核内存池）

---

## 5. 中断管理

### 裸机
```
1. 向量表中 144 个 IRQ handler 固定（弱符号）
2. 用户覆盖弱符号来实现具体 ISR
3. HC32F460 INTC SEL 寄存器将外设中断源映射到 IRQ 线
4. 需手动调用 NVIC_EnableIRQ() 使能中断
```

### Zephyr
```
1. 软件中断表（_sw_isr_table）替代硬件向量表中的直接 handler
2. 支持动态注册：irq_connect_dynamic(irq, priority, isr, param, flags)
3. 中断通过 BASEPRI 锁定（而非 PRIMASK），保留最高优先级中断
4. 中断上下文自动保存/恢复浮点寄存器（CONFIG_FPU_SHARING）
5. IRQ 编号由 INTC SEL 映射，与裸机一致
```

HC32F460 特殊之处：
- 有 144 个 NVIC IRQ，但外设中断源有 288+ 个
- INTC SEL[0..143] 寄存器可将任意中断源映射到任意 IRQ 线
- 我们的 Zephyr 驱动中 GPIO EXTINT 使用 IRQ 0-15，UART 使用 IRQ 16+

---

## 6. 系统初始化钩子

### 裸机
```
Reset_Handler → __libc_init_array → SystemInit → main
```
- `SystemInit()` 配置 VTOR、启用 FPU、初始化时钟（默认 MRC 8MHz）
- 所有外设初始化在 `main()` 中手动完成

### Zephyr
```
z_arm_reset
  → soc_early_reset_hook()     [最早，无栈无RAM]
  → soc_reset_hook()           [MSP 已设置]
  → z_prep_c()
    → soc_prep_hook()          [BSS/data 之前]
    → relocate_vector_table()
    → arch_bss_zero() / arch_data_copy()
    → z_cstart()
      → soc_early_init_hook()  [内核初始化前，我们在这里解锁外设]
      → PRE_KERNEL_1 驱动初始化（GPIO、UART）
      → PRE_KERNEL_2 驱动初始化
      → POST_KERNEL 驱动初始化
      → APPLICATION 初始化
      → main 线程启动
```

Zephyr 提供了 5 个 SoC 钩子点，远比裸机灵活：

| 钩子 | 时机 | 我们的用途 |
|------|------|-----------|
| `soc_early_reset_hook` | 最早，无栈无RAM | — |
| `soc_reset_hook` | MSP 已设置 | — |
| `soc_prep_hook` | BSS/data 之前 | — |
| `soc_early_init_hook` | 内核初始化前 | `SystemInit()` + `LL_PERIPH_WE(LL_PERIPH_ALL)` |
| `soc_late_init_hook` | 所有驱动初始化后 | — |

---

## 7. 启动流程图

### 裸机
```
硬件复位
  │
  ▼
向量表[0] → MSP = __StackTop
向量表[1] → PC = Reset_Handler
  │
  ▼
清 SRAM 状态 ──→ 拷贝 .data ──→ 清零 .bss ──→ 配 SRAM3
  │
  ▼
__libc_init_array (C++ 构造) ──→ SystemInit ──→ main()
```

### Zephyr
```
硬件复位
  │
  ▼
向量表[0] → MSP = z_main_stack + MAIN_STACK_SIZE
向量表[1] → PC = z_arm_reset
  │
  ▼
清 CONTROL ──→ soc_early_reset_hook ──→ 设置 MSP
  │
  ▼
soc_reset_hook ──→ 初始化架构硬件 ──→ 锁中断(BASEPRI)
  │
  ▼
[可选] 栈填充 0xAA ──→ 切换到 PSP ──→ z_prep_c()
  │
  ▼
VTOR 设置 ──→ FPU 配置 ──→ BSS 清零 ──→ .data 拷贝
  │
  ▼
z_cstart()
  │
  ├──→ 内核初始化（调度器、定时器、内存）
  ├──→ soc_early_init_hook()     ← SystemInit + LL_PERIPH_WE
  ├──→ PRE_KERNEL_1 驱动初始化   ← GPIO、UART 在这里初始化
  ├──→ PRE_KERNEL_2 驱动初始化
  ├──→ POST_KERNEL 驱动初始化
  └──→ main 线程启动
```

---

## 8. 总结

| 特性 | 裸机 | Zephyr |
|------|------|--------|
| 向量表位置 | 固定在 Flash | Flash 或 SRAM（可配置） |
| MSP 初始值 | `__StackTop`（栈顶） | `z_main_stack`（临时，后切 PSP） |
| 栈模型 | 单栈 MSP | 双栈 MSP(ISR) + PSP(线程) |
| 中断锁定 | 不锁（或 CPSID I） | BASEPRI（保留高优先级中断） |
| .data/.bss | 汇编手写循环 | C 函数 `arch_data_copy/bss_zero` |
| 初始化钩子 | 无 | 5 个钩子点 |
| FPU 初始化 | SystemInit 中 | `z_arm_floating_point_init()`（更精细控制） |
| 中断注册 | 覆盖弱符号 | 软件表 + `irq_connect_dynamic()` |
| 栈保护 | 无 | MPU 保护 + 标记字节检测 |
| 代码大小 | ~600 字节启动代码 | ~2-4KB（包含错误处理、调试支持） |

Zephyr 的启动流程更复杂，但提供了：
- 线程安全的双栈模型
- 灵活的驱动初始化框架
- 内置的栈溢出检测
- 丰富的 SoC 适配钩子
- 支持调试器感知 RTOS 线程
