# HC32F460 clock-control / RCC port

## Goal

Provide a Zephyr `clock_control` driver for HC32F460 so the board boots with a real PLL clock tree, peripheral gates are managed through the standard API, and runtime clock queries return the values drivers actually need.

## Verified board clock tree

On the tested HC32F460PETB board, the stable configuration is:

- **HXT = 8 MHz**
- **SYSCLK/HCLK = 200 MHz**
- **PCLK0 = 200 MHz**
- **PCLK1 = EXCLK = PCLK4 = 100 MHz**
- **PCLK2 = PCLK3 = 50 MHz**

The implementation uses:

- `pll-m = <1>`
- `pll-n = <50>`
- `pll-p = <2>`
- `pll-q = <2>`
- `pll-r = <2>`

This gives `8 MHz / 1 * 50 / 2 = 200 MHz`.

## Why the HXT value changed

An earlier attempt modeled the board as **24 MHz HXT**. The firmware then produced readable UART output only at **38400** while configured for **115200**, which implies the real `PCLK1` was only one third of the expected value. After switching the board model to **8 MHz HXT** and recalculating the PLL input divider, serial output returned to correct **115200** timing and the runtime self-tests passed.

If a different HC32F460 board variant is populated with a 24 MHz crystal, update both:

- `dts/arm/hdsc/hc32f460.dtsi` (`hxt-frequency`, `pll-m`)
- `soc/hdsc/hc32f460/hc32f4xx_conf.h` (`XTAL_VALUE`)

## Implementation

Main file:

- `drivers/clock_control/clock_control_hc32.c`

The driver:

1. unlocks and enables HXT/LXT/PLL through the DDL clock API
2. applies the validated bus divider profile
3. switches SYSCLK to PLL
4. refreshes `SystemCoreClock` and `stc_clock_freq_t`
5. exposes Zephyr `clock_control_on()`, `clock_control_off()`, and `clock_control_get_rate()`

Peripheral gates use a compact `(fcg_bus, bit)` clock specifier from devicetree:

```dts
clocks = <&clk 1 24>;
```

For USART1 this means **FCG1 bit 24**, while `clock_control_get_rate()` returns the matching **PCLK1** bus rate.

## Devicetree / SoC integration

Key files:

- `dts/bindings/clock/hdsc,hc32-clock.yaml`
- `dts/arm/hdsc/hc32f460.dtsi`
- `soc/hdsc/hc32f460/Kconfig.defconfig`
- `soc/hdsc/hc32f460/hc32f4xx_conf.h`

The SoC DTS now declares a real clock controller node and the CPU model uses `clock-frequency = <200000000>`.

## Runtime verification

The board image now verifies clocks in two ways:

1. boot self-test in `src/driver_tests.c`
2. shell commands:
   - `test core`
   - `clock info`
   - `sysinfo`

Expected runtime result:

```text
[TEST] clock-tree: PASS (SYSCLK=200000000 HCLK=200000000 PCLK1=100000000)
Clock source: PLL
SYSCLK=200000000 HCLK=200000000 EXCLK=100000000
PCLK0=200000000 PCLK1=100000000 PCLK2=50000000 PCLK3=50000000 PCLK4=100000000
```

## Current limitations

- Runtime CPU frequency switching is still deferred until the timer / PM stack is ready.
- Only the clock tree required by the current board bring-up is implemented.
- Timer, watchdog, and power-state drivers should consume this driver instead of hardcoding bus rates.
