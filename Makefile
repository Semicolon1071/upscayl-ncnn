# ============================================================
# Upscayl NCNN — Makefile
# Single source of truth for building on Linux and macOS.
# Run `make help` for available targets.
# ============================================================

# Load optional per-developer overrides (see .env.example)
-include .env

# --- Platform detection ---
OS    := $(shell uname -s)
ARCH  := $(shell uname -m)
NPROC := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)
DATE  := $(shell date +%Y%m%d-%H%M%S)

# --- Configurable variables (override via .env or CLI) ---
# PRESET: CMake preset name (see CMakePresets.json for full list)
#   release, debug, profile, unit-test, integration-test, sanitize
PRESET					?= release
BUILD_DIR				?= build
BUILD_DIR_PRESET		?= $(BUILD_DIR)/$(PRESET)
CMAKE_SRC_DIR			?= $(PWD)
MODEL_DIR				?= testdata/models
# MODEL_NAME is the default model name passed to the CLI (without .param/.bin extension).
MODEL_NAME				?= upscayl-standard-4x
INPUT_DIR				?= testdata/images
INPUT_FILE				?= $(INPUT_DIR)/image.webp
OUTPUT_DIR				?= testdata/output
OUTPUT_FILE				?= $(OUTPUT_DIR)/image-upscaled.webp
PARALLEL_JOBS			?= $(NPROC)
VK_VERSION				?= 1.4.341.1
VULKAN_SDK_DIR			?= vulkan-sdk
# Set USE_SYSTEM_VULKAN=1 to skip SDK download and use system-installed Vulkan packages.
USE_SYSTEM_VULKAN		?=

# Platform identifier for Lunarg SDK API (linux, mac)
ifeq ($(OS),Linux)
  VK_SDK_PLATFORM := linux
else ifeq ($(OS),Darwin)
  VK_SDK_PLATFORM := mac
endif
LOG_DIR					?= logs
LOG_DIR_PRESET			?= $(LOG_DIR)/$(PRESET)
BUILD_LOG				?= $(LOG_DIR_PRESET)/build-$(DATE).log
TEST_NAME				?= unset
TEST_LOG				?= $(LOG_DIR_PRESET)/test-$(TEST_NAME)-$(DATE).log
BINARY					:= $(BUILD_DIR_PRESET)/src/upscayl-bin

# Compiler defaults — only override Make's built-in CC/CXX, not user-provided values.
# $(origin VAR) returns "default" for Make's built-in, "command line" for CLI,
# "file" for Makefile/.env, "environment" for env vars.
ifeq ($(OS),Linux)
  ifeq ($(origin CC),default)
    CC := $(shell command -v gcc-9 >/dev/null 2>&1 && echo gcc-9 || echo gcc)
  endif
  ifeq ($(origin CXX),default)
    CXX := $(shell command -v g++-9 >/dev/null 2>&1 && echo g++-9 || echo g++)
  endif
endif
ifeq ($(OS),Darwin)
  LIBOMP_PREFIX ?= $(shell brew --prefix libomp 2>/dev/null)
endif

# --- Auto-detect VULKAN_SDK from downloaded SDK ---
# $(wildcard ...) is evaluated at Makefile parse time, so a fresh `make check`
# that downloads the SDK will not be visible until the next make invocation.
# This is intentional — `check` is a one-time setup step, not a build prerequisite.
ifndef VULKAN_SDK
  ifeq ($(USE_SYSTEM_VULKAN),)
    ifeq ($(OS),Linux)
      _VK_SDK_CANDIDATE := $(PWD)/$(VULKAN_SDK_DIR)/$(VK_VERSION)/x86_64
      ifneq ($(wildcard $(_VK_SDK_CANDIDATE)/bin/glslangValidator),)
        VULKAN_SDK := $(_VK_SDK_CANDIDATE)
      endif
    else ifeq ($(OS),Darwin)
      _VK_SDK_CANDIDATE := $(PWD)/$(VULKAN_SDK_DIR)
      ifneq ($(wildcard $(_VK_SDK_CANDIDATE)/macOS/include/vulkan/vulkan.h),)
        VULKAN_SDK := $(_VK_SDK_CANDIDATE)
      endif
    endif
  endif
endif

# --- Environment variable bridge ---
# CMake presets read these env vars for compiler, SDK, and platform detection.
# CMakeLists.txt uses them for platform-specific defaults (macOS OpenMP, MoltenVK).
export CC CXX ARCH
ifdef VULKAN_SDK
  export VULKAN_SDK
endif
ifeq ($(OS),Darwin)
  export LIBOMP_PREFIX
endif

# ============================================================
# Targets
# ============================================================

.DEFAULT_GOAL := help
.PHONY: help info check install-deps install-vulkan-sdk check-vk-update submodules \
        configure _build build-default build rebuild debug profile \
        check-model-dir check-model-file check-input-dir check-input-file \
        ensure-output-dir ensure-log-dir setup-test \
        _test-file test-file _test-folder test-folder test-file-profile \
        _unit-test unit-test _integration-test integration-test \
        _sanitize-test sanitize-test \
        clean clean-build clean-logs clean-output clean-deps clean-all

help:
	@echo "Upscayl NCNN build targets:"
	@echo ""
	@echo "  make                       Show help message (default)"
	@echo "  make help                  Show help message (default)"
	@echo "  make info                  Print detected platform, compilers, and paths"
	@echo "  make check                 Install deps, download SDK, verify tools"
	@echo "  make install-deps          Install system packages"
	@echo "  make install-vulkan-sdk    Download pinned Vulkan SDK (idempotent)"
	@echo "  make check-vk-update       Check for newer Vulkan SDK versions"
	@echo "  make submodules            Initialize/update git submodules"
	@echo "  make configure             Run cmake --preset (default: release)"
	@echo "  make build                 Configure and build (default: release)"
	@echo "  make rebuild               Clean current preset and build"
	@echo "  make debug                 Build with PRESET=debug"
	@echo "  make profile               Build with PRESET=profile (NCNN_BENCHMARK)"
	@echo "  make test-file             Upscale a single test image"
	@echo "  make test-folder           Upscale a folder of test images"
	@echo "  make test-file-profile     Run test image with profiling build"
	@echo "  make unit-test             Build and run Catch2 unit tests"
	@echo "  make integration-test      Run CLI integration tests"
	@echo "  make sanitize-test         Build and run tests with ASan + UBSan"
	@echo "  make clean                 Remove build/logs for current preset"
	@echo "  make clean-all             Remove build, logs, output, and downloaded SDK"
	@echo ""
	@echo "CMake presets (see CMakePresets.json):"
	@echo "  release, debug, profile, unit-test, integration-test, sanitize"
	@echo ""
	@echo "Override variables via CLI or .env file (see .env.example):"
	@echo "  make build PRESET=debug"
	@echo "  make build CC=gcc-12 CXX=g++-12"
	@echo "  make build VULKAN_SDK=/path/to/sdk"
	@echo "  make check USE_SYSTEM_VULKAN=1       (skip SDK, use system packages)"

info:
	@echo "OS:                $(OS)"
	@echo "ARCH:              $(ARCH)"
	@echo "CC:                $(CC)"
	@echo "CXX:               $(CXX)"
	@echo "PRESET:            $(PRESET)"
	@echo "BUILD_DIR_PRESET:  $(BUILD_DIR_PRESET)"
	@echo "MODEL_DIR:         $(MODEL_DIR)"
	@echo "MODEL_NAME:        $(MODEL_NAME)"
	@echo "INPUT_FILE:        $(INPUT_FILE)"
	@echo "OUTPUT_DIR:        $(OUTPUT_DIR)"
	@echo "PARALLEL_JOBS:     $(PARALLEL_JOBS)"
	@echo "USE_SYSTEM_VULKAN: $(or $(USE_SYSTEM_VULKAN),(not set — using SDK))"
	@echo "VK_VERSION:        $(VK_VERSION)"
	@echo "VULKAN_SDK:        $(or $(VULKAN_SDK),(not set))"
	@echo "LOG_DIR_PRESET:    $(LOG_DIR_PRESET)"
	@echo "BUILD_LOG:         $(BUILD_LOG)"
	@echo "TEST_LOG:          $(TEST_LOG)"
ifeq ($(OS),Darwin)
	@echo "LIBOMP_PREFIX:     $(or $(LIBOMP_PREFIX),(not found))"
endif
	@echo "BINARY:            $(BINARY)"

# --- Dependency Management ---

submodules:
	@if [ ! -f src/ncnn/CMakeLists.txt ] || [ ! -f src/libwebp/CMakeLists.txt ]; then \
	  echo "Initializing git submodules..."; \
	  git submodule update --init --recursive; \
	else \
	  echo "Submodules already initialized."; \
	fi

install-deps:
ifeq ($(OS),Linux)
	@if command -v dnf >/dev/null 2>&1; then \
	  PKGS="cmake gcc gcc-c++ libstdc++-static libomp-devel libasan libubsan"; \
	  if [ -n "$(USE_SYSTEM_VULKAN)" ]; then \
	    PKGS="$$PKGS vulkan-loader-devel glslang"; \
	  fi; \
	  sudo dnf install -y $$PKGS; \
	elif command -v apt >/dev/null 2>&1; then \
	  PKGS="build-essential cmake libomp-dev libasan8 libubsan1"; \
	  if [ -n "$(USE_SYSTEM_VULKAN)" ]; then \
	    PKGS="$$PKGS libvulkan-dev glslang-tools"; \
	  fi; \
	  sudo apt update && sudo apt install -y $$PKGS; \
	else \
	  echo "Unsupported Linux package manager."; exit 1; \
	fi
else ifeq ($(OS),Darwin)
	@PKGS="cmake libomp"; \
	if [ -n "$(USE_SYSTEM_VULKAN)" ]; then \
	  PKGS="$$PKGS glslang"; \
	fi; \
	brew install $$PKGS
else
	@echo "Unsupported platform: $(OS)"; exit 1
endif

check: install-deps install-vulkan-sdk
	@FAIL=0; \
	command -v cmake >/dev/null 2>&1 \
	  || { echo "FAIL: cmake not found"; FAIL=1; }; \
	command -v $(CC) >/dev/null 2>&1 \
	  || { echo "FAIL: $(CC) not found"; FAIL=1; }; \
	command -v $(CXX) >/dev/null 2>&1 \
	  || { echo "FAIL: $(CXX) not found"; FAIL=1; }; \
	echo 'int main(){}' | $(CXX) -static-libstdc++ -x c++ -o /dev/null - 2>/dev/null \
	  || { echo "FAIL: libstdc++ static linking unavailable"; FAIL=1; }; \
	GLSLANG=""; \
	if [ -n "$(VULKAN_SDK)" ] && [ -x "$(VULKAN_SDK)/bin/glslangValidator" ]; then \
	  GLSLANG="$(VULKAN_SDK)/bin/glslangValidator"; \
	elif command -v glslangValidator >/dev/null 2>&1; then \
	  GLSLANG=$$(command -v glslangValidator); \
	else \
	  echo "FAIL: glslangValidator not found"; FAIL=1; \
	fi; \
	{ [ -n "$(VULKAN_SDK)" ] || pkg-config --exists vulkan 2>/dev/null; } \
	  || { echo "FAIL: Vulkan not found"; FAIL=1; }; \
	if [ "$(OS)" = "Darwin" ]; then \
	  { [ -n "$(LIBOMP_PREFIX)" ] && [ -d "$(LIBOMP_PREFIX)" ]; } \
	    || { echo "FAIL: OpenMP not found"; FAIL=1; }; \
	else \
	  { [ -f /usr/lib/libomp.so ] || [ -f /usr/lib/x86_64-linux-gnu/libomp.so ] || [ -f /usr/lib64/libomp.so ] \
	    || dpkg -s libomp-dev >/dev/null 2>&1 || rpm -q libomp-devel >/dev/null 2>&1; } \
	    || { echo "FAIL: OpenMP not found"; FAIL=1; }; \
	fi; \
	if [ $$FAIL -ne 0 ]; then \
	  echo "check failed: install-deps is missing packages for this platform."; exit 1; \
	fi; \
	echo ""; \
	echo "Build tools:"; \
	printf "  %-16s %s\n" "cmake" "$$(cmake --version | head -1 | awk '{print $$3}')"; \
	printf "  %-16s %s\n" "$(CC)" "$$($(CC) --version | head -1)"; \
	printf "  %-16s %s\n" "$(CXX)" "$$($(CXX) --version | head -1)"; \
	if [ -n "$$GLSLANG" ]; then \
	  GLSLANG_VER=$$($$GLSLANG --version 2>&1 | head -1); \
	  printf "  %-16s %s  (%s)\n" "glslangValidator" "$$GLSLANG_VER" "$$GLSLANG"; \
	fi; \
	if [ -n "$(VULKAN_SDK)" ]; then \
	  printf "  %-16s %s\n" "Vulkan" "SDK $(VK_VERSION)  ($(VULKAN_SDK))"; \
	else \
	  printf "  %-16s %s\n" "Vulkan" "system packages"; \
	fi

# --- Download Vulkan SDK into repo ---
# Idempotent: skips download if SDK already present at the pinned version.
# Self-gating: no-op when USE_SYSTEM_VULKAN is set.

install-vulkan-sdk:
ifneq ($(USE_SYSTEM_VULKAN),)
	@echo "USE_SYSTEM_VULKAN is set — skipping SDK download."
else ifeq ($(OS),Linux)
	@if [ "$(ARCH)" != "x86_64" ]; then \
	  echo "Error: Lunarg SDK only provides x86_64 Linux tarballs (detected: $(ARCH))."; \
	  echo "Use USE_SYSTEM_VULKAN=1 with system packages instead."; exit 1; \
	fi; \
	SENTINEL="$(VULKAN_SDK_DIR)/$(VK_VERSION)/x86_64/bin/glslangValidator"; \
	if [ -x "$$SENTINEL" ]; then \
	  echo "Vulkan SDK $(VK_VERSION) already present."; exit 0; \
	fi; \
	set -e; \
	URL="https://sdk.lunarg.com/sdk/download/$(VK_VERSION)/linux/vulkansdk-linux-x86_64-$(VK_VERSION).tar.xz?Human=true"; \
	echo "Downloading Vulkan SDK $(VK_VERSION) to $(VULKAN_SDK_DIR)/..."; \
	echo "  $$URL"; \
	mkdir -p $(VULKAN_SDK_DIR); \
	wget -q --show-progress "$$URL" -O $(VULKAN_SDK_DIR)/vk.tar.xz; \
	echo "Extracting..."; \
	tar xf $(VULKAN_SDK_DIR)/vk.tar.xz -C $(VULKAN_SDK_DIR); \
	rm $(VULKAN_SDK_DIR)/vk.tar.xz; \
	echo "Vulkan SDK $(VK_VERSION) installed to $(VULKAN_SDK_DIR)/$(VK_VERSION)/x86_64"
else ifeq ($(OS),Darwin)
	@SENTINEL="$(VULKAN_SDK_DIR)/macOS/include/vulkan/vulkan.h"; \
	if [ -f "$$SENTINEL" ]; then \
	  echo "Vulkan SDK already present."; exit 0; \
	fi; \
	set -e; \
	URL="https://sdk.lunarg.com/sdk/download/$(VK_VERSION)/mac/vulkansdk-macos-$(VK_VERSION).dmg?Human=true"; \
	echo "Downloading Vulkan SDK $(VK_VERSION) to $(VULKAN_SDK_DIR)/..."; \
	echo "  $$URL"; \
	mkdir -p $(VULKAN_SDK_DIR); \
	curl -L --progress-bar "$$URL" -o $(VULKAN_SDK_DIR)/vk.dmg; \
	echo "Installing (requires sudo)..."; \
	hdiutil attach $(VULKAN_SDK_DIR)/vk.dmg -quiet; \
	sudo /Volumes/vulkansdk-macos-$(VK_VERSION)/InstallVulkan.app/Contents/MacOS/InstallVulkan \
	  --root $$(pwd)/$(VULKAN_SDK_DIR) --accept-licenses --default-answer --confirm-command install; \
	hdiutil detach /Volumes/vulkansdk-macos-$(VK_VERSION) -quiet; \
	rm $(VULKAN_SDK_DIR)/vk.dmg; \
	echo "Vulkan SDK $(VK_VERSION) installed to $(VULKAN_SDK_DIR)/"
else
	@echo "Unsupported platform: $(OS)"; exit 1
endif

# Check for newer Vulkan SDK versions at sdk.lunarg.com.
check-vk-update:
	@LATEST=$$(curl -sf https://vulkan.lunarg.com/sdk/latest/$(VK_SDK_PLATFORM).txt) || \
	  { echo "Failed to fetch latest SDK version (network error?)"; exit 1; }; \
	echo "Pinned:  $(VK_VERSION)"; \
	echo "Latest:  $$LATEST"; \
	if [ "$(VK_VERSION)" = "$$LATEST" ]; then \
	  echo "Up to date."; \
	else \
	  echo "Update available. Edit VK_VERSION in Makefile to upgrade."; \
	fi

# --- Log header ---
# Writes runtime environment info to the start of a log file.
# The binary's own build_info.h captures compile-time parameters;
# this header captures the runtime context. A discrepancy between
# them signals a stale binary or changed environment.

define log-header
	@printf "=== Build Environment ===\n" > "$(1)"; \
	printf "Date:       %s\n" "$$(date -Iseconds)" >> "$(1)"; \
	printf "Preset:     %s\n" "$(PRESET)" >> "$(1)"; \
	printf "OS:         %s %s\n" "$(OS)" "$(ARCH)" >> "$(1)"; \
	printf "CC:         %s\n" "$(CC)" >> "$(1)"; \
	printf "CXX:        %s\n" "$(CXX)" >> "$(1)"; \
	printf "VULKAN_SDK: %s\n" "$(or $(VULKAN_SDK),(system))" >> "$(1)"; \
	printf "VK_VERSION: %s\n" "$(VK_VERSION)" >> "$(1)"; \
	printf "Binary:     %s\n" "$(BINARY)" >> "$(1)"; \
	printf "===\n\n" >> "$(1)"
endef

# --- Build ---

# cmake --preset reads configuration from CMakePresets.json.
# The preset sets build type, build directory, cache variables, etc.
# Environment variables (CC, CXX, VULKAN_SDK, ARCH, LIBOMP_PREFIX) are exported
# above so CMakeLists.txt can use them for platform-specific defaults.
configure: submodules
	@echo "Configuring with preset '$(PRESET)'..."
	cmake --preset $(PRESET)
	@echo "Configuration complete."

_build: configure
	@echo "Building with preset '$(PRESET)'..."
	cmake --build --preset $(PRESET) -j $(PARALLEL_JOBS)
	@echo "Built: $(BINARY)"

build-default: ensure-log-dir
	@echo "Logging build to: $(BUILD_LOG)"
	$(call log-header,$(BUILD_LOG))
	$(MAKE) _build 2>&1 | tee -a "$(BUILD_LOG)"
	@echo "Build log saved: $(BUILD_LOG)"

build: build-default

rebuild: clean build

# Convenience aliases — each uses a named preset from CMakePresets.json.
debug:
	$(MAKE) build PRESET=debug

profile:
	$(MAKE) build PRESET=profile

# --- Test ---

check-model-dir:
	@if [ ! -d "${MODEL_DIR}" ] || [ -z "$$(ls "${MODEL_DIR}/" 2>/dev/null)" ]; then \
	  echo "Error: "${MODEL_DIR}" directory is empty or missing."; \
	  echo "Place model files (.param and .bin) in "${MODEL_DIR}" before testing."; \
	  exit 1; \
	fi

check-model-file: check-model-dir
	@if [ ! -f "${MODEL_DIR}/${MODEL_NAME}.param" ] || [ ! -f "${MODEL_DIR}/${MODEL_NAME}.bin" ]; then \
	  echo "Error: Model files for the default model name are missing."; \
	  echo "Expected: ${MODEL_DIR}/${MODEL_NAME}.param and ${MODEL_DIR}/${MODEL_NAME}.bin"; \
	  echo "Place the correct model files in ${MODEL_DIR} or update MODEL_NAME in .env"; \
	  exit 1; \
	fi

check-input-dir:
	@if [ ! -d "${INPUT_DIR}" ] || [ -z "$$(ls "${INPUT_DIR}" 2>/dev/null)" ]; then \
	  echo "Error: Input directory "${INPUT_DIR}" is empty or missing."; \
	  echo "Place image files in "${INPUT_DIR}" or update INPUT_DIR in .env"; \
	  exit 1; \
	fi

check-input-file: check-input-dir
	@if [ ! -f "${INPUT_FILE}" ]; then \
	  echo "Error: Input file "${INPUT_FILE}" is missing."; \
	  echo "Place an image file at "${INPUT_FILE}" or update INPUT_FILE in .env"; \
	  exit 1; \
	fi

ensure-output-dir:
	@mkdir -p $(OUTPUT_DIR)

ensure-log-dir:
	@mkdir -p $(LOG_DIR_PRESET)

setup-test: check-model-file check-input-file ensure-output-dir ensure-log-dir
	@echo "Test setup complete."
	@echo "Model: ${MODEL_NAME}"
	@echo "Input: ${INPUT_FILE}"
	@echo "Output: ${OUTPUT_FILE}"

_test-file: setup-test build
	@echo "Logging to: $(TEST_LOG)"
	$(call log-header,$(TEST_LOG))
	{ time $(BINARY) \
		-i $(INPUT_FILE) \
		-o $(OUTPUT_FILE) \
		-m $(MODEL_DIR) \
		-n $(MODEL_NAME) \
		-c 0 \
		-s 4; \
	} 2>&1 | tee -a "$(TEST_LOG)"

test-file:
	@echo "Testing single file upscale..."; \
	$(MAKE) _test-file TEST_NAME=test-file

_test-folder: setup-test build
	@echo "Testing folder upscale..."
	$(call log-header,$(TEST_LOG))
	{ time $(BINARY) \
		-i $(INPUT_DIR) \
		-o $(OUTPUT_DIR) \
		-m $(MODEL_DIR) \
		-n $(MODEL_NAME) \
		-c 0 \
		-s 4; \
	} 2>&1 | tee -a "$(TEST_LOG)"

test-folder:
	@echo "Testing folder upscale..."; \
	$(MAKE) _test-folder TEST_NAME=test-folder

test-file-profile:
	@echo "Testing single file upscale with profiling..."; \
	$(MAKE) _test-file \
		TEST_NAME=test-file-profile \
		PRESET=profile

_unit-test: build
	@echo "Running unit tests..."
	@echo "Logging to: $(TEST_LOG)"
	$(call log-header,$(TEST_LOG))
	ctest --test-dir $(BUILD_DIR_PRESET) --output-on-failure 2>&1 | tee -a "$(TEST_LOG)"

unit-test:
	$(MAKE) _unit-test \
		TEST_NAME=unit-test \
		PRESET=unit-test

_integration-test: build
	@echo "Running CLI integration tests..."
	@echo "Logging to: $(TEST_LOG)"
	$(call log-header,$(TEST_LOG))
	UPSCAYL_BIN=$(BINARY) python3 tests/test_cli_integration.py 2>&1 | tee -a "$(TEST_LOG)"

integration-test:
	$(MAKE) _integration-test \
		TEST_NAME=integration-test \
		PRESET=integration-test

_sanitize-test: build
	@echo "Running sanitizer tests..."
	@echo "Logging to: $(TEST_LOG)"
	$(call log-header,$(TEST_LOG))
	ctest --test-dir $(BUILD_DIR_PRESET) --output-on-failure 2>&1 | tee -a "$(TEST_LOG)"

sanitize-test:
	$(MAKE) _sanitize-test \
		TEST_NAME=sanitize-test \
		PRESET=sanitize

# --- Clean ---

clean-build:
	rm -rf $(BUILD_DIR)

clean-logs:
	rm -rf $(LOG_DIR)

clean-output:
	rm -rf $(OUTPUT_DIR)

clean-deps:
	rm -rf $(VULKAN_SDK_DIR)

# "clean" removes the build and logs for the current PRESET only,
# preserving other presets (e.g. debug logs remain if you clean the release build).
clean:
	rm -rf $(BUILD_DIR_PRESET) $(LOG_DIR_PRESET)

clean-all: clean-build clean-logs clean-output clean-deps
