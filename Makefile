.PHONY: build clean run format lint debug debug-run

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

# Build dependencies in the correct order
build-deps: build-cjson build-mactcphelper build-mbedtls build-machttp

# Build cJSON library
build-cjson:
	@echo "Building cJSON library..."
	mkdir -p libs/cjson/build
	cd libs/cjson/build && cmake .. -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN_PATH) -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DENABLE_CJSON_UTILS=On -DENABLE_CJSON_TEST=Off -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX=$(ABSOLUTE_PATH)/toolchain/m68k-apple-macos && cmake --build . && cmake --install .
	@echo "cJSON build complete"

# Build mbedtls library
build-mbedtls:
	@echo "Building mbedtls library..."
	mkdir -p libs/mbedtls-mac-68k/build
	cd libs/mbedtls-mac-68k/build && cmake .. -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN_PATH) -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DUNSAFE_BUILD=ON -DCMAKE_INSTALL_PREFIX=$(ABSOLUTE_PATH)/toolchain/m68k-apple-macos && cmake --build . && cmake --install .
	@echo "mbedtls build complete"

# Build mactcphelper library
build-mactcphelper:
	@echo "Building MacTCPHelper library..."
	mkdir -p libs/mactcphelper/build
	cd libs/mactcphelper/build && cmake .. -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN_PATH) -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_INSTALL_PREFIX=$(ABSOLUTE_PATH)/toolchain/m68k-apple-macos && cmake --build . && cmake --install .
	@echo "MacTCPHelper build complete"

# Build machttp library (depends on mbedtls and mactcphelper)
build-machttp: build-mbedtls build-mactcphelper
	@echo "Building MacHTTP library..."
	mkdir -p libs/machttp/build
	cd libs/machttp/build && cmake .. -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN_PATH) -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DSSL_ENABLED=ON -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX=$(ABSOLUTE_PATH)/toolchain/m68k-apple-macos && cmake --build . && cmake --install .
	@echo "MacHTTP build complete"

build: build-deps
	mkdir -p build
	cd build && cmake .. $(CMAKE_OPTS) -DCMAKE_POLICY_VERSION_MINIMUM=3.5 && cmake --build .
	mkdir -p dist
	cp build/$(APP_NAME).{dsk,bin} dist

debug-build:
	mkdir -p build
	cd build && cmake .. $(CMAKE_OPTS) -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DDEBUG=ON && cmake --build .
	cp build/$(APP_NAME).{dsk,bin} dist

run: build
	cd build && cmake --build . --target run
	
debug-run: debug
	cd build && cmake --build . --target run

clean: clean-deps
	cd build && cmake --build . --target clean || true
	rm -rf build/* dist/*

clean-deps:
	@echo "Cleaning dependency builds..."
	rm -rf libs/*/build
	@echo "Dependencies cleaned."

# Format the code with clang-format
format:
	@echo "Formatting code using clang-format..."
	@command -v clang-format >/dev/null 2>&1 || { echo "clang-format not found, please install it."; exit 1; }
	@find src -path libs -prune -o \( -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" \) -print | xargs clang-format -i -style=file
	@echo "Formatting complete."

# Run static analysis
lint:
	@echo "Running static analysis..."
	
	@# Make sure we have a compilation database for clang-tidy
	@cd build && cmake .. $(CMAKE_OPTS) -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	
	if command -v clang-tidy >/dev/null 2>&1; then \
		echo "Found clang-tidy, performing deep analysis..."; \
		for file in $$(find src -path libs -prune -o \( -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" \) -print); do \
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