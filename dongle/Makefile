TARGET := dongle
BUILD_DIR := build

# SDK path (can be overridden from command line):
#   make SDK=E:/Works/CH585EVT/EVT/EXAM
SDK ?= E:/Works/CH585EVT/EVT/EXAM
USBHS_USER_DIR := $(SDK)/USB/USBHS/DEVICE/SimulateCDC-HID/User
USBHS_COMPAT_USER_DIR := $(SDK)/USB/USBHS/DEVICE/CompatibilityHID/User

# WCH BLE/HAL (CH58xBLE_ROM) path requires WCH RISC-V toolchain.
# Override from command line if needed, e.g. TOOLCHAIN_PREFIX=riscv-none-embed.
TOOLCHAIN_PREFIX ?= riscv32-wch-elf
CC := $(TOOLCHAIN_PREFIX)-gcc
OBJCOPY := $(TOOLCHAIN_PREFIX)-objcopy
SIZE := $(TOOLCHAIN_PREFIX)-size

LDSCRIPT := $(SDK)/SRC/Ld/Link.ld
STARTUP := $(SDK)/SRC/Startup/startup_CH585.S

INCLUDES := \
	-Iinclude \
	-I$(SDK)/BLE/LIB \
	-I$(SDK)/BLE/HAL/include \
	-I$(USBHS_USER_DIR) \
	-I$(USBHS_COMPAT_USER_DIR) \
	-I$(SDK)/SRC/StdPeriphDriver/inc \
	-I$(SDK)/SRC/RVMSIS

CPU_FLAGS := -march=rv32imac -mabi=ilp32
COMMON_FLAGS := -Os -g3 -ffunction-sections -fdata-sections -Wall -Wextra
CFLAGS := $(CPU_FLAGS) $(COMMON_FLAGS) $(INCLUDES) -DCH585 -DCH58xBLE_ROM
ASFLAGS := $(CPU_FLAGS) $(INCLUDES) -x assembler-with-cpp
LDFLAGS := $(CPU_FLAGS) -T$(LDSCRIPT) -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/$(TARGET).map -nostartfiles
SDK_LIBS := $(SDK)/SRC/StdPeriphDriver/libISP585.a
BLE_LIB := $(SDK)/BLE/LIB/libCH58xBLE.a

OBJS :=
OBJS += $(BUILD_DIR)/sdk/CH58x_gpio.o
OBJS += $(BUILD_DIR)/sdk/CH58x_adc.o
OBJS += $(BUILD_DIR)/sdk/CH58x_timer0.o
OBJS += $(BUILD_DIR)/sdk/CH58x_timer2.o
OBJS += $(BUILD_DIR)/sdk/CH58x_clk.o
OBJS += $(BUILD_DIR)/sdk/CH58x_usbdev.o
OBJS += $(BUILD_DIR)/sdk/CH58x_uart0.o
OBJS += $(BUILD_DIR)/sdk/CH58x_uart2.o
OBJS += $(BUILD_DIR)/sdk/CH58x_sys.o
OBJS += $(BUILD_DIR)/sdk/startup_CH585.o
OBJS += $(BUILD_DIR)/sdk/BLE_HAL_MCU.o
OBJS += $(BUILD_DIR)/sdk/BLE_HAL_RTC.o
OBJS += $(BUILD_DIR)/user/main.o
OBJS += $(BUILD_DIR)/user/dongle_fsm.o
OBJS += $(BUILD_DIR)/user/platform_ch585_stub.o
OBJS += $(BUILD_DIR)/user/rf_hw_ch58.o
OBJS += $(BUILD_DIR)/user/rf_link_stub.o
OBJS += $(BUILD_DIR)/user/rf_protocol.o
OBJS += $(BUILD_DIR)/user/UART.o
OBJS += $(BUILD_DIR)/user/log_utils.o
OBJS += $(BUILD_DIR)/user/usbd_compatibility_hid_shim.o
OBJS += $(BUILD_DIR)/usbhs/ch585_usbhs_device.o
OBJS += $(BUILD_DIR)/usbhs/usb_desc.o

.PHONY: all clean info

all: info $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

info:
	@echo SDK=$(SDK)
	@echo LINKER=$(LDSCRIPT)
	@echo USB_EXAMPLE=$(USBHS_USER_DIR)

$(BUILD_DIR):
	@if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"

$(BUILD_DIR)/sdk: | $(BUILD_DIR)
	@if not exist "$(BUILD_DIR)/sdk" mkdir "$(BUILD_DIR)/sdk"

$(BUILD_DIR)/user: | $(BUILD_DIR)
	@if not exist "$(BUILD_DIR)/user" mkdir "$(BUILD_DIR)/user"

$(BUILD_DIR)/usbhs: | $(BUILD_DIR)
	@if not exist "$(BUILD_DIR)/usbhs" mkdir "$(BUILD_DIR)/usbhs"

$(BUILD_DIR)/sdk/CH58x_gpio.o: $(SDK)/SRC/StdPeriphDriver/CH58x_gpio.c | $(BUILD_DIR)/sdk
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/CH58x_adc.o: $(SDK)/SRC/StdPeriphDriver/CH58x_adc.c | $(BUILD_DIR)/sdk
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/CH58x_timer0.o: $(SDK)/SRC/StdPeriphDriver/CH58x_timer0.c | $(BUILD_DIR)/sdk
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/CH58x_timer2.o: $(SDK)/SRC/StdPeriphDriver/CH58x_timer2.c | $(BUILD_DIR)/sdk
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/CH58x_clk.o: $(SDK)/SRC/StdPeriphDriver/CH58x_clk.c | $(BUILD_DIR)/sdk
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/CH58x_usbdev.o: $(SDK)/SRC/StdPeriphDriver/CH58x_usbdev.c | $(BUILD_DIR)/sdk
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/CH58x_uart0.o: $(SDK)/SRC/StdPeriphDriver/CH58x_uart0.c | $(BUILD_DIR)/sdk
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/CH58x_uart2.o: $(SDK)/SRC/StdPeriphDriver/CH58x_uart2.c | $(BUILD_DIR)/sdk
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/CH58x_sys.o: $(SDK)/SRC/StdPeriphDriver/CH58x_sys.c | $(BUILD_DIR)/sdk
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/startup_CH585.o: $(STARTUP) | $(BUILD_DIR)/sdk
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/BLE_HAL_MCU.o: $(SDK)/BLE/HAL/MCU.c | $(BUILD_DIR)/sdk
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/BLE_HAL_RTC.o: $(SDK)/BLE/HAL/RTC.c | $(BUILD_DIR)/sdk
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/user/main.o: src/main_official_1slog.c | $(BUILD_DIR)/user
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/user/dongle_fsm.o: src/dongle_fsm.c | $(BUILD_DIR)/user
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/user/platform_ch585_stub.o: src/platform_ch585_stub.c | $(BUILD_DIR)/user
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/user/rf_hw_ch58.o: src/rf_hw_ch58.c | $(BUILD_DIR)/user
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/user/rf_link_stub.o: src/rf_link_stub.c | $(BUILD_DIR)/user
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/user/rf_protocol.o: src/rf_protocol.c | $(BUILD_DIR)/user
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/user/UART.o: $(USBHS_USER_DIR)/UART.c | $(BUILD_DIR)/user
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/user/log_utils.o: src/log_utils.c | $(BUILD_DIR)/user
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/user/usbd_compatibility_hid_shim.o: src/usbd_compatibility_hid_shim.c | $(BUILD_DIR)/user
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/usbhs/ch585_usbhs_device.o: src/ch585_usbhs_device_sdkpatch.c | $(BUILD_DIR)/usbhs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/usbhs/usb_desc.o: src/usb_desc_xinput.c | $(BUILD_DIR)/usbhs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) $(OBJS) $(BLE_LIB) $(SDK_LIBS) -o $@
	$(SIZE) $@

$(BUILD_DIR)/$(TARGET).hex: $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

clean:
	@if exist "$(BUILD_DIR)" rmdir /s /q "$(BUILD_DIR)"
