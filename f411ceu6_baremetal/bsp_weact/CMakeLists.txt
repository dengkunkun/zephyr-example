cmake_minimum_required(VERSION 3.20)

# ── Toolchain ────────────────────────────────────────────────────────────────
set(CMAKE_TOOLCHAIN_FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchain-arm-none-eabi.cmake
    CACHE FILEPATH "Toolchain file")

project(bsp_weact C ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Chip / board constants ───────────────────────────────────────────────────
set(MCU          "cortex-m4"   CACHE STRING "MCU core")
set(PYOCD_TARGET "stm32f411ce" CACHE STRING "pyOCD target name")
set(PYOCD        "pyocd"       CACHE STRING "pyOCD executable")
set(PROBE        ""            CACHE STRING "pyOCD probe UID (optional)")

# ── Paths ────────────────────────────────────────────────────────────────────
set(CUBE_DIR       ${CMAKE_CURRENT_SOURCE_DIR}/../STM32CubeF4)
set(CMSIS_DIR      ${CUBE_DIR}/Drivers/CMSIS)
set(HAL_DIR        ${CUBE_DIR}/Drivers/STM32F4xx_HAL_Driver)
set(LINKER_SCRIPT  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/STM32F411CEUx_FLASH.ld)

# ── Compile definitions ─────────────────────────────────────────────────────
add_compile_definitions(
    STM32F411xE
    USE_FULL_LL_DRIVER
    HSE_VALUE=8000000U
)

# ── Include paths ────────────────────────────────────────────────────────────
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc
    ${CMSIS_DIR}/Device/ST/STM32F4xx/Include
    ${CMSIS_DIR}/Include
    ${HAL_DIR}/Inc
)

# ── Common compiler / linker flags ───────────────────────────────────────────
add_compile_options(
    -mcpu=${MCU} -mthumb -mfloat-abi=soft
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
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -mcpu=${MCU} -mthumb -x assembler-with-cpp -g3")

# ── Detect toolchain variant (newlib-nano vs picolibc) ───────────────────────
execute_process(
    COMMAND ${CMAKE_C_COMPILER} --specs=nano.specs -E -x c /dev/null -o /dev/null
    RESULT_VARIABLE _nano_result
    OUTPUT_QUIET ERROR_QUIET
)

if(_nano_result EQUAL 0)
    message(STATUS "Toolchain: newlib-nano (standard arm-none-eabi)")
    set(LIBC_FLAGS "--specs=nano.specs --specs=nosys.specs")
else()
    message(STATUS "Toolchain: picolibc (Zephyr SDK)")
    set(LIBC_FLAGS "--specs=picolibc.specs -nostartfiles")
endif()

set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} -mcpu=${MCU} -mthumb ${LIBC_FLAGS} -T${LINKER_SCRIPT} -Wl,--gc-sections -Wl,-Map,${CMAKE_BINARY_DIR}/${PROJECT_NAME}.map")

# ── Sources ──────────────────────────────────────────────────────────────────
# Startup assembly (GCC variant from STM32CubeF4)
set(ASM_SOURCES
    ${CMSIS_DIR}/Device/ST/STM32F4xx/Source/Templates/gcc/startup_stm32f411xe.s
)

# Application sources
set(APP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/stm32f4xx_it.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/system_stm32f4xx.c
)

# STM32 LL driver sources (only what this project uses)
set(LL_SOURCES
    ${HAL_DIR}/Src/stm32f4xx_ll_gpio.c
    ${HAL_DIR}/Src/stm32f4xx_ll_utils.c
    ${HAL_DIR}/Src/stm32f4xx_ll_rcc.c
)

# ── Executable ───────────────────────────────────────────────────────────────
add_executable(${PROJECT_NAME}
    ${ASM_SOURCES}
    ${APP_SOURCES}
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

# ── Info target ──────────────────────────────────────────────────────────────
add_custom_target(info
    COMMAND ${CMAKE_COMMAND} -E echo "Build:       cmake --build build"
    COMMAND ${CMAKE_COMMAND} -E echo "Flash:       cmake --build build --target flash"
    COMMAND ${CMAKE_COMMAND} -E echo "Erase:       cmake --build build --target erase"
    COMMAND ${CMAKE_COMMAND} -E echo "GDB server:  cmake --build build --target gdbserver"
    COMMAND ${CMAKE_COMMAND} -E echo "Probe:       cmake -B build -DPROBE=<uid>"
)
