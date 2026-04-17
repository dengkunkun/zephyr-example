# HC32F460PETB Power States Bring-up

## Scope
- Zephyr CPU power management is enabled for the HC32F460PETB port
- Implemented states:
  - `runtime-idle`
  - `suspend-to-idle` via HC32 `PWC_SLEEP_Enter()`
  - `suspend-to-ram` via HC32 `PWC_STOP_Enter()`
- A board demo for hibernate-like behavior is added with HC32 power-down mode

## Devicetree model
- `hc32f460.dtsi` now declares CPU power states and attaches them to `cpu0`
- Current states are:
  - `runtime-idle`
  - `suspend-to-idle`
  - `suspend-to-ram`
- `suspend-to-ram` is intentionally **locked by default** and only used by shell/self-tests

## Zephyr PM hooks
- File: `soc/hdsc/hc32f460/power.c`
- Implemented hooks:
  - `pm_state_set()`
  - `pm_state_exit_post_ops()`
- State mapping:
  - `PM_STATE_RUNTIME_IDLE` -> plain Cortex-M `WFI`
  - `PM_STATE_SUSPEND_TO_IDLE` -> `PWC_SLEEP_Enter(PWC_SLEEP_WFI)`
  - `PM_STATE_SUSPEND_TO_RAM` -> `PWC_STOP_Enter(PWC_STOP_WFI)`

## Stop-mode timing strategy
- HC32 STOP mode halts SysTick, so Zephyr needs a low-power time companion
- This port uses the HC32 **Wakeup Timer (WKTM)** through the SysTick low-power hook API:
  - `z_sys_clock_lpm_enter()`
  - `z_sys_clock_lpm_exit()`
- Current policy:
  - use `LRC` (32.768 kHz) for short sleeps
  - fall back to `64 Hz` mode for longer sleeps
- This keeps Zephyr time accounting coherent across STOP wakeups that are driven by WKTM

## Why STOP is locked by default
- The board still uses active console/shell traffic, watchdog keepalive, and other demo activity
- To keep behavior predictable, deep STOP is not entered automatically during normal idle
- Users can still exercise STOP explicitly with:
  - `power stop <ms>`
  - `test power`

## Hibernate / power-down demo
- Command: `power hibernate <ms>`
- Implementation:
  - configures HC32 `PWC_PD_*`
  - arms WKTM as the wake source
  - enters HC32 power-down mode
- Wake from power-down is a **reset**, so the board reboots after the timer expires
- The current implementation uses **LRC-backed WKTM** because that is the path covered by the vendor power-down wake sample
- Current software limit is about **124 ms max** (`0xFFF / 32768 Hz`) for the WKTM-backed delay
- Boot code captures and prints the reset cause, including WKTM-triggered power-down wakeups
- Final HC32-specific constraint:
  - SWDT must use **ICG reset-stop (`RST_STOP`)** so the driver starts it on first `wdt_setup()` feed
  - leaving SWDT in ICG auto-start mode caused the board to disappear into deep sleep and later come back only through `swdt_reset`, not a valid WKTM power-down wake

## CPU frequency switching
- The clock driver now supports:
  - **high-performance** mode: `HXT 8 MHz -> PLL 200 MHz`
  - **low-speed** mode: `MRC 8 MHz`
- Runtime switching updates:
  - `SystemCoreClock`
  - Zephyr SysTick timing via `z_sys_clock_hw_cycles_per_sec_update()`
  - USART1 baud rate through an explicit HC32 UART reconfigure helper
- Shell command:
  - `clock speed low`
  - `clock speed high`

## UART / DMA interaction
- DMA shell RX uses a 5 ms poll timer for partial-buffer events
- Before an explicit STOP test, the PM helper pauses that DMA RX poll timer
- After wakeup, the timer is resumed so the shell remains responsive

## Runtime tests
- Shell commands:
  - `power info`
  - `power sleep <ms>`
  - `power stop <ms>`
  - `power hibernate <ms>`
  - `clock speed <low|high>`
  - `test power`
- `test power` currently validates:
  - forced sleep entry/exit
  - low-speed clock switch
  - high-speed clock restore
  - forced STOP entry/exit

## Hardware validation status
- Validated on hardware:
  - `hc32_clock_set_mode(HC32_CLOCK_MODE_LOW_SPEED)` -> `SystemCoreClock = 8000000`
  - `hc32_clock_set_mode(HC32_CLOCK_MODE_HIGH_PERFORMANCE)` -> `SystemCoreClock = 200000000`
  - `hc32_power_force_sleep_ms(20)` -> success
  - `hc32_power_force_stop_ms(30)` -> success
  - `power hibernate 100` -> `power_down_reset = yes`, `wakeup_timer = yes`
- Validation method:
  - default and DMA builds both compile successfully
  - low/high/sleep/stop paths were exercised on board with pyOCD + GDB batch calls
  - hibernate wake/reset was validated on board with timed pyOCD retained-state readback after reset:
    - at 5 s and 20 s after the trigger, boot-status bytes reported `pd=yes` and `wkt=yes`
  - the validated runtime image uses SWDT ICG `RST_STOP`; flash readback shows `0x400 = 0xFFDFEF4B`
- Remaining limitation:
  - longer power-down delays need RTC-backed wake if the current WKTM-only window is not sufficient

## Current limitations
- STOP wake timing is currently modeled around WKTM-based wakeups
- External early-wakeup sources are not yet part of the deep-sleep validation path
- Power-down mode is exposed as a demo/reset flow, not yet as a full Zephyr `sys_poweroff()` integration
