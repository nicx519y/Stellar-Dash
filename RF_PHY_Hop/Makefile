.PHONY: all both tx rx clean clean-tx clean-rx

PREFIX ?= riscv32-wch-elf-
TOOLCHAIN_BIN ?=
SDK_ROOT ?= E:/Works/CH585EVT/EVT/EXAM
SERIAL_LOG ?= 0
JOBS ?= $(NUMBER_OF_PROCESSORS)
ifeq ($(strip $(JOBS)),)
JOBS := 8
endif
SUBMAKE := make.exe -j$(JOBS)

# Default target: build both products.
all: both

both:
	+$(SUBMAKE) -C TX PREFIX=$(PREFIX) TOOLCHAIN_BIN="$(TOOLCHAIN_BIN)" SDK_ROOT=$(SDK_ROOT) SERIAL_LOG=$(SERIAL_LOG) all
	+$(SUBMAKE) -C RX PREFIX=$(PREFIX) TOOLCHAIN_BIN="$(TOOLCHAIN_BIN)" SDK_ROOT=$(SDK_ROOT) SERIAL_LOG=$(SERIAL_LOG) all

tx:
	+$(SUBMAKE) -C TX PREFIX=$(PREFIX) TOOLCHAIN_BIN="$(TOOLCHAIN_BIN)" SDK_ROOT=$(SDK_ROOT) SERIAL_LOG=$(SERIAL_LOG) all

rx:
	+$(SUBMAKE) -C RX PREFIX=$(PREFIX) TOOLCHAIN_BIN="$(TOOLCHAIN_BIN)" SDK_ROOT=$(SDK_ROOT) SERIAL_LOG=$(SERIAL_LOG) all

clean: clean-tx clean-rx

clean-tx:
	+$(SUBMAKE) -C TX clean

clean-rx:
	+$(SUBMAKE) -C RX clean
