# HC32F460PETB TMRA Timer / Counter Bring-up

## Scope
- First Zephyr timer driver is implemented as a **TMRA-based `counter` driver**
- Current target is **TMRA1**
- Mode is intentionally narrow and testable:
  - software clock source only (`PCLK1`)
  - sawtooth count-up mode
  - 16-bit top value
  - overflow callback + 8 compare channels

## Why TMRA instead of TMR0
- `counter_basic_api` expects a counter device to support a programmable **top value** and **alarm channels** at the same time.
- `TMR0` is simple, but a single channel does not map cleanly to “top + independent alarm”.
- `TMRA` has the right hardware shape:
  - dedicated `PERAR` top register
  - one shared count stream
  - compare registers `CMPAR1..CMPAR8`
  - overflow status/interrupt plus compare-match status/interrupts

## Devicetree model
- New binding: `hdsc,hc32-tmra-counter`
- SoC node:
  - `tmra1_counter: counter@40015000`
  - `clocks = <&clk 2 2>` -> FCG2 TIMERA1 gate
  - `clock-prescaler = <1024>`
  - overflow IRQ source: `INT_SRC_TMRA_1_OVF = 256`
  - compare IRQ source: `INT_SRC_TMRA_1_CMP = 258`
- Board enables the node and exposes `counter0 = &tmra1_counter`

## Driver design
- File: `drivers/counter/counter_hc32_tmra.c`
- Uses the existing HC32 `clock_control` driver to:
  - ungate TMRA1
  - read `PCLK1`
  - derive runtime counter frequency from `PCLK1 / prescaler`
- Uses HC32 DDL TMRA APIs for peripheral setup:
  - `TMRA_Init()`
  - `TMRA_SetPeriodValue()`
  - `TMRA_SetCompareValue()`
  - `TMRA_IntCmd()`
  - `TMRA_Start()` / `TMRA_Stop()`

## Interrupt routing
- HC32 TMRA routes peripheral sources through the INTC SEL registers before they reach NVIC.
- The driver maps:
  - TMRA1 overflow -> IRQ 22
  - TMRA1 compare -> IRQ 23
- Compare interrupts are shared in hardware, so the ISR scans all compare-match flags and dispatches callbacks per channel.

## Supported Zephyr counter features
- `counter_start()`
- `counter_stop()`
- `counter_get_value()`
- `counter_reset()`
- `counter_set_value()`
- `counter_set_channel_alarm()`
- `counter_cancel_channel_alarm()`
- `counter_set_top_value()`
- `counter_get_pending_int()`
- `counter_get_top_value()`
- `counter_get_frequency()`
- `counter_get/set_guard_period()`

## Late-alarm handling
- Absolute alarms use Zephyr’s guard-period model.
- If an absolute alarm is already late:
  - driver returns `-ETIME`
  - if `COUNTER_ALARM_CFG_EXPIRE_WHEN_LATE` is set, callback is still delivered immediately from interrupt context
- Relative alarms use the same “short delay may already be late” strategy common in other Zephyr counter drivers.

## Runtime tests
- Boot self-tests:
  - `tmra-counter-info`
  - `tmra-alarm-rel`
  - `tmra-top+alarm`
  - `tmra-late-alarm`
- Shell commands:
  - `counter info`
  - `counter start`
  - `counter stop`
  - `test timer`

## Current configuration
- Clock source: `PCLK1 = 100 MHz`
- Prescaler: `1024`
- Counter frequency: about `97.656 kHz`
- Max top value: `65535`
- Wrap period at default top: about `671 ms`
