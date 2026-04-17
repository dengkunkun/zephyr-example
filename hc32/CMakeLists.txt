# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Point Zephyr to our out-of-tree SoC, board, and DTS definitions
list(APPEND SOC_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hc32f_demo)

if(CONFIG_BOARD_HC32F460PETB)
	# F460 demo: LED blink + buttons + shell driver tests
	target_sources(app PRIVATE src/main.c)
	target_sources(app PRIVATE src/driver_tests.c)
	target_sources_ifdef(CONFIG_SHELL app PRIVATE src/shell_cmds.c)
elseif(CONFIG_BOARD_HC32F4A0PGTB)
	# F4A0 smoke test: printk over USART3 console
	target_sources(app PRIVATE src/main_f4a0_smoke.c)
endif()
