.PHONY: build clean run

# Default values
RETRO68_PATH ?= ~/Projects/macintosh/Retro68-build
USE_BASILISK ?= OFF
APP_NAME ?= App

# Get absolute path by expanding tilde
ABSOLUTE_PATH := $(shell echo $(RETRO68_PATH) | sed 's:^~:$(HOME):')

# Automatically construct paths
TOOLCHAIN_PATH = $(ABSOLUTE_PATH)/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
LAUNCH_APPL = $(ABSOLUTE_PATH)/toolchain/bin/LaunchAPPL

# Configure CMake options
CMAKE_OPTS = -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN_PATH) \
             -DUSE_MINIVMAC=$(shell if [ "$(USE_BASILISK)" = "ON" ]; then echo OFF; else echo ON; fi) \
             -DAPP_NAME=$(APP_NAME) \
             -DLAUNCH_APPL=$(LAUNCH_APPL)

build:
	mkdir -p build
	cd build && cmake .. $(CMAKE_OPTS) && cmake --build .

run: build
	cd build && cmake --build . --target run

clean:
	cd build && cmake --build . --target clean || true
	rm -rf build/*

# Usage examples:
# make build RETRO68_PATH=/path/to/Retro68-build
# make run USE_BASILISK=ON
# make build APP_NAME=MyApp