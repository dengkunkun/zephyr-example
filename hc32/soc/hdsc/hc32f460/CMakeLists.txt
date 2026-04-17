# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(.)

# Include HC32F460 DDL device headers (but NOT the old CMSIS Core —
# Zephyr provides CMSIS 6 Core via the cmsis_6 module)
set(HC32_DDL_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../HC32F460_DDL_Rev3.3.0)
zephyr_include_directories(
  ${HC32_DDL_ROOT}/drivers/cmsis/Device/HDSC/hc32f4xx/Include
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/inc
)

zephyr_compile_definitions(
  HC32F460
  USE_DDL_DRIVER
)

zephyr_sources(soc.c icg_config.c power.c)

# DDL driver sources needed for system init
zephyr_sources(
  ${HC32_DDL_ROOT}/drivers/cmsis/Device/HDSC/hc32f4xx/Source/system_hc32f460.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_clk.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_efm.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_fcg.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_gpio.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_interrupts.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32f460_ll_interrupts_share.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_pwc.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_rmu.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_sram.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_usart.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_utility.c
)

zephyr_linker_sources(ROM_START SORT_KEY 0x2icg icg_rom_start.ld)

zephyr_sources_ifdef(CONFIG_COUNTER_HC32_TMRA
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_tmra.c
)

zephyr_sources_ifdef(CONFIG_WDT_HC32_WDT
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../drivers/watchdog/wdt_hc32.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_wdt.c
)

zephyr_sources_ifdef(CONFIG_WDT_HC32_SWDT
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../drivers/watchdog/wdt_hc32_swdt.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_swdt.c
)

# HC32F460 DDL conf header location
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/arm/cortex_m/scripts/linker.ld CACHE INTERNAL "")

# Include out-of-tree drivers
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../drivers ${CMAKE_CURRENT_BINARY_DIR}/drivers)
