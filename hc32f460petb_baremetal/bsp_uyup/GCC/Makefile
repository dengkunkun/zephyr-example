PROJECT := gpio_output
BUILD_DIR := build
TARGET ?= hc32f460xe
PYOCD ?= pyocd
PROBE ?=

PREFIX ?= arm-none-eabi-
CC := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
OBJDUMP := $(PREFIX)objdump
SIZE := $(PREFIX)size
GDB := $(PREFIX)gdb

MCU_FLAGS := -mcpu=cortex-m4 -mthumb -mthumb-interwork
COMMON_FLAGS := $(MCU_FLAGS) -ffunction-sections -fdata-sections -fno-common -fsigned-char -Wall -Wno-main -Wno-comment
CFLAGS := $(COMMON_FLAGS) -Og -g3 -std=c11
ASFLAGS := $(COMMON_FLAGS) -x assembler-with-cpp -g3
CPPFLAGS := \
	-D__DEBUG \
	-DHC32F460 \
	-DUSE_DDL_DRIVER \
	-I../source \
	-I../../drivers/cmsis/Device/HDSC/hc32f4xx/Include \
	-I../../drivers/hc32_ll_driver/inc \
	-I../../drivers/cmsis/Include

# Detect toolchain: Zephyr SDK (picolibc) vs standard arm-none-eabi (newlib-nano)
TOOLCHAIN_ID := $(shell $(CC) -dumpversion 2>/dev/null)
HAS_NANO_SPECS := $(shell $(CC) --specs=nano.specs -E -x c /dev/null -o /dev/null 2>/dev/null && echo yes || echo no)

ifeq ($(HAS_NANO_SPECS),yes)
  # Standard arm-none-eabi-gcc with newlib-nano
  LDFLAGS := $(MCU_FLAGS) --specs=nano.specs --specs=nosys.specs -Tconfig/linker/HC32F460xE.ld -Wl,--gc-sections -Wl,-Map,$(BUILD_DIR)/$(PROJECT).map
  LDLIBS := -lm
else
  # Zephyr SDK (picolibc) — provide stdout stub for hc32_ll_utility printf
  LDFLAGS := $(MCU_FLAGS) --specs=picolibc.specs -nostartfiles -Tconfig/linker/HC32F460xE.ld -Wl,--gc-sections -Wl,-Map,$(BUILD_DIR)/$(PROJECT).map
  LDLIBS := -lm
  NEED_PICOLIBC_STUB := yes
endif

ASM_SRCS := \
	startup_hc32f460.S \

C_SRCS := \
	../source/main.c \
	../../drivers/cmsis/Device/HDSC/hc32f4xx/Source/system_hc32f460.c \
	../../drivers/hc32_ll_driver/src/hc32_ll.c \
	../../drivers/hc32_ll_driver/src/hc32_ll_clk.c \
	../../drivers/hc32_ll_driver/src/hc32_ll_efm.c \
	../../drivers/hc32_ll_driver/src/hc32_ll_fcg.c \
	../../drivers/hc32_ll_driver/src/hc32_ll_gpio.c \
	../../drivers/hc32_ll_driver/src/hc32_ll_icg.c \
	../../drivers/hc32_ll_driver/src/hc32_ll_interrupts.c \
	../../drivers/hc32_ll_driver/src/hc32_ll_pwc.c \
	../../drivers/hc32_ll_driver/src/hc32_ll_sram.c \
	../../drivers/hc32_ll_driver/src/hc32_ll_usart.c \
	../../drivers/hc32_ll_driver/src/hc32_ll_utility.c

ifeq ($(NEED_PICOLIBC_STUB),yes)
  C_SRCS += source/picolibc_stub.c
endif

ASM_OBJS := $(addprefix $(BUILD_DIR)/,$(subst ../,,$(ASM_SRCS:.S=.o)))
C_OBJS := $(addprefix $(BUILD_DIR)/,$(subst ../,,$(C_SRCS:.c=.o)))
OBJS := $(ASM_OBJS) $(C_OBJS)
DEPS := $(OBJS:.o=.d)

.PHONY: all clean size flash-info

PYOCD_ARGS := -t $(TARGET)
ifneq ($(strip $(PROBE)),)
PYOCD_ARGS += --probe $(PROBE)
endif

all: $(BUILD_DIR)/$(PROJECT).elf $(BUILD_DIR)/$(PROJECT).hex $(BUILD_DIR)/$(PROJECT).bin $(BUILD_DIR)/$(PROJECT).lst

$(BUILD_DIR)/$(PROJECT).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) $(LDLIBS) -o $@
	$(SIZE) $@

$(BUILD_DIR)/$(PROJECT).hex: $(BUILD_DIR)/$(PROJECT).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/$(PROJECT).bin: $(BUILD_DIR)/$(PROJECT).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/$(PROJECT).lst: $(BUILD_DIR)/$(PROJECT).elf
	$(OBJDUMP) --source --all-headers --demangle --line-numbers --wide $< > $@

define compile_c_rule
$(BUILD_DIR)/$(subst ../,,$(1:.c=.o)): $(1)
	@mkdir -p $$(dir $$@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $$< -o $$@
endef

define compile_asm_rule
$(BUILD_DIR)/$(subst ../,,$(1:.S=.o)): $(1)
	@mkdir -p $$(dir $$@)
	$(CC) $(CPPFLAGS) $(ASFLAGS) -MMD -MP -c $$< -o $$@
endef

$(foreach src,$(C_SRCS),$(eval $(call compile_c_rule,$(src))))
$(foreach src,$(ASM_SRCS),$(eval $(call compile_asm_rule,$(src))))

clean:
	rm -rf $(BUILD_DIR)

size: $(BUILD_DIR)/$(PROJECT).elf
	$(SIZE) $<

flash: all
	$(PYOCD) load $(PYOCD_ARGS) $(BUILD_DIR)/$(PROJECT).hex

erase:
	$(PYOCD) erase $(PYOCD_ARGS) --chip

gdbserver:
	$(PYOCD) gdbserver $(PYOCD_ARGS) --port 3333 --telnet-port 4444

debug: all gdbserver-bg
	$(GDB) -ex "target remote :3333" -ex "monitor reset halt" -ex "load" $(BUILD_DIR)/$(PROJECT).elf

flash-info:
	@echo "Build:      make -j2"
	@echo "Build (Zephyr SDK):  make -j2 PREFIX=/path/to/arm-zephyr-eabi-"
	@echo "Flash:      make flash"
	@echo "Erase:      make erase"
	@echo "GDB server: make gdbserver"
	@echo "Debug:      make debug"
	@echo "Optional probe selection: make flash PROBE=<probe-uid>"
	@echo "Note: stock OpenOCD 0.12 does not include an HC32F460 flash target; use pyOCD for download/debug."

-include $(DEPS)