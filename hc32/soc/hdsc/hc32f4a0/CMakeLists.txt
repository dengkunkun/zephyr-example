# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(.)

set(HC32_DDL_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../HC32F4A0_DDL_Rev2.4.0)
zephyr_include_directories(
  ${HC32_DDL_ROOT}/drivers/cmsis/Device/HDSC/hc32f4xx/Include
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/inc
)

zephyr_compile_definitions(
  HC32F4A0
  USE_DDL_DRIVER
)

zephyr_sources(soc.c icg_config.c power.c)

zephyr_sources(
  ${HC32_DDL_ROOT}/drivers/cmsis/Device/HDSC/hc32f4xx/Source/system_hc32f4a0.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_clk.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_efm.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_fcg.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_gpio.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_interrupts.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32f4a0_ll_interrupts_share.c
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

set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/arm/cortex_m/scripts/linker.ld CACHE INTERNAL "")

# Include out-of-tree drivers (safe: only one SoC CMakeLists.txt is active per build)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../drivers ${CMAKE_CURRENT_BINARY_DIR}/drivers)
