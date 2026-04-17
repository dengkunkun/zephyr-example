# HC32F460 pinctrl port

## Goal

Move HC32F460 pin mux configuration out of individual drivers and into Zephyr pinctrl state data so UART and later peripherals can use standard devicetree pin descriptions.

## Implementation

Main files:

- `drivers/pinctrl/pinctrl_hc32.c`
- `soc/hdsc/hc32f460/pinctrl_soc.h`
- `include/zephyr/dt-bindings/pinctrl/hc32-pinctrl.h`
- `dts/bindings/pinctrl/hdsc,hc32-pinctrl.yaml`

The HC32 pin encoding packs:

- port
- pin
- alternate function
- pull mode
- open-drain flag

into a single `pinctrl_soc_pin_t`.

The binding exposes:

```c
HC32_PINMUX(port, pin, func)
```

and currently defines:

- `HC32_FUNC_GPIO`
- `HC32_FUNC_USART1_TX`
- `HC32_FUNC_USART1_RX`

## Why the driver uses DDL GPIO APIs

The first version wrote PCR/PFSR registers directly. That was enough to compile, but it is safer and easier to keep the pinctrl backend aligned with the vendor library behavior.

The current backend therefore uses:

- `GPIO_StructInit()`
- `GPIO_Init()`
- `GPIO_SetFunc()`

This keeps the Zephyr pinctrl layer compatible with the same register programming model already validated in the HC32 DDL examples and earlier bare-metal tests.

## Verified USART1 state

Board DTS:

```dts
&pinctrl {
	usart1_default: usart1_default {
		group1 {
			pinmux = <HC32_PINMUX(HC32_PORT_A, 9, HC32_FUNC_USART1_TX)>,
				 <HC32_PINMUX(HC32_PORT_A, 10, HC32_FUNC_USART1_RX)>;
			bias-pull-up;
		};
	};

	usart1_sleep: usart1_sleep {
		group1 {
			pinmux = <HC32_PINMUX(HC32_PORT_A, 9, HC32_FUNC_GPIO)>,
				 <HC32_PINMUX(HC32_PORT_A, 10, HC32_FUNC_GPIO)>;
			bias-disable;
		};
	};
};
```

USART1 now consumes these states from the UART driver through:

- `PINCTRL_DT_INST_DEFINE()`
- `PINCTRL_DT_INST_DEV_CONFIG_GET()`
- `pinctrl_apply_state(..., PINCTRL_STATE_DEFAULT)`

## Runtime verification

`src/driver_tests.c` checks the live PA9/PA10 PFSR function values:

```text
[TEST] pinctrl-usart1: PASS (PA9=32 PA10=33)
```

Shell and boot output were also verified through the DAPLink VCP on:

- **PA9 = USART1_TX**
- **PA10 = USART1_RX**

## Current scope

The current pinctrl backend is intentionally small and only implements what the board needs right now:

- alternate function select
- pull-up handling
- open-drain handling

Drive-strength and more complex pin attributes can be added later when the next peripheral blocks need them.
