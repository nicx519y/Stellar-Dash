PROJECT := RF_PHY_Hop_TX
OUT_DIR := build_tx
MODE_DEFINE := -DRF_HOP_MODE=1
BOARD ?= latest_ch585
PYTHON ?= python

ifeq ($(BOARD),latest_ch585)
BOARD_CLK_OSC32K := 0
BOARD_DEFINES := \
	-DRFM_BOARD_LATEST_CH585=1 \
	-DRFM_TX_LOG_ENABLE=0 \
	-UDEBUG
else
$(error Unsupported BOARD '$(BOARD)'; supported value: latest_ch585)
endif

# Toolchain prefix, e.g. riscv-none-embed- or riscv32-wch-elf-
PREFIX ?= riscv32-wch-elf-
TOOLCHAIN_BIN ?=
SDK_ROOT ?= E:/Works/CH585EVT/EVT/EXAM
SDK_ROOT_NORM := $(patsubst "%",%,$(patsubst %/,%,$(subst \,/,$(SDK_ROOT))))
SDK_ROOT_MAKE := $(patsubst E:/Works/%,../../../../%,$(SDK_ROOT_NORM))
BLE_SDK_DIR := $(SDK_ROOT_MAKE)/BLE
TOOLCHAIN_BIN_NORM := $(patsubst %/,%,$(subst \,/,$(TOOLCHAIN_BIN)))
TOOLCHAIN_PREFIX := $(if $(TOOLCHAIN_BIN_NORM),$(TOOLCHAIN_BIN_NORM)/$(PREFIX),$(PREFIX))

CC      := "$(TOOLCHAIN_PREFIX)gcc"
AS      := "$(TOOLCHAIN_PREFIX)gcc"
OBJCOPY := "$(TOOLCHAIN_PREFIX)objcopy"
OBJDUMP := "$(TOOLCHAIN_PREFIX)objdump"
SIZE    := "$(TOOLCHAIN_PREFIX)size"
RM      := if exist "$(subst /,\,$(OUT_DIR))" rmdir /s /q "$(subst /,\,$(OUT_DIR))"

APP_DIR      := APP
BOARD_DIR    := BOARD
USB_DIR      := USB
COMMON_INC_DIR := ../Common/include
REPO_COMMON_DIR := ../../common
HAL_DIR      := $(BLE_SDK_DIR)/HAL
LIB_DIR      := $(BLE_SDK_DIR)/LIB
SRC_ROOT_DIR := $(SDK_ROOT_MAKE)/SRC
APP_STARTUP_DIR := $(SDK_ROOT_MAKE)/IAP/APP/Startup
IAP_STARTUP_DIR := $(SDK_ROOT_MAKE)/IAP/USB_IAP/Startup
DRIVER_DIR   := $(SRC_ROOT_DIR)/StdPeriphDriver
LD_DIR       := $(SRC_ROOT_DIR)/Ld
RVMSIS_DIR   := $(SRC_ROOT_DIR)/RVMSIS
PROFILE_DIR  := $(BLE_SDK_DIR)/RF_PHY_Hop/Profile
PROFILE_INCLUDE := $(if $(wildcard $(PROFILE_DIR)/include),-I$(PROFILE_DIR)/include,)

# The default target is now a combined migration image:
#   0x0000..0x0fff  persistent SPI IAP
#   0x1000..         normal RF/USB application
# Both linker scripts are the official WCH IAP layouts supplied by the SDK.
LINKER_SCRIPT := $(SDK_ROOT_MAKE)/IAP/APP/Ld/Link.ld
IAP_LINKER_SCRIPT := $(SDK_ROOT_MAKE)/IAP/USB_IAP/Ld/Link.ld

DEFINES := \
	-DCLK_OSC32K=$(BOARD_CLK_OSC32K) \
	-DRF_8K=1 \
	$(MODE_DEFINE) \
	$(EXTRA_DEFINES) \
	$(BOARD_DEFINES)

INCLUDES := \
	-I$(APP_DIR)/include \
	-I$(BOARD_DIR) \
	-I$(USB_DIR) \
	-I$(COMMON_INC_DIR) \
	-I$(REPO_COMMON_DIR) \
	-I$(HAL_DIR)/include \
	$(PROFILE_INCLUDE) \
	-I$(DRIVER_DIR)/inc \
	-I$(RVMSIS_DIR) \
	-I$(LD_DIR) \
	-I$(LIB_DIR)

ARCH_FLAGS := \
	-march=rv32imcbxw \
	-mabi=ilp32

CFLAGS := \
	$(ARCH_FLAGS) \
	-std=gnu99 \
	-Os \
	-g \
	-ffunction-sections \
	-fdata-sections \
	-fsigned-char \
	-fmessage-length=0 \
	-fno-common \
	-Wall \
	$(DEFINES) \
	$(INCLUDES)

ASFLAGS := \
	$(ARCH_FLAGS) \
	-x assembler-with-cpp \
	$(DEFINES) \
	$(INCLUDES)

LDFLAGS := \
	$(ARCH_FLAGS) \
	-T$(LINKER_SCRIPT) \
	-nostartfiles \
	--specs=nano.specs \
	--specs=nosys.specs \
	-Wl,--gc-sections \
	-Wl,--print-memory-usage \
	-Wl,-Map,$(OUT_DIR)/$(PROJECT).map \
	-L$(LIB_DIR) \
	-L$(DRIVER_DIR)

LIBS := \
	-lISP585 \
	-lCH58xBLE

APP_SRCS := $(wildcard $(APP_DIR)/*.c)
BOARD_SRCS := $(wildcard $(BOARD_DIR)/*.c)
USB_SRCS := $(wildcard $(USB_DIR)/*.c)
COMMON_CODEC_SRC := $(REPO_COMMON_DIR)/usb_board_link_codec.c
HAL_SRCS := $(wildcard $(HAL_DIR)/*.c)
LIB_SRCS := $(wildcard $(LIB_DIR)/*.S)
DRV_SRCS := $(wildcard $(DRIVER_DIR)/*.c)

APP_EXCLUDE := \
	$(APP_DIR)/rfm_input_stream.c

# Keep consistent with current .cproject exclude rules.
HAL_EXCLUDE := \
	$(HAL_DIR)/KEY.c \
	$(HAL_DIR)/LED.c

DRV_EXCLUDE := \
	$(DRIVER_DIR)/CH59x_pwm.c \
	$(DRIVER_DIR)/CH59x_usbdev.c \
	$(DRIVER_DIR)/CH59x_usbhostClass.c \
	$(DRIVER_DIR)/CH59x_usbhostBase.c \
	$(DRIVER_DIR)/CH59x_spi0.c \
	$(DRIVER_DIR)/CH59x_timer0.c \
	$(DRIVER_DIR)/CH59x_timer1.c \
	$(DRIVER_DIR)/CH59x_timer2.c \
	$(DRIVER_DIR)/CH59x_timer3.c \
	$(DRIVER_DIR)/CH59x_uart0.c \
	$(DRIVER_DIR)/CH59x_uart2.c \
	$(DRIVER_DIR)/CH59x_uart3.c

APP_STARTUP_SRC := $(APP_STARTUP_DIR)/startup_CH585.S
IAP_STARTUP_SRC := $(IAP_STARTUP_DIR)/startup_CH585.S

LOCAL_SRCS := $(filter-out $(APP_EXCLUDE),$(APP_SRCS)) $(BOARD_SRCS) $(USB_SRCS)
HAL_BUILD_SRCS := $(filter-out $(HAL_EXCLUDE),$(HAL_SRCS))
DRV_BUILD_SRCS := $(filter-out $(DRV_EXCLUDE),$(DRV_SRCS))
SRCS := $(LOCAL_SRCS) $(HAL_BUILD_SRCS) $(LIB_SRCS) $(DRV_BUILD_SRCS) $(APP_STARTUP_SRC)
COMMON_CODEC_OBJ := $(OUT_DIR)/COMMON/usb_board_link_codec.o
LOCAL_OBJS := $(patsubst %.c,$(OUT_DIR)/%.o,$(LOCAL_SRCS))
HAL_OBJS := $(patsubst $(HAL_DIR)/%.c,$(OUT_DIR)/SDK/HAL/%.o,$(HAL_BUILD_SRCS))
LIB_OBJS := $(patsubst $(LIB_DIR)/%.S,$(OUT_DIR)/SDK/LIB/%.o,$(LIB_SRCS))
DRV_OBJS := $(patsubst $(DRIVER_DIR)/%.c,$(OUT_DIR)/SDK/DRIVER/%.o,$(DRV_BUILD_SRCS))
APP_STARTUP_OBJ := $(OUT_DIR)/SDK/APP_STARTUP/startup_CH585.o
IAP_STARTUP_OBJ := $(OUT_DIR)/SDK/IAP_STARTUP/startup_CH585.o
OBJS := $(LOCAL_OBJS) $(HAL_OBJS) $(LIB_OBJS) $(DRV_OBJS) $(APP_STARTUP_OBJ) \
        $(COMMON_CODEC_OBJ)
RF_MAIN_OBJ := $(OUT_DIR)/$(APP_DIR)/RF_main.o
RF_FREEZE_CHECK := ../../tools/check_rf_frozen.py
IAP_MAIN_OBJ := $(OUT_DIR)/IAP/iap_main.o
IAP_BOARD_OBJ := $(OUT_DIR)/$(BOARD_DIR)/board_latest_ch585.o
IAP_ELF := $(OUT_DIR)/$(PROJECT)_iap.elf
IAP_PADDED_BIN := $(OUT_DIR)/$(PROJECT)_iap_padded.bin
APP_BIN := $(OUT_DIR)/$(PROJECT)_app.bin
IAP_DRIVER_OBJS := \
	$(OUT_DIR)/SDK/DRIVER/CH58x_gpio.o \
	$(OUT_DIR)/SDK/DRIVER/CH58x_spi0.o \
	$(OUT_DIR)/SDK/DRIVER/CH58x_sys.o
IAP_OBJS := $(IAP_MAIN_OBJ) $(IAP_BOARD_OBJ) $(IAP_DRIVER_OBJS) $(IAP_STARTUP_OBJ)

DEPFLAGS = -MMD -MF $(@:.o=.d)

.PHONY: all clean print-vars check-toolchain check-rf-freeze

TOOLCHAIN_OK := $(OUT_DIR)/.toolchain_ok

all: $(TOOLCHAIN_OK) $(OUT_DIR)/$(PROJECT).elf $(OUT_DIR)/$(PROJECT).hex $(OUT_DIR)/$(PROJECT).bin $(OUT_DIR)/$(PROJECT).lst
	$(SIZE) $(OUT_DIR)/$(PROJECT).elf

check-rf-freeze:
	$(PYTHON) "$(RF_FREEZE_CHECK)" --require-rx-binary

$(TOOLCHAIN_OK):
	@if not exist "$(subst /,\,$(OUT_DIR))" mkdir "$(subst /,\,$(OUT_DIR))"
	@$(CC) --version >NUL 2>&1 || ( \
		echo [ERROR] GCC toolchain not found. & \
		echo [HINT ] 1. set PATH to your RISC-V GCC bin directory. & \
		echo [HINT ] 2. or run: make PREFIX=riscv32-wch-elf- TOOLCHAIN_BIN="C:/path/to/bin" & \
		exit /b 1 \
	)
	@echo ok > "$(subst /,\,$(TOOLCHAIN_OK))"

check-toolchain: $(TOOLCHAIN_OK)

$(OUT_DIR)/$(PROJECT).elf: $(OBJS) $(LINKER_SCRIPT)
	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
	$(CC) $(OBJS) $(LDFLAGS) $(LIBS) -o $@

$(APP_BIN): $(OUT_DIR)/$(PROJECT).elf
	$(OBJCOPY) -O binary $< $@

$(IAP_ELF): $(IAP_OBJS) $(IAP_LINKER_SCRIPT)
	$(CC) $(IAP_OBJS) $(ARCH_FLAGS) -T$(IAP_LINKER_SCRIPT) -nostartfiles \
		--specs=nano.specs --specs=nosys.specs -Wl,--gc-sections \
		-Wl,--print-memory-usage -Wl,-Map,$(OUT_DIR)/$(PROJECT)_iap.map \
		-L$(LIB_DIR) -L$(DRIVER_DIR) -lISP585 -o $@

$(IAP_PADDED_BIN): $(IAP_ELF)
	$(OBJCOPY) -O binary --gap-fill 0xFF --pad-to 0x1000 $< $@

$(OUT_DIR)/$(PROJECT).bin: $(IAP_PADDED_BIN) $(APP_BIN)
	copy /b "$(subst /,\,$(IAP_PADDED_BIN))"+"$(subst /,\,$(APP_BIN))" "$(subst /,\,$@)" >NUL

$(OUT_DIR)/$(PROJECT).hex: $(OUT_DIR)/$(PROJECT).bin
	$(OBJCOPY) -I binary -O ihex --change-addresses 0 $< $@

$(OUT_DIR)/$(PROJECT).lst: $(OUT_DIR)/$(PROJECT).elf
	$(OBJDUMP) --source --all-headers --demangle --line-numbers --wide $< > $@

$(OUT_DIR)/%.o: %.c
	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(COMMON_CODEC_OBJ): $(COMMON_CODEC_SRC)
	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OUT_DIR)/SDK/HAL/%.o: $(HAL_DIR)/%.c
	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OUT_DIR)/SDK/DRIVER/%.o: $(DRIVER_DIR)/%.c
	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OUT_DIR)/SDK/LIB/%.o: $(LIB_DIR)/%.S
	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
	$(AS) $(ASFLAGS) $(DEPFLAGS) -c $< -o $@

$(APP_STARTUP_OBJ): $(APP_STARTUP_SRC)
	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
	$(AS) $(ASFLAGS) $(DEPFLAGS) -c $< -o $@

$(IAP_STARTUP_OBJ): $(IAP_STARTUP_SRC)
	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
	$(AS) $(ASFLAGS) $(DEPFLAGS) -c $< -o $@

$(OUT_DIR)/%.o: %.S
	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
	$(AS) $(ASFLAGS) $(DEPFLAGS) -c $< -o $@

$(RF_MAIN_OBJ): CFLAGS += -Dmain=RF_FrozenMain

$(OBJS) $(IAP_MAIN_OBJ): $(TOOLCHAIN_OK)

clean:
	$(RM)

print-vars:
	@echo "PROJECT=$(PROJECT)"
	@echo "OUT_DIR=$(OUT_DIR)"
	@echo "BOARD=$(BOARD)"
	@echo "BOARD_CLK_OSC32K=$(BOARD_CLK_OSC32K)"
	@echo "SRCS=$(SRCS)"

-include $(OBJS:.o=.d)
