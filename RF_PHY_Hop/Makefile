.PHONY: all both tx rx clean clean-tx clean-rx

PREFIX ?= riscv32-wch-elf-
TOOLCHAIN_BIN ?=
SDK_ROOT ?= E:/Works/CH585EVT/EVT/EXAM
JOBS ?= $(NUMBER_OF_PROCESSORS)
ifeq ($(strip $(JOBS)),)
JOBS := 8
endif
SUBMAKE := make.exe -j$(JOBS)

# Default target: build both products.
all: both

both:
	+$(SUBMAKE) -C TX PREFIX=$(PREFIX) TOOLCHAIN_BIN="$(TOOLCHAIN_BIN)" SDK_ROOT=$(SDK_ROOT) all
	+$(SUBMAKE) -C RX PREFIX=$(PREFIX) TOOLCHAIN_BIN="$(TOOLCHAIN_BIN)" SDK_ROOT=$(SDK_ROOT) all

tx:
	+$(SUBMAKE) -C TX PREFIX=$(PREFIX) TOOLCHAIN_BIN="$(TOOLCHAIN_BIN)" SDK_ROOT=$(SDK_ROOT) all

rx:
	+$(SUBMAKE) -C RX PREFIX=$(PREFIX) TOOLCHAIN_BIN="$(TOOLCHAIN_BIN)" SDK_ROOT=$(SDK_ROOT) all

clean: clean-tx clean-rx

clean-tx:
	+$(SUBMAKE) -C TX clean

clean-rx:
	+$(SUBMAKE) -C RX clean
