# 4 时钟控制器（CMU）
## 参考手册
系统时钟的源可选择 6 个时钟源：
1） 外部高速振荡器（XTAL）
2） 外部低速振荡器（XTAL32）
3） MPLL 时钟（MPLL）
4） 内部高速振荡器（HRC）
5） 内部中速振荡器（MRC）
6） 内部低速振荡器（LRC）
两个PLL
MPLL 时钟（MPLL）
UPLL 时钟（UPLL）I2S 使用
各个时钟源可以独立关闭，时钟的频率配置有一定规则，比如HCLK 频率>=PCLK1 频率，PCLK0 频率>=PCLK1 频率

Q&A
Q:一般来说，内部振荡器比外部振荡器功耗低，精度差，所以哪些外设需要高精度的外部振荡器
A:所有高度通信，内部振荡器受温度和电压影响大，精度通常在 1% ~ 5% 左右。usb、can、以太网、rtc、i2s、rgb、mipi、无线设备、高精度adc/dac都需要，
### XTAL 故障检测

### 切换时钟源
在系统复位后，默认系统时钟为 MRC。
只有在目标时钟源已稳定的状态下，才可以从一个时钟源切换到另一个时钟源。
时钟切换时需要正确配置 FLASH/SRAM 的等待周期，防止系统时钟频率大于 FLASH/SRAM 的最大动作频率。

### 切换分频系数


### 时钟频率测量

## hc32_ll_clk.h/c
整体来说，参考手册和代码都很容易理解
但是好像没有单独关闭某个时钟的功能，比如PCLK0 Timer6 计数器用时钟，是否只能降低分频系数来降低速度
## hc32_ll_fcm.h/c

## examples
clk_switch_sysclk

clk_xtalstop_detect

fcm_freq_measure


## 典型硬件电路

# 5 电源控制（PWC）
超高速、高速、超低速三种运行模式，
睡眠、停止和掉电等三种低功耗模式。
VCC 域、VDD 电源域、AVCC 电源域、VDDR 域 4个电源域
VCC域：通过电压调节器(LDO)、VDDR 域调压器(RLDO)给其他模块和外部振荡器供电
VDD域：由 CPU、数字外设等数字逻辑、RAM、FLASH 等构成，在 VDD 域中 RAM 被分为 4 组，可以通过寄存器控制独立断电
VDDR域：低功耗，在掉电模式下通过RLDO 供电，Ret-SRAM 能够保持数据
AVCC：模拟外设供电

睡眠模式：内核停止，外设保持
停止模式：内核和外设都停止
掉电模式：VDD域关闭，VDDR域中的外设仍然可用


## 切换运行模式

## 切换低功耗模式

### 降低功耗的方法

## hc32_ll_pwc.h/c
各种电压控制和低功耗模式控制

# 6 初始化配置（ICG）
在flash中，系统行为配置

# 7 嵌入式 FLASH（EFM）
## 参考手册

## 读写flash

## hc32_ll_efm.h/c

## demo


# 8 内置 SRAM（SRAM）
## 参考手册

## hc32_ll_sram.h/c
## demo
  