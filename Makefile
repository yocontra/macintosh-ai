.PHONY: build clean run format lint

# Default values
RETRO68_PATH ?= ~/Projects/macintosh/Retro68-build
USE_BASILISK ?= OFF
APP_NAME ?= App

# Get absolute path by expanding tilde
ABSOLUTE_PATH := $(shell echo $(RETRO68_PATH) | sed 's:^~:$(HOME):')

# Automatically construct paths
TOOLCHAIN_PATH = $(ABSOLUTE_PATH)/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
LAUNCH_APPL = $(ABSOLUTE_PATH)/toolchain/bin/LaunchAPPL

# Source files
SOURCES := $(shell find src -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp")

# Configure CMake options
CMAKE_OPTS = -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN_PATH) \
             -DUSE_MINIVMAC=$(shell if [ "$(USE_BASILISK)" = "ON" ]; then echo OFF; else echo ON; fi) \
             -DAPP_NAME=$(APP_NAME) \
             -DLAUNCH_APPL=$(LAUNCH_APPL)

build:
	mkdir -p build
	cd build && cmake .. $(CMAKE_OPTS) && cmake --build .
	cp build/$(APP_NAME).{dsk,bin} dist

run: build
	cd build && cmake --build . --target run

clean:
	cd build && cmake --build . --target clean || true
	rm -rf build/* dist/*

# Format the code with clang-format
format:
	@echo "Formatting code using clang-format..."
	@command -v clang-format >/dev/null 2>&1 || { echo "clang-format not found, please install it."; exit 1; }
	@find src -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i -style=file
	@echo "Formatting complete."

# Run static analysis
lint:
	@echo "Running static analysis..."
	
	@# Make sure we have a compilation database for clang-tidy
	@cd build && cmake .. $(CMAKE_OPTS) -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	
	if command -v clang-tidy >/dev/null 2>&1; then \
		echo "Found clang-tidy, performing deep analysis..."; \
		for file in $$(find src -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp"); do \
			echo "Analyzing $$file"; \
			clang-tidy "$$file" -p build; \
		done; \
		echo ""; \
	fi
	
	@echo "Static analysis complete."

# Usage examples:
# make build RETRO68_PATH=/path/to/Retro68-build
# make run USE_BASILISK=ON
# make build APP_NAME=MyApp
# make format
# make lint