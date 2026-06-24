PROJECT := RF_PHY_Hop_TX
OUT_DIR := build_tx
MODE_DEFINE := -DRF_HOP_MODE=1

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
COMMON_INC_DIR := ../Common/include
HAL_DIR      := $(BLE_SDK_DIR)/HAL
LIB_DIR      := $(BLE_SDK_DIR)/LIB
SRC_ROOT_DIR := $(SDK_ROOT_MAKE)/SRC
STARTUP_DIR  := $(SRC_ROOT_DIR)/Startup
DRIVER_DIR   := $(SRC_ROOT_DIR)/StdPeriphDriver
LD_DIR       := $(SRC_ROOT_DIR)/Ld
RVMSIS_DIR   := $(SRC_ROOT_DIR)/RVMSIS
PROFILE_DIR  := $(BLE_SDK_DIR)/RF_PHY_Hop/Profile
PROFILE_INCLUDE := $(if $(wildcard $(PROFILE_DIR)/include),-I$(PROFILE_DIR)/include,)

LINKER_SCRIPT := $(LD_DIR)/Link.ld

DEFINES := \
	-DCLK_OSC32K=2 \
	-DRF_8K=1 \
	$(MODE_DEFINE) \
	$(EXTRA_DEFINES)

INCLUDES := \
	-I$(APP_DIR)/include \
	-I$(COMMON_INC_DIR) \
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

STARTUP_SRCS := $(STARTUP_DIR)/startup_CH585.S

SRCS := $(filter-out $(APP_EXCLUDE) $(HAL_EXCLUDE) $(DRV_EXCLUDE),$(APP_SRCS) $(HAL_SRCS) $(LIB_SRCS) $(DRV_SRCS) $(STARTUP_SRCS))
OBJS := $(patsubst %.c,$(OUT_DIR)/%.o,$(filter %.c,$(SRCS))) \
        $(patsubst %.S,$(OUT_DIR)/%.o,$(filter %.S,$(SRCS)))

DEPFLAGS = -MMD -MF $(@:.o=.d)

.PHONY: all clean print-vars check-toolchain

TOOLCHAIN_OK := $(OUT_DIR)/.toolchain_ok

all: $(TOOLCHAIN_OK) $(OUT_DIR)/$(PROJECT).elf $(OUT_DIR)/$(PROJECT).hex $(OUT_DIR)/$(PROJECT).bin $(OUT_DIR)/$(PROJECT).lst
	$(SIZE) $(OUT_DIR)/$(PROJECT).elf

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

$(OUT_DIR)/$(PROJECT).hex: $(OUT_DIR)/$(PROJECT).elf
	$(OBJCOPY) -O ihex $< $@

$(OUT_DIR)/$(PROJECT).bin: $(OUT_DIR)/$(PROJECT).elf
	$(OBJCOPY) -O binary $< $@

$(OUT_DIR)/$(PROJECT).lst: $(OUT_DIR)/$(PROJECT).elf
	$(OBJDUMP) --source --all-headers --demangle --line-numbers --wide $< > $@

$(OUT_DIR)/%.o: %.c
	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OUT_DIR)/%.o: %.S
	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
	$(AS) $(ASFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJS): $(TOOLCHAIN_OK)

clean:
	$(RM)

print-vars:
	@echo "PROJECT=$(PROJECT)"
	@echo "OUT_DIR=$(OUT_DIR)"
	@echo "SRCS=$(SRCS)"

-include $(OBJS:.o=.d)
