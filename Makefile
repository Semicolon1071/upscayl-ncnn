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
# CMAKE_BUILD_TYPE: [Debug, Release, RelWithDebInfo, MinSizeRel]
CMAKE_BUILD_TYPE		?= Release
# CUSTOM_BUILD_OPTION: [default, profile, sanitize]
CUSTOM_BUILD_OPTION		?= default
BUILD_DIR				?= build
BUILD_DIR_TYPE_OPTION	?= $(BUILD_DIR)/$(CMAKE_BUILD_TYPE)/$(CUSTOM_BUILD_OPTION)
CMAKE_SRC_DIR			?= $(PWD)
MODEL_DIR				?= testdata/models
# MODEL_NAME is the default model name passed to the CLI (without .param/.bin extension).
MODEL_NAME				?= upscayl-standard-4x
INPUT_DIR				?= testdata/images
INPUT_FILE				?= $(INPUT_DIR)/image.webp
OUTPUT_DIR				?= testdata/output
OUTPUT_FILE				?= $(OUTPUT_DIR)/image-upscaled.webp
PARALLEL_JOBS			?= $(NPROC)
VK_VERSION				?= 1.4.328.0
VULKAN_SDK_DIR			?= vulkan-sdk
LOG_DIR					?= logs
LOG_DIR_TYPE_OPTION		?= $(LOG_DIR)/$(CMAKE_BUILD_TYPE)/$(CUSTOM_BUILD_OPTION)
BUILD_LOG				?= $(LOG_DIR_TYPE_OPTION)/build-$(DATE).log
TEST_NAME				?= unset
TEST_LOG				?= $(LOG_DIR_TYPE_OPTION)/test-$(TEST_NAME)-$(DATE).log
BINARY					:= $(BUILD_DIR_TYPE_OPTION)/src/upscayl-bin

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

# Extra cmake flags (user-provided, appended last)
CMAKE_EXTRA_FLAGS ?=

# CMake flags that are passed directory to the CXX compiler
CMAKE_CXX_FLAGS ?=

# --- CMake flag assembly ---
CMAKE_FLAGS := \
  -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) \
  -DCMAKE_C_COMPILER=$(CC) \
  -DCMAKE_CXX_COMPILER=$(CXX) \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

ifeq ($(OS),Darwin)
  CMAKE_FLAGS += \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_OSX_ARCHITECTURES=$(ARCH) \
    -DUSE_STATIC_MOLTENVK=ON

  # Cross-compilation (e.g. ARCH=arm64 on an x86_64 runner)
  HOST_ARCH := $(shell uname -m)
  ifneq ($(ARCH),$(HOST_ARCH))
    CMAKE_FLAGS += -DCMAKE_CROSSCOMPILING=ON -DCMAKE_SYSTEM_PROCESSOR=$(ARCH)
  endif

  # OpenMP (clang doesn't find it without hints)
  ifneq ($(LIBOMP_PREFIX),)
    CMAKE_FLAGS += \
      -DOpenMP_C_FLAGS="-Xclang -fopenmp -I$(LIBOMP_PREFIX)/include" \
      -DOpenMP_CXX_FLAGS="-Xclang -fopenmp -I$(LIBOMP_PREFIX)/include" \
      -DOpenMP_C_LIB_NAMES=libomp \
      -DOpenMP_CXX_LIB_NAMES=libomp \
      -DOpenMP_libomp_LIBRARY="$(LIBOMP_PREFIX)/lib/libomp.a"
  endif

  # Vulkan / MoltenVK
  ifdef VULKAN_SDK
    CMAKE_FLAGS += \
      -DVulkan_INCLUDE_DIR=$(VULKAN_SDK)/macOS/include \
      -DVulkan_LIBRARY=$(VULKAN_SDK)/macOS/lib/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a
  endif
endif

# Linux: CMake's FindVulkan reads VULKAN_SDK from the environment automatically.
# Export it so the cmake subprocess inherits it.
ifdef VULKAN_SDK
  export VULKAN_SDK
endif

CMAKE_FLAGS += $(CMAKE_EXTRA_FLAGS)
CMAKE_FLAGS += $(CMAKE_CXX_FLAGS)

# ============================================================
# Targets
# ============================================================

.DEFAULT_GOAL := help
.PHONY: help info check install-deps install-vulkan-sdk submodules configure build debug build-log \
        test-file test-folder unit-test sanitize-test integration-test clean \
        profile test-file-profile

help:
	@echo "Upscayl NCNN build targets:"
	@echo ""
	@echo "  make                       Show help message (default)"
	@echo "  make help                  Show help message (default)"
	@echo "  make info                  Print detected platform, compilers, and paths"
	@echo "  make check                 Verify required build tools are present"
	@echo "  make install-deps          Install system packages"
	@echo "  make install-vulkan-sdk    Download Vulkan SDK into this repo"
	@echo "  make submodules            Initialize/update git submodules"
	@echo "  make configure             Run cmake configuration step"
	@echo "  make build                 Configure and build"
	@echo "  make rebuild               Clean and build"
	@echo "  make debug                 Configure and build with debug symbols"
	@echo "  make test-file             Upscale a single test image"
	@echo "  make test-folder           Upscale a folder of test images"
	@echo "  make unit-test             Build and run Catch2 unit tests"
	@echo "  make integration-test      Run CLI integration tests"
	@echo "  make sanitize-test         Build and run tests with AddressSanitizer and UBSan"
	@echo "  make profile               Build with NCNN_BENCHMARK=ON for per-layer profiling"
	@echo "  make test-file-profile     Run test image with profiling build"
	@echo "  make clean                 Remove build directory"
	@echo "  make clean-all             Remove build, logs, output, and downloaded SDK"
	@echo ""
	@echo "Override variables via CLI or .env file (see .env.example):"
	@echo "  make build CC=gcc-12 CXX=g++-12"
	@echo "  make build BUILD_DIR_TYPE_OPTION=build-arm64 ARCH=arm64"
	@echo "  make build VULKAN_SDK=/path/to/sdk"
	@echo "  make build CMAKE_BUILD_TYPE=RelWithDebInfo"

info:
	@echo "OS:                    $(OS)"
	@echo "ARCH:                  $(ARCH)"
	@echo "CC:                    $(CC)"
	@echo "CXX:                   $(CXX)"
	@echo "CMAKE_BUILD_TYPE:      $(CMAKE_BUILD_TYPE)"
	@echo "CUSTOM_BUILD_OPTION:   $(CUSTOM_BUILD_OPTION)"
	@echo "BUILD_DIR_TYPE_OPTION:             $(BUILD_DIR_TYPE_OPTION)"
	@echo "CMAKE_SRC_DIR:         $(CMAKE_SRC_DIR)"
	@echo "MODEL_DIR:             $(MODEL_DIR)"
	@echo "MODEL_NAME:            $(MODEL_NAME)"
	@echo "INPUT_DIR:             $(INPUT_DIR)"
	@echo "INPUT_FILE:            $(INPUT_FILE)"
	@echo "OUTPUT_DIR:            $(OUTPUT_DIR)"
	@echo "PARALLEL_JOBS:         $(PARALLEL_JOBS)"
	@echo "VK_VERSION:            $(VK_VERSION)"
	@echo "VULKAN_SDK_DIR:        $(VULKAN_SDK_DIR)"
	@echo "VULKAN_SDK:            $(or $(VULKAN_SDK),(not set))"
	@echo "LOG_DIR_TYPE_OPTION:               $(LOG_DIR_TYPE_OPTION)"
	@echo "BUILD_LOG:             $(BUILD_LOG)"
	@echo "TEST_NAME:             $(TEST_NAME)"
	@echo "TEST_LOG:              $(TEST_LOG)"
	@echo "CMAKE_EXTRA_FLAGS:     $(CMAKE_EXTRA_FLAGS)"
	@echo "CMAKE_CXX_FLAGS:       $(CMAKE_CXX_FLAGS)"
	@echo "CMAKE_FLAGS:           $(CMAKE_FLAGS)"
ifeq ($(OS),Darwin)
	@echo "LIBOMP_PREFIX:         $(or $(LIBOMP_PREFIX),(not found))"
endif
	@echo "BINARY:                $(BINARY)"

# --- Dependency checks ---

check:
	@echo "Checking build requirements..."; echo ""; FAIL=0; \
	\
	printf "  %-24s" "cmake"; \
	if command -v cmake >/dev/null 2>&1; then \
	  echo "OK ($$(cmake --version | head -1 | awk '{print $$3}'))"; \
	else \
	  echo "MISSING"; echo "    Install: sudo apt install cmake (Linux) / brew install cmake (macOS)"; FAIL=1; \
	fi; \
	\
	printf "  %-24s" "$(CC)"; \
	if command -v $(CC) >/dev/null 2>&1; then \
	  echo "OK"; \
	else \
	  echo "MISSING"; \
	  if command -v dnf >/dev/null 2>&1; then \
	    echo "    Install: sudo dnf install gcc gcc-c++"; \
	  else \
	    echo "    Install: sudo apt install gcc-9 (Debian/Ubuntu)"; \
	  fi; FAIL=1; \
	fi; \
	\
	printf "  %-24s" "$(CXX)"; \
	if command -v $(CXX) >/dev/null 2>&1; then \
	  echo "OK"; \
	else \
	  echo "MISSING"; \
	  if command -v dnf >/dev/null 2>&1; then \
	    echo "    Install: sudo dnf install gcc-c++"; \
	  else \
	    echo "    Install: sudo apt install g++-9 (Debian/Ubuntu)"; \
	  fi; FAIL=1; \
	fi; \
	\
	printf "  %-24s" "libstdc++ (static)"; \
	if echo 'int main(){}' | $(CXX) -static-libstdc++ -x c++ -o /dev/null - 2>/dev/null; then \
	  echo "OK"; \
	else \
	  echo "MISSING"; \
	  if command -v dnf >/dev/null 2>&1; then \
	    echo "    Install: sudo dnf install libstdc++-static"; \
	  else \
	    echo "    (usually included with g++ on Debian/Ubuntu)"; \
	  fi; FAIL=1; \
	fi; \
	\
	printf "  %-24s" "glslangValidator"; \
	if command -v glslangValidator >/dev/null 2>&1; then \
	  echo "OK"; \
	elif [ -n "$(VULKAN_SDK)" ] && [ -x "$(VULKAN_SDK)/bin/glslangValidator" ]; then \
	  echo "OK (via VULKAN_SDK)"; \
	else \
	  echo "MISSING"; \
	  if command -v dnf >/dev/null 2>&1; then \
	    echo "    Install: sudo dnf install glslang"; \
	  elif command -v apt >/dev/null 2>&1; then \
	    echo "    Install: sudo apt install glslang-tools"; \
	  else \
	    echo "    Install: brew install glslang (macOS)"; \
	  fi; FAIL=1; \
	fi; \
	\
	printf "  %-24s" "Vulkan"; \
	if [ -n "$(VULKAN_SDK)" ]; then \
	  echo "OK (VULKAN_SDK=$(VULKAN_SDK))"; \
	elif pkg-config --exists vulkan 2>/dev/null; then \
	  echo "OK (system)"; \
	else \
	  echo "NOT FOUND"; \
	  echo "    Set VULKAN_SDK in .env, or run: make install-vulkan-sdk"; \
	  if command -v dnf >/dev/null 2>&1; then \
	    echo "    Linux system install: sudo dnf install vulkan-loader-devel"; \
	  else \
	    echo "    Linux system install: sudo apt install libvulkan-dev"; \
	  fi; FAIL=1; \
	fi; \
	\
	printf "  %-24s" "OpenMP"; \
	if [ "$(OS)" = "Darwin" ]; then \
	  if [ -n "$(LIBOMP_PREFIX)" ] && [ -d "$(LIBOMP_PREFIX)" ]; then \
	    echo "OK ($(LIBOMP_PREFIX))"; \
	  else \
	    echo "MISSING"; echo "    Install: brew install libomp"; FAIL=1; \
	  fi; \
	else \
	  if [ -f /usr/lib/libomp.so ] || [ -f /usr/lib/x86_64-linux-gnu/libomp.so ] || [ -f /usr/lib64/libomp.so ]; then \
	    echo "OK"; \
	  elif dpkg -s libomp-dev >/dev/null 2>&1; then \
	    echo "OK"; \
	  elif rpm -q libomp-devel >/dev/null 2>&1; then \
	    echo "OK"; \
	  else \
	    echo "MISSING (optional but recommended)"; \
	    if command -v dnf >/dev/null 2>&1; then \
	      echo "    Install: sudo dnf install libomp-devel"; \
	    else \
	      echo "    Install: sudo apt install libomp-dev"; \
	    fi; \
	  fi; \
	fi; \
	\
	printf "  %-24s" "git submodules"; \
	if [ -f src/ncnn/CMakeLists.txt ] && [ -f src/libwebp/CMakeLists.txt ]; then \
	  echo "OK"; \
	else \
	  echo "NOT INITIALIZED"; echo "    Run: make submodules"; \
	fi; \
	\
	echo ""; \
	if [ $$FAIL -ne 0 ]; then \
	  echo "FAILED: required tools missing (see above)."; exit 1; \
	else \
	  echo "All required tools found."; \
	fi

# --- Install system packages ---

install-deps:
ifeq ($(OS),Linux)
	@if command -v dnf >/dev/null 2>&1; then \
	  sudo dnf install -y cmake gcc gcc-c++ libstdc++-static libomp-devel vulkan-loader-devel glslang libasan libubsan; \
	elif command -v apt >/dev/null 2>&1; then \
	  sudo apt update && sudo apt install -y cmake gcc-9 g++-9 libomp-dev libvulkan-dev glslang-tools libasan8 libubsan1; \
	else \
	  echo "Unsupported Linux package manager. Install manually: cmake, gcc, g++, libomp, vulkan, glslang libasan libubsan"; exit 1; \
	fi
else ifeq ($(OS),Darwin)
	brew install cmake libomp glslang
else
	@echo "Unsupported platform: $(OS)"; exit 1
endif

# --- Download Vulkan SDK into repo ---

install-vulkan-sdk:
ifeq ($(OS),Linux)
	@echo "This will download the Vulkan SDK $(VK_VERSION) (~200 MB) into $(VULKAN_SDK_DIR)/"; \
	echo ""; \
	read -p "Continue? [y/N] " ans; \
	case "$$ans" in [yY]*) ;; *) echo "Aborted."; exit 1;; esac; \
	set -e; \
	mkdir -p $(VULKAN_SDK_DIR); \
	echo "Downloading..."; \
	wget -q --show-progress \
	  "https://sdk.lunarg.com/sdk/download/$(VK_VERSION)/linux/vulkansdk-linux-x86_64-$(VK_VERSION).tar.xz?Human=true" \
	  -O $(VULKAN_SDK_DIR)/vk.tar.xz; \
	echo "Extracting..."; \
	tar xf $(VULKAN_SDK_DIR)/vk.tar.xz -C $(VULKAN_SDK_DIR); \
	rm $(VULKAN_SDK_DIR)/vk.tar.xz; \
	echo ""; \
	echo "Done. Add to your .env:"; \
	echo "  VULKAN_SDK=$$(pwd)/$(VULKAN_SDK_DIR)/$(VK_VERSION)/x86_64"
else ifeq ($(OS),Darwin)
	@echo "This will download the Vulkan SDK $(VK_VERSION) (~400 MB) into $(VULKAN_SDK_DIR)/"; \
	echo "The installer requires sudo for the InstallVulkan step."; \
	echo ""; \
	read -p "Continue? [y/N] " ans; \
	case "$$ans" in [yY]*) ;; *) echo "Aborted."; exit 1;; esac; \
	set -e; \
	mkdir -p $(VULKAN_SDK_DIR); \
	echo "Downloading..."; \
	curl -L --progress-bar \
	  "https://sdk.lunarg.com/sdk/download/$(VK_VERSION)/mac/vulkansdk-macos-$(VK_VERSION).dmg?Human=true" \
	  -o $(VULKAN_SDK_DIR)/vk.dmg; \
	echo "Mounting and installing..."; \
	hdiutil attach $(VULKAN_SDK_DIR)/vk.dmg -quiet; \
	sudo /Volumes/vulkansdk-macos-$(VK_VERSION)/InstallVulkan.app/Contents/MacOS/InstallVulkan \
	  --root $$(pwd)/$(VULKAN_SDK_DIR) --accept-licenses --default-answer --confirm-command install; \
	hdiutil detach /Volumes/vulkansdk-macos-$(VK_VERSION) -quiet; \
	rm $(VULKAN_SDK_DIR)/vk.dmg; \
	echo ""; \
	echo "Done. Add to your .env:"; \
	echo "  VULKAN_SDK=$$(pwd)/$(VULKAN_SDK_DIR)"
else
	@echo "Unsupported platform: $(OS)"; exit 1
endif

# --- Submodules ---

submodules:
	@if [ ! -f src/ncnn/CMakeLists.txt ] || [ ! -f src/libwebp/CMakeLists.txt ]; then \
	  echo "Initializing git submodules..."; \
	  git submodule update --init --recursive; \
	else \
	  echo "Submodules already initialized."; \
	fi

# --- Build ---

# cmake -B <build-dir> -S <source-dir> configures the build in <build-dir> without
# having to cd there first.  cmake creates the build directory if it does not exist.
# All -D flags set cmake cache variables (e.g. -DCMAKE_BUILD_TYPE=Release).
# CMAKE_CXX_FLAGS passes extra flags directly to the C++ compiler for every file.
configure: check submodules
	@echo "Configuring build with CMake..."
	cmake -B $(BUILD_DIR_TYPE_OPTION) -S $(CMAKE_SRC_DIR) $(CMAKE_FLAGS)
	@echo "Configuration complete."

_build: configure
	@echo "Building with CMake..."
	cmake --build $(BUILD_DIR_TYPE_OPTION) -j $(PARALLEL_JOBS)
	@echo "Built: $(BINARY)"

build-default: ensure-log-dir
	@echo "Logging build to: $(BUILD_LOG)"
	$(MAKE) _build 2>&1 | tee "$(BUILD_LOG)"
	@echo "Build log saved: $(BUILD_LOG)"

build: build-default

rebuild: clean build

# Debug build — uses a separate directory so it coexists with the release build.
# $(MAKE) passes BUILD_TYPE as command-line variables to the sub-make,
# which ensures CMAKE_FLAGS (computed with :=) picks them up correctly.
build-debug:
	$(MAKE) build CMAKE_BUILD_TYPE=Debug

# Profiling build — compiles NCNN with NCNN_BENCHMARK=ON and the matching preprocessor
# define so every layer prints its execution time in milliseconds.
# Note: cmake's option(NCNN_BENCHMARK) is never wired to a compile definition inside
# NCNN's own CMakeLists, so we must pass -DNCNN_BENCHMARK=1 via CMAKE_CXX_FLAGS directly.
build-profile:
	$(MAKE) build CMAKE_BUILD_TYPE=Release CUSTOM_BUILD_OPTION=profile CMAKE_CXX_FLAGS="-DNCNN_BENCHMARK=1"

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
	@mkdir -p $(LOG_DIR_TYPE_OPTION)

setup-test: check-model-file check-input-file ensure-output-dir ensure-log-dir
	@echo "Test setup complete."
	@echo "Model: ${MODEL_NAME}"
	@echo "Input: ${INPUT_FILE}"
	@echo "Output: ${OUTPUT_FILE}"

# Variable-chain for defining TEST_LOG:
# TEST_LOG <-- LOG_DIR_TYPE_OPTION + TEST_NAME + timestamp
# LOG_DIR_TYPE_OPTION <-- logs/ + CMAKE_BUILD_TYPE + CUSTOM_BUILD_OPTION
# CMAKE_BUILD_TYPE (default "Release") and CUSTOM_BUILD_OPTION (default "default") are used to group logs by build configuration.
# CUSTOM_BUILD_OPTION is useful to differentiate between different builds of the same type (e.g. default vs profile), while CMAKE_BUILD_TYPE differentiates between fundamentally different builds (e.g. Release vs Debug).
# TEST_NAME (default "unset") is used to differentiate logs for different test targets (e.g. test-file vs test-folder).
# You can test this by running 'make info' and seeing the printed values.
_test-file: setup-test build
	@echo "Logging to: $(TEST_LOG)"
	{ time $(BINARY) \
		-i $(INPUT_FILE) \
		-o $(OUTPUT_FILE) \
		-m $(MODEL_DIR) \
		-n $(MODEL_NAME) \
		-c 0 \
		-s 4; \
	} 2>&1 | tee "$(TEST_LOG)"

test-file:
	@echo "Testing single file upscale..."; \
	$(MAKE) _test-file TEST_NAME=test-file

# test-folder target runs the same upscale command but with an input directory instead of a single file,
# allowing you to test multiple images at once. 
# The output files will be saved in the specified output directory with the same names as the input files
# but with "-upscaled" appended before the extension (e.g. image.webp -> image-upscaled.webp).
_test-folder: setup-test build
	@echo "Testing folder upscale..."; \
	{ time $(BINARY) \
		-i $(INPUT_DIR) \
		-o $(OUTPUT_DIR) \
		-m $(MODEL_DIR) \
		-n $(MODEL_NAME) \
		-c 0 \
		-s 4; \
	} 2>&1 | tee "$(TEST_LOG)"

test-folder:
	@echo "Testing folder upscale..."; \
	$(MAKE) _test-folder TEST_NAME=test-folder

test-file-profile:
	@echo "Testing single file upscale with profiling..."; \
	$(MAKE) _test-file CMAKE_BUILD_TYPE=Release CUSTOM_BUILD_OPTION=profile CMAKE_CXX_FLAGS="-DNCNN_BENCHMARK=1" TEST_NAME=test-file-profile

unit-test: check submodules
	@mkdir -p $(BUILD_DIR_TYPE_OPTION)
	cd $(BUILD_DIR_TYPE_OPTION) && cmake $(CMAKE_FLAGS) -DBUILD_TESTS=ON ../$(CMAKE_SRC_DIR)
	cmake --build $(BUILD_DIR_TYPE_OPTION) -j $(PARALLEL_JOBS) --target upscayl-tests --target upscayl-codec-tests
	cd $(BUILD_DIR_TYPE_OPTION) && ctest --output-on-failure

integration-test: build
	UPSCAYL_BIN=$(BINARY) python3 tests/test_cli_integration.py

sanitize-test: check submodules
	@mkdir -p $(BUILD_DIR_TYPE_OPTION)-sanitize
	cd $(BUILD_DIR_TYPE_OPTION)-sanitize && cmake $(CMAKE_FLAGS) \
	    -DBUILD_TESTS=ON \
	    -DENABLE_ASAN=ON \
	    -DENABLE_UBSAN=ON \
	    -DCMAKE_BUILD_TYPE=Debug \
	    ../$(CMAKE_SRC_DIR)
	cmake --build $(BUILD_DIR_TYPE_OPTION)-sanitize -j $(PARALLEL_JOBS) --target upscayl-tests --target upscayl-codec-tests
	cd $(BUILD_DIR_TYPE_OPTION)-sanitize && ctest --output-on-failure

# --- Clean ---

clean-build-type-option:
	rm -rf $(BUILD_DIR_TYPE_OPTION)

clean-build: clean-build-type-option
	rm -rf $(BUILD_DIR)

clean-logs-type-option:
	rm -rf $(LOG_DIR_TYPE_OPTION)

clean-logs:
	rm -rf $(LOG_DIR)

clean-output:
	rm -rf $(OUTPUT_DIR)

clean-deps:
	rm -rf $(VULKAN_SDK_DIR)

clean-type-option: clean-build-type-option clean-logs-type-option

# "clean" removes the build and logs for the current CMAKE_BUILD_TYPE and CUSTOM_BUILD_OPTION, preserving other configurations (e.g. debug logs remain if you clean the release build).
clean: clean-type-option

clean-all: clean-build clean-logs clean-output clean-deps
