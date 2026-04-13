cmake_minimum_required(VERSION 3.20)

# ── Toolchain ────────────────────────────────────────────────────────────────
set(CMAKE_TOOLCHAIN_FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchain-arm-none-eabi.cmake
    CACHE FILEPATH "Toolchain file")

project(bsp_test C ASM)

# ── Chip / board constants ───────────────────────────────────────────────────
set(MCU         "cortex-m4"  CACHE STRING "MCU core")
set(PYOCD_TARGET "hc32f460xe" CACHE STRING "pyOCD target name")
set(PYOCD       "pyocd"      CACHE STRING "pyOCD executable")
set(PROBE       ""           CACHE STRING "pyOCD probe UID (optional)")

# ── Paths ────────────────────────────────────────────────────────────────────
set(DDL_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/../../HC32F460_DDL_Rev3.3.0)
set(CMSIS_DIR   ${DDL_DIR}/drivers/cmsis)
set(LL_DIR      ${DDL_DIR}/drivers/hc32_ll_driver)
set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/cmake/HC32F460xE.ld)

# ── Compile definitions ─────────────────────────────────────────────────────
add_compile_definitions(
    __DEBUG
    HC32F460
    USE_DDL_DRIVER
    XTAL_VALUE=24000000UL
    XTAL32_VALUE=32768UL
)

# ── Include paths ────────────────────────────────────────────────────────────
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${CMAKE_CURRENT_SOURCE_DIR}/drivers
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMSIS_DIR}/Device/HDSC/hc32f4xx/Include
    ${LL_DIR}/inc
    ${CMSIS_DIR}/Include
)

# ── Common compiler / linker flags ───────────────────────────────────────────
set(MCU_FLAGS "-mcpu=${MCU} -mthumb -mthumb-interwork")

add_compile_options(
    -mcpu=${MCU} -mthumb -mthumb-interwork
    -ffunction-sections -fdata-sections -fno-common -fsigned-char
    -Wall -Wno-main -Wno-comment
)
set(CMAKE_C_STANDARD 11)

# Debug build by default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()
set(CMAKE_C_FLAGS_DEBUG   "-Og -g3" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_RELEASE "-Os"     CACHE STRING "" FORCE)

# Assembly also needs MCU flags
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -mcpu=${MCU} -mthumb -mthumb-interwork -x assembler-with-cpp -g3")

# ── Detect toolchain variant (newlib-nano vs picolibc) ───────────────────────
execute_process(
    COMMAND ${CMAKE_C_COMPILER} --specs=nano.specs -E -x c /dev/null -o /dev/null
    RESULT_VARIABLE _nano_result
    OUTPUT_QUIET ERROR_QUIET
)

if(_nano_result EQUAL 0)
    message(STATUS "Toolchain: newlib-nano (standard arm-none-eabi)")
    set(LIBC_FLAGS "--specs=nano.specs --specs=nosys.specs")
    set(NEED_PICOLIBC_STUB OFF)
else()
    message(STATUS "Toolchain: picolibc (Zephyr SDK)")
    set(LIBC_FLAGS "--specs=picolibc.specs -nostartfiles")
    set(NEED_PICOLIBC_STUB ON)
endif()

set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} -mcpu=${MCU} -mthumb -mthumb-interwork ${LIBC_FLAGS} -T${LINKER_SCRIPT} -Wl,--gc-sections -Wl,-Map,${CMAKE_BINARY_DIR}/${PROJECT_NAME}.map")

# ── Sources ──────────────────────────────────────────────────────────────────
# Startup assembly
set(ASM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/GCC/startup_hc32f460.S
)

# Application sources
file(GLOB APP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.c
)

# BSP drivers
file(GLOB BSP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/drivers/*.c
)

# Test sources
file(GLOB TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.c
)

# DDL LL driver sources (only compile what is enabled in hc32f4xx_conf.h)
set(LL_SOURCES
    ${CMSIS_DIR}/Device/HDSC/hc32f4xx/Source/system_hc32f460.c
    ${LL_DIR}/src/hc32_ll.c
    ${LL_DIR}/src/hc32_ll_clk.c
    ${LL_DIR}/src/hc32_ll_efm.c
    ${LL_DIR}/src/hc32_ll_fcg.c
    ${LL_DIR}/src/hc32_ll_gpio.c
    ${LL_DIR}/src/hc32_ll_icg.c
    ${LL_DIR}/src/hc32_ll_interrupts.c
    ${LL_DIR}/src/hc32_ll_pwc.c
    ${LL_DIR}/src/hc32_ll_sram.c
    ${LL_DIR}/src/hc32_ll_usart.c
    ${LL_DIR}/src/hc32_ll_utility.c
    # Peripheral drivers for all enabled modules:
    ${LL_DIR}/src/hc32_ll_adc.c
    ${LL_DIR}/src/hc32_ll_aes.c
    ${LL_DIR}/src/hc32_ll_aos.c
    ${LL_DIR}/src/hc32_ll_can.c
    ${LL_DIR}/src/hc32_ll_crc.c
    ${LL_DIR}/src/hc32_ll_dcu.c
    ${LL_DIR}/src/hc32_ll_dma.c
    ${LL_DIR}/src/hc32_ll_hash.c
    ${LL_DIR}/src/hc32_ll_i2c.c
    ${LL_DIR}/src/hc32_ll_ots.c
    ${LL_DIR}/src/hc32_ll_rmu.c
    ${LL_DIR}/src/hc32_ll_rtc.c
    ${LL_DIR}/src/hc32_ll_sdioc.c
    ${LL_DIR}/src/hc32_ll_spi.c
    ${LL_DIR}/src/hc32_ll_tmr0.c
    ${LL_DIR}/src/hc32_ll_tmr4.c
    ${LL_DIR}/src/hc32_ll_tmra.c
    ${LL_DIR}/src/hc32_ll_trng.c
    ${LL_DIR}/src/hc32_ll_wdt.c
    ${LL_DIR}/src/hc32f460_ll_interrupts_share.c
)

# Picolibc stub (Zephyr SDK only)
if(NEED_PICOLIBC_STUB)
    list(APPEND LL_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/GCC/source/picolibc_stub.c
    )
endif()

# ── Executable ───────────────────────────────────────────────────────────────
add_executable(${PROJECT_NAME}
    ${ASM_SOURCES}
    ${APP_SOURCES}
    ${BSP_SOURCES}
    ${TEST_SOURCES}
    ${LL_SOURCES}
)

target_link_libraries(${PROJECT_NAME} PRIVATE m)

# ── Post-build: generate .hex, .bin, .lst ────────────────────────────────────
set(ELF  $<TARGET_FILE:${PROJECT_NAME}>)
set(HEX  ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.hex)
set(BIN  ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin)
set(LST  ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.lst)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex ${ELF} ${HEX}
    COMMAND ${CMAKE_OBJCOPY} -O binary ${ELF} ${BIN}
    COMMAND ${CMAKE_OBJDUMP} --source --all-headers --demangle --line-numbers --wide ${ELF} > ${LST}
    COMMAND ${CMAKE_SIZE} ${ELF}
    COMMENT "Generating HEX, BIN, LST and size report"
)

# ── Flash / debug targets ────────────────────────────────────────────────────
set(PYOCD_ARGS "-t ${PYOCD_TARGET}")
if(NOT "${PROBE}" STREQUAL "")
    string(APPEND PYOCD_ARGS " --probe ${PROBE}")
endif()

add_custom_target(flash
    COMMAND ${PYOCD} load ${PYOCD_ARGS} ${HEX}
    DEPENDS ${PROJECT_NAME}
    COMMENT "Flashing ${HEX} via pyOCD"
)

add_custom_target(erase
    COMMAND ${PYOCD} erase ${PYOCD_ARGS} --chip
    COMMENT "Erasing chip via pyOCD"
)

add_custom_target(gdbserver
    COMMAND ${PYOCD} gdbserver ${PYOCD_ARGS} --port 3333 --telnet-port 4444
    COMMENT "Starting pyOCD GDB server on :3333"
)

add_custom_target(debug
    COMMAND ${CMAKE_C_COMPILER}
            -ex "target remote :3333"
            -ex "monitor reset halt"
            -ex "load"
            ${ELF}
    DEPENDS ${PROJECT_NAME}
    COMMENT "Launching GDB debug session"
)

# ── Info target ──────────────────────────────────────────────────────────────
add_custom_target(info
    COMMAND ${CMAKE_COMMAND} -E echo "Build:       cmake --build build -j2"
    COMMAND ${CMAKE_COMMAND} -E echo "Flash:       cmake --build build --target flash"
    COMMAND ${CMAKE_COMMAND} -E echo "Erase:       cmake --build build --target erase"
    COMMAND ${CMAKE_COMMAND} -E echo "GDB server:  cmake --build build --target gdbserver"
    COMMAND ${CMAKE_COMMAND} -E echo "Debug:       cmake --build build --target debug"
    COMMAND ${CMAKE_COMMAND} -E echo "Probe:       cmake -B build -DPROBE=<uid>"
)
