# Macintosh AI - Classic Mac OS (68k) build.
#
# Everything builds inside the Retro68 image; see README.md. The one-liner
# (Apple's Universal Interfaces are mounted read-only for the OT headers):
#
#   docker run --rm -v "$PWD:/root/work" \
#     -v "$HOME/Documents/SendToMacOS9:/ial:ro" -w /root/work \
#     -e APP_VERSION_STR="$(git describe --tags --always --dirty)" \
#     ghcr.io/autc04/retro68 bash -lc \
#     'make build IAL_DIR="/ial/MPW/MPW-GM+PR+Final Updates/Interfaces&Libraries"'

.DEFAULT_GOAL := build

.PHONY: build deps universal universal-force check-toolchain \
        debug-build run debug-run clean format lint

APP_NAME ?= MacintoshAI
RETRO68_PATH ?= /Retro68-build
NET68K_TRANSPORT ?= ot

# Apple's Universal Interfaces & Libraries (MPW GM): the only source of the
# 68k Open Transport headers and libraries. Override with IAL_DIR=...
IAL_DIR ?= $(HOME)/Documents/SendToMacOS9/MPW/MPW-GM+PR+Final Updates/Interfaces&Libraries

TOOLCHAIN := $(RETRO68_PATH)/toolchain
TOOLCHAIN_PREFIX := $(TOOLCHAIN)/m68k-apple-macos
TOOLCHAIN_FILE := $(TOOLCHAIN_PREFIX)/cmake/retro68.toolchain.cmake
UNIVERSAL_DIR := $(TOOLCHAIN)/universal
LAUNCH_APPL := $(RETRO68_PATH)/toolchain/bin/LaunchAPPL
STAGE_SCRIPT := $(RETRO68_PATH)/bin/interfaces-and-libraries.sh

APP_VERSION_STR ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)

CMAKE_OPTS = -DCMAKE_TOOLCHAIN_FILE="$(TOOLCHAIN_FILE)" \
             -DAPP_NAME="$(APP_NAME)" \
             -DAPP_VERSION_STR="$(APP_VERSION_STR)" \
             -DNET68K_TRANSPORT="$(NET68K_TRANSPORT)" \
             -DNET68K_UNIVERSAL_DIR="$(UNIVERSAL_DIR)" \
             -DLAUNCH_APPL="$(LAUNCH_APPL)"

check-toolchain:
	@test -f "$(TOOLCHAIN_FILE)" || { \
		echo "error: Retro68 toolchain not found at $(TOOLCHAIN_FILE)"; \
		echo "       set RETRO68_PATH to your Retro68-build directory (default /Retro68-build)."; \
		exit 1; \
	}

# Stage Apple's Universal Interfaces into $(TOOLCHAIN)/universal, then put the
# toolchain's global include path back on the open-source Multiversal headers
# so only libs/net68k's Open Transport translation unit sees Apple's headers.
#
# The second invocation is essential: the first one leaves Apple's headers
# linked into $(TOOLCHAIN_PREFIX)/include, which would shadow Multiversal for
# every source file in src/.
# Stage Apple's Universal Interfaces & Libraries into the toolchain and link
# them in globally, which is what the whole application builds against.
#
# Not `--multiversal` afterwards: Retro68's bundled Multiversal set has only 44
# headers and is missing Controls.h, Lists.h, Scrap.h, Script.h and Threads.h,
# all of which this application and the engine's HttpClient.cpp include. The
# staging script links Universal into $(TOOLCHAIN_PREFIX)/include and
# .../lib, replacing the Multiversal symlinks; setting up the toolchain that
# way is what Retro68's own build-toolchain.sh does when Apple's interfaces are
# supplied.
universal: check-toolchain
	@if [ -f "$(UNIVERSAL_DIR)/CIncludes/OpenTransport.h" ] && \
	    [ -f "$(UNIVERSAL_DIR)/lib68k/libOpenTptInet.a" ] && \
	    [ -f "$(TOOLCHAIN_PREFIX)/include/Controls.h" ]; then \
		echo "Universal Interfaces already staged in $(UNIVERSAL_DIR)"; \
	else \
		test -d "$(IAL_DIR)" || { \
			echo "error: Apple's Universal Interfaces not found at:"; \
			echo "       $(IAL_DIR)"; \
			echo "       set IAL_DIR to the Interfaces&Libraries directory"; \
			exit 1; \
		}; \
		"$(STAGE_SCRIPT)" "$(TOOLCHAIN)" "$(IAL_DIR)" true false false; \
	fi
	@test -f "$(UNIVERSAL_DIR)/CIncludes/OpenTransport.h" || { \
		echo "error: $(UNIVERSAL_DIR)/CIncludes/OpenTransport.h is missing"; \
		exit 1; \
	}
	@test -f "$(UNIVERSAL_DIR)/lib68k/libOpenTptInet.a" || { \
		echo "error: $(UNIVERSAL_DIR)/lib68k/libOpenTptInet.a is missing"; \
		exit 1; \
	}
	@test -f "$(TOOLCHAIN_PREFIX)/include/Controls.h" || { \
		echo "error: staging left the toolchain on Multiversal headers (no Controls.h)"; \
		exit 1; \
	}

# Re-stage even when a previous staging is present.
universal-force: check-toolchain
	@test -d "$(IAL_DIR)" || { \
		echo "error: Apple's Universal Interfaces not found at:"; \
		echo "       $(IAL_DIR)"; \
		exit 1; \
	}
	"$(STAGE_SCRIPT)" "$(TOOLCHAIN)" "$(IAL_DIR)" true false false

deps: universal

build: check-toolchain deps
	cmake -S . -B build $(CMAKE_OPTS)
	cmake --build build
	mkdir -p dist
	# The .APPL is an empty placeholder: the application is the .bin (MacBinary)
	# and the .dsk; the resource fork travels in the AppleDouble %$(APP_NAME).ad.
	cp build/$(APP_NAME).dsk dist/
	cp build/$(APP_NAME).bin dist/
	cp build/$(APP_NAME).APPL dist/
	cp build/$(APP_NAME).ad dist/
	cp "build/%$(APP_NAME).ad" dist/

debug-build: check-toolchain deps
	cmake -S . -B build $(CMAKE_OPTS) -DCMAKE_BUILD_TYPE=Debug
	cmake --build build
	mkdir -p dist
	cp build/$(APP_NAME).dsk dist/
	cp build/$(APP_NAME).bin dist/
	cp build/$(APP_NAME).APPL dist/
	cp build/$(APP_NAME).ad dist/
	cp "build/%$(APP_NAME).ad" dist/

run: build
	cmake --build build --target run

debug-run: debug-build
	cmake --build build --target run

clean:
	rm -rf build dist

# Format the code with clang-format.
format:
	@command -v clang-format >/dev/null 2>&1 || { echo "clang-format not found, please install it."; exit 1; }
	find src -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" | \
		xargs clang-format -i -style=file

# Run static analysis over the compilation database written by the configure
# step (no `cd build`, so a clean tree works).
lint: check-toolchain
	cmake -S . -B build $(CMAKE_OPTS) -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	@command -v clang-tidy >/dev/null 2>&1 || { echo "clang-tidy not found, nothing to do."; exit 0; }
	find src -name "*.c" -o -name "*.cpp" | while read file; do \
		echo "Analyzing $$file"; \
		clang-tidy "$$file" -p build || true; \
	done
