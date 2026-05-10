.PHONY: all both tx rx clean clean-tx clean-rx

PREFIX ?= riscv32-wch-elf-
TOOLCHAIN_BIN ?=
SDK_ROOT ?= E:/Works/CH585EVT/EVT/EXAM

# Default target: build both products.
all: both

both:
	"$(MAKE)" -C TX PREFIX=$(PREFIX) TOOLCHAIN_BIN="$(TOOLCHAIN_BIN)" SDK_ROOT=$(SDK_ROOT) all
	"$(MAKE)" -C RX PREFIX=$(PREFIX) TOOLCHAIN_BIN="$(TOOLCHAIN_BIN)" SDK_ROOT=$(SDK_ROOT) all

tx:
	"$(MAKE)" -C TX PREFIX=$(PREFIX) TOOLCHAIN_BIN="$(TOOLCHAIN_BIN)" SDK_ROOT=$(SDK_ROOT) all

rx:
	"$(MAKE)" -C RX PREFIX=$(PREFIX) TOOLCHAIN_BIN="$(TOOLCHAIN_BIN)" SDK_ROOT=$(SDK_ROOT) all

clean: clean-tx clean-rx

clean-tx:
	"$(MAKE)" -C TX clean

clean-rx:
	"$(MAKE)" -C RX clean
