TARGET := rf_module
BUILD_DIR := build

# SDK path (override if needed):
#   make SDK=E:/Works/CH585EVT/EVT/EXAM
SDK ?= E:/Works/CH585EVT/EVT/EXAM

CC := riscv-none-embed-gcc
OBJCOPY := riscv-none-embed-objcopy
SIZE := riscv-none-embed-size

LDSCRIPT := linker/Link_CH584.ld
STARTUP := $(SDK)/SRC/Startup/startup_CH585.S

PROJECT_SRCS := \
	src/main.c \
	src/rfm_protocol.c \
	src/rfm_link.c \
	src/rfm_spi_bridge_stub.c \
	src/platform_ch584_stub.c \
	src/ch58x_startup_compat.c

INCLUDES := \
	-Iinclude \
	-I$(SDK)/SRC/StdPeriphDriver/inc \
	-I$(SDK)/SRC/RVMSIS

CPU_FLAGS := -march=rv32imac -mabi=ilp32
COMMON_FLAGS := -Os -g3 -ffunction-sections -fdata-sections -Wall -Wextra
CFLAGS := $(CPU_FLAGS) $(COMMON_FLAGS) $(INCLUDES) -DCH584
ASFLAGS := $(CPU_FLAGS) $(INCLUDES) -x assembler-with-cpp
LDFLAGS := $(CPU_FLAGS) -T$(LDSCRIPT) -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/$(TARGET).map -nostartfiles
SDK_LIBS := $(SDK)/SRC/StdPeriphDriver/libISP585.a

OBJS := $(addprefix $(BUILD_DIR)/,$(PROJECT_SRCS:.c=.o))
OBJS += $(BUILD_DIR)/sdk/CH58x_gpio.o
OBJS += $(BUILD_DIR)/sdk/CH58x_timer0.o
OBJS += $(BUILD_DIR)/sdk/CH58x_flash.o
OBJS += $(BUILD_DIR)/sdk/CH58x_spi0.o
OBJS += $(BUILD_DIR)/sdk/startup_CH585.o

.PHONY: all clean info

all: info $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

info:
	@echo SDK=$(SDK)
	@echo LINKER=$(LDSCRIPT)

$(BUILD_DIR):
	@if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"

$(BUILD_DIR)/src: | $(BUILD_DIR)
	@if not exist "$(BUILD_DIR)/src" mkdir "$(BUILD_DIR)/src"

$(BUILD_DIR)/sdk: | $(BUILD_DIR)
	@if not exist "$(BUILD_DIR)/sdk" mkdir "$(BUILD_DIR)/sdk"

$(BUILD_DIR)/src/%.o: src/%.c | $(BUILD_DIR)/src
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/CH58x_gpio.o: $(SDK)/SRC/StdPeriphDriver/CH58x_gpio.c | $(BUILD_DIR)/sdk
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/CH58x_timer0.o: $(SDK)/SRC/StdPeriphDriver/CH58x_timer0.c | $(BUILD_DIR)/sdk
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/CH58x_flash.o: $(SDK)/SRC/StdPeriphDriver/CH58x_flash.c | $(BUILD_DIR)/sdk
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/CH58x_spi0.o: $(SDK)/SRC/StdPeriphDriver/CH58x_spi0.c | $(BUILD_DIR)/sdk
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/startup_CH585.o: $(STARTUP) | $(BUILD_DIR)/sdk
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) $(OBJS) $(SDK_LIBS) -o $@
	$(SIZE) $@

$(BUILD_DIR)/$(TARGET).hex: $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

clean:
	@if exist "$(BUILD_DIR)" rmdir /s /q "$(BUILD_DIR)"
