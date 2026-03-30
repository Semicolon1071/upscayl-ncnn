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

# --- Configurable variables (override via .env or CLI) ---
BUILD_DIR     ?= build
BUILD_TYPE    ?= Release
CMAKE_SRC_DIR ?= .
PARALLEL_JOBS ?= $(NPROC)
VK_VERSION    ?= 1.3.280.0
LOG_DIR           ?= logs
PROFILE_BUILD_DIR ?= build-profile
PROFILE_BINARY    := $(PROFILE_BUILD_DIR)/src/upscayl-bin

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

# --- Binary ---
BINARY := $(BUILD_DIR)/src/upscayl-bin

# --- CMake flag assembly ---
CMAKE_FLAGS := \
  -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
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

# ============================================================
# Targets
# ============================================================

.DEFAULT_GOAL := build
.PHONY: help info check install-deps install-vulkan-sdk submodules configure build debug build-log \
        test-file test-folder unit-test sanitize-test integration-test clean \
        profile test-file-profile

help:
	@echo "Upscayl NCNN build targets:"
	@echo ""
	@echo "  make              Build the project (default)"
	@echo "  make help         Show this help message"
	@echo "  make info         Print detected platform, compilers, and paths"
	@echo "  make check        Verify required build tools are present"
	@echo "  make install-deps Install system packages (apt on Linux, brew on macOS)"
	@echo "  make install-vulkan-sdk"
	@echo "                    Download Vulkan SDK into this repo (interactive)"
	@echo "  make submodules   Initialize/update git submodules"
	@echo "  make configure    Run cmake configuration step"
	@echo "  make build        Configure and build (Release, optimized)"
	@echo "  make build-log    Build and save output to logs/build-TIMESTAMP.log"
	@echo "  make debug        Configure and build with debug symbols (build-debug/)"
	@echo "  make test-file    Upscale a single test image (logs to logs/test-file-TIMESTAMP.log)"
	@echo "  make test-folder  Upscale a folder of test images (logs to logs/test-folder-TIMESTAMP.log)"
	@echo "  make unit-test    Build and run Catch2 unit tests"
	@echo "  make integration-test  Run CLI integration tests (no GPU needed)"
	@echo "  make sanitize-test  Build and run tests with AddressSanitizer and UBSan"
	@echo "  make profile      Build with NCNN_BENCHMARK=ON for per-layer profiling ($(PROFILE_BUILD_DIR)/)"
	@echo "  make test-file-profile"
	@echo "                    Run test image with profiling build (logs/profile-test-file-TIMESTAMP.log)"
	@echo "  make clean        Remove build directory"
	@echo ""
	@echo "Override variables via CLI or .env file (see .env.example):"
	@echo "  make build CC=gcc-12 CXX=g++-12"
	@echo "  make build BUILD_DIR=build-arm64 ARCH=arm64"
	@echo "  make build VULKAN_SDK=/path/to/sdk"
	@echo "  make build BUILD_TYPE=RelWithDebInfo  # optimized + debug symbols (for profiling)"

info:
	@echo "OS:            $(OS)"
	@echo "ARCH:          $(ARCH)"
	@echo "CC:            $(CC)"
	@echo "CXX:           $(CXX)"
	@echo "BUILD_DIR:     $(BUILD_DIR)"
	@echo "BUILD_TYPE:    $(BUILD_TYPE)"
	@echo "PARALLEL_JOBS: $(PARALLEL_JOBS)"
	@echo "VK_VERSION:    $(VK_VERSION)"
	@echo "VULKAN_SDK:    $(or $(VULKAN_SDK),(not set))"
ifeq ($(OS),Darwin)
	@echo "LIBOMP_PREFIX: $(or $(LIBOMP_PREFIX),(not found))"
endif
	@echo "BINARY:        $(BINARY)"

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
	  sudo dnf install -y cmake gcc gcc-c++ libstdc++-static libomp-devel vulkan-loader-devel glslang; \
	elif command -v apt >/dev/null 2>&1; then \
	  sudo apt update && sudo apt install -y cmake gcc-9 g++-9 libomp-dev libvulkan-dev glslang-tools; \
	else \
	  echo "Unsupported Linux package manager. Install manually: cmake, gcc, g++, libomp, vulkan, glslang"; exit 1; \
	fi
else ifeq ($(OS),Darwin)
	brew install cmake libomp glslang
else
	@echo "Unsupported platform: $(OS)"; exit 1
endif

# --- Download Vulkan SDK into repo ---

VULKAN_SDK_DIR := vulkan-sdk

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
	cmake -B $(BUILD_DIR) -S $(CMAKE_SRC_DIR) $(CMAKE_FLAGS)

build: configure
	cmake --build $(BUILD_DIR) -j $(PARALLEL_JOBS)
	@echo ""
	@echo "Built: $(BINARY)"

# Debug build — uses a separate directory so it coexists with the release build.
# $(MAKE) passes BUILD_TYPE and BUILD_DIR as command-line variables to the sub-make,
# which ensures CMAKE_FLAGS (computed with :=) picks them up correctly.
debug:
	$(MAKE) BUILD_TYPE=Debug BUILD_DIR=build-debug

build-log:
	@mkdir -p $(LOG_DIR)
	@LOG=$(LOG_DIR)/build-$$(date +%Y%m%d-%H%M%S).log; \
	  echo "Logging build to: $$LOG"; \
	  $(MAKE) build 2>&1 | tee "$$LOG"; \
	  echo "Build log saved: $$LOG"

# Profiling build — compiles NCNN with NCNN_BENCHMARK=ON and the matching preprocessor
# define so every layer prints its execution time in milliseconds.
# Note: cmake's option(NCNN_BENCHMARK) is never wired to a compile definition inside
# NCNN's own CMakeLists, so we must pass -DNCNN_BENCHMARK=1 via CMAKE_CXX_FLAGS directly.
profile: submodules
	cmake -B $(PROFILE_BUILD_DIR) -S $(CMAKE_SRC_DIR) \
	  $(CMAKE_FLAGS) \
	  -DNCNN_BENCHMARK=ON \
	  "-DCMAKE_CXX_FLAGS=-DNCNN_BENCHMARK=1 $(CXXFLAGS)"
	cmake --build $(PROFILE_BUILD_DIR) -j $(PARALLEL_JOBS)
	@echo ""
	@echo "Profile build: $(PROFILE_BINARY)"

test-file-profile: profile
	@if [ ! -d models ] || [ -z "$$(ls models/ 2>/dev/null)" ]; then \
	  echo "Error: models/ directory is empty or missing."; \
	  exit 1; \
	fi
	@mkdir -p output $(LOG_DIR)
	@LOG=$(LOG_DIR)/profile-test-file-$$(date +%Y%m%d-%H%M%S).log; \
	  echo "Profiling to: $$LOG"; \
	  { time $(PROFILE_BINARY) -i ./images/image.webp \
	    -o ./output/image-profile.webp \
	    -m models -n upscayl-standard-4x -c 0; } \
	  2>&1 | tee "$$LOG"

# --- Test ---

test-file: build
	@if [ ! -d models ] || [ -z "$$(ls models/ 2>/dev/null)" ]; then \
	  echo "Error: models/ directory is empty or missing."; \
	  echo "Place model files (.param and .bin) in models/ before testing."; \
	  exit 1; \
	fi
	@mkdir -p output $(LOG_DIR)
	@LOG=$(LOG_DIR)/test-file-$$(date +%Y%m%d-%H%M%S).log; \
	  echo "Logging to: $$LOG"; \
	  { time $(BINARY) -i ./images/image.webp -o ./output/image.webp -m models -n upscayl-standard-4x -c 0; } \
	  2>&1 | tee "$$LOG"

test-folder: build
	@if [ ! -d models ] || [ -z "$$(ls models/ 2>/dev/null)" ]; then \
	  echo "Error: models/ directory is empty or missing."; \
	  echo "Place model files (.param and .bin) in models/ before testing."; \
	  exit 1; \
	fi
	@mkdir -p output $(LOG_DIR)
	@LOG=$(LOG_DIR)/test-folder-$$(date +%Y%m%d-%H%M%S).log; \
	  echo "Logging to: $$LOG"; \
	  { time $(BINARY) -i ./images/ -o ./output/ -s 4 -m models -n upscayl-standard-4x; } \
	  2>&1 | tee "$$LOG"

unit-test: check submodules
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake $(CMAKE_FLAGS) -DBUILD_TESTS=ON ../$(CMAKE_SRC_DIR)
	cmake --build $(BUILD_DIR) -j $(PARALLEL_JOBS) --target upscayl-tests --target upscayl-codec-tests
	cd $(BUILD_DIR) && ctest --output-on-failure

integration-test: build
	UPSCAYL_BIN=$(BINARY) python3 tests/test_cli_integration.py

sanitize-test: check submodules
	@mkdir -p $(BUILD_DIR)-sanitize
	cd $(BUILD_DIR)-sanitize && cmake $(CMAKE_FLAGS) \
	    -DBUILD_TESTS=ON \
	    -DENABLE_ASAN=ON \
	    -DENABLE_UBSAN=ON \
	    -DCMAKE_BUILD_TYPE=Debug \
	    ../$(CMAKE_SRC_DIR)
	cmake --build $(BUILD_DIR)-sanitize -j $(PARALLEL_JOBS) --target upscayl-tests --target upscayl-codec-tests
	cd $(BUILD_DIR)-sanitize && ctest --output-on-failure

# --- Clean ---

clean:
	rm -rf $(BUILD_DIR) build-debug $(PROFILE_BUILD_DIR) $(LOG_DIR)
