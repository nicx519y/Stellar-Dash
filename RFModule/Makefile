TARGET := rf_module
BUILD_DIR := build

# SDK path (override if needed):
#   make SDK=E:/Works/CH585EVT/EVT/EXAM
SDK ?= E:/Works/CH585EVT/EVT/EXAM

TOOLCHAIN_PREFIX ?= riscv32-wch-elf
TOOLCHAIN_CC := $(TOOLCHAIN_PREFIX)-gcc
TOOLCHAIN_OBJCOPY := $(TOOLCHAIN_PREFIX)-objcopy
TOOLCHAIN_SIZE := $(TOOLCHAIN_PREFIX)-size

LDSCRIPT := $(SDK)/SRC/Ld/Link.ld
STARTUP := $(SDK)/SRC/Startup/startup_CH585.S

PROJECT_SRCS := \
	src/main.c \
	src/log_utils.c \
	src/RF_PHY.c

INCLUDES := \
	-Iinclude \
	-I$(SDK)/BLE/LIB \
	-I$(SDK)/BLE/HAL/include \
	-I$(SDK)/SRC/StdPeriphDriver/inc \
	-I$(SDK)/SRC/RVMSIS

CPU_FLAGS := -march=rv32imac -mabi=ilp32
COMMON_FLAGS := -Os -g3 -ffunction-sections -fdata-sections -Wall -Wextra
CFLAGS := $(CPU_FLAGS) $(COMMON_FLAGS) $(INCLUDES) -DCH584 -DCH58xBLE_ROM -DDEBUG=Debug_UART0
ASFLAGS := $(CPU_FLAGS) $(INCLUDES) -x assembler-with-cpp
LDFLAGS := $(CPU_FLAGS) -T$(LDSCRIPT) -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/$(TARGET).map -nostartfiles
SDK_LIBS := $(SDK)/SRC/StdPeriphDriver/libISP585.a
BLE_LIB := $(SDK)/BLE/LIB/libCH58xBLE.a

OBJS := $(addprefix $(BUILD_DIR)/,$(PROJECT_SRCS:.c=.o))
OBJS += $(BUILD_DIR)/sdk/CH58x_gpio.o
OBJS += $(BUILD_DIR)/sdk/CH58x_adc.o
OBJS += $(BUILD_DIR)/sdk/CH58x_clk.o
OBJS += $(BUILD_DIR)/sdk/CH58x_sys.o
OBJS += $(BUILD_DIR)/sdk/CH58x_timer0.o
OBJS += $(BUILD_DIR)/sdk/CH58x_flash.o
OBJS += $(BUILD_DIR)/sdk/CH58x_uart0.o
OBJS += $(BUILD_DIR)/sdk/startup_CH585.o
OBJS += $(BUILD_DIR)/sdk/BLE_HAL_MCU.o
OBJS += $(BUILD_DIR)/sdk/BLE_HAL_RTC.o

.PHONY: all clean info
.PHONY: toolchain-check

all: toolchain-check info $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

info:
	@echo SDK=$(SDK)
	@echo LINKER=$(LDSCRIPT)
	@echo TOOLCHAIN_CC=$(TOOLCHAIN_CC)

toolchain-check:
	@$(TOOLCHAIN_CC) --version >NUL 2>NUL || (echo [ERR] Toolchain not found: $(TOOLCHAIN_CC) & echo [HINT] Try: make TOOLCHAIN_PREFIX=riscv32-wch-elf all & echo [HINT] Or: make TOOLCHAIN_PREFIX=riscv-none-embed all & exit 2)

$(BUILD_DIR):
	@if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"

$(BUILD_DIR)/src: | $(BUILD_DIR)
	@if not exist "$(BUILD_DIR)/src" mkdir "$(BUILD_DIR)/src"

$(BUILD_DIR)/sdk: | $(BUILD_DIR)
	@if not exist "$(BUILD_DIR)/sdk" mkdir "$(BUILD_DIR)/sdk"

$(BUILD_DIR)/src/%.o: src/%.c | $(BUILD_DIR)/src
	$(TOOLCHAIN_CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/CH58x_gpio.o: $(SDK)/SRC/StdPeriphDriver/CH58x_gpio.c | $(BUILD_DIR)/sdk
	$(TOOLCHAIN_CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/CH58x_adc.o: $(SDK)/SRC/StdPeriphDriver/CH58x_adc.c | $(BUILD_DIR)/sdk
	$(TOOLCHAIN_CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/CH58x_clk.o: $(SDK)/SRC/StdPeriphDriver/CH58x_clk.c | $(BUILD_DIR)/sdk
	$(TOOLCHAIN_CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/CH58x_sys.o: $(SDK)/SRC/StdPeriphDriver/CH58x_sys.c | $(BUILD_DIR)/sdk
	$(TOOLCHAIN_CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/CH58x_timer0.o: $(SDK)/SRC/StdPeriphDriver/CH58x_timer0.c | $(BUILD_DIR)/sdk
	$(TOOLCHAIN_CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/CH58x_flash.o: $(SDK)/SRC/StdPeriphDriver/CH58x_flash.c | $(BUILD_DIR)/sdk
	$(TOOLCHAIN_CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/CH58x_uart0.o: $(SDK)/SRC/StdPeriphDriver/CH58x_uart0.c | $(BUILD_DIR)/sdk
	$(TOOLCHAIN_CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/startup_CH585.o: $(STARTUP) | $(BUILD_DIR)/sdk
	$(TOOLCHAIN_CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/BLE_HAL_MCU.o: $(SDK)/BLE/HAL/MCU.c | $(BUILD_DIR)/sdk
	$(TOOLCHAIN_CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/BLE_HAL_RTC.o: $(SDK)/BLE/HAL/RTC.c | $(BUILD_DIR)/sdk
	$(TOOLCHAIN_CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(TOOLCHAIN_CC) $(LDFLAGS) $(OBJS) $(BLE_LIB) $(SDK_LIBS) -o $@
	$(TOOLCHAIN_SIZE) $@

$(BUILD_DIR)/$(TARGET).hex: $(BUILD_DIR)/$(TARGET).elf
	$(TOOLCHAIN_OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	$(TOOLCHAIN_OBJCOPY) -O binary $< $@

clean:
	@if exist "$(BUILD_DIR)" rmdir /s /q "$(BUILD_DIR)"
