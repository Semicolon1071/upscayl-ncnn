# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Upscayl NCNN is a CLI image upscaler using RealESRGAN with NCNN (Tencent's neural network framework) and Vulkan GPU acceleration. It supports batch processing, multiple image formats (JPG, PNG, WebP), and cross-platform builds (Linux, macOS, Windows).

## Build Commands

All builds go through the Makefile. Per-developer overrides go in `.env` (see `.env.example`).

```bash
make check          # install deps + Vulkan SDK, verify all tools
make build          # configure + build (default preset: release)
make clean          # remove build/logs for current preset
```

The binary is produced at `build/<preset>/src/upscayl-bin`.

### CMake Presets

Build configuration profiles are defined in `CMakePresets.json`. The Makefile uses `cmake --preset` under the hood.

Available presets: `release`, `debug`, `profile`, `unit-test`, `integration-test`, `sanitize`.

```bash
make build                    # release (default)
make build PRESET=debug       # debug build
make debug                    # shorthand for PRESET=debug
make profile                  # shorthand for PRESET=profile (NCNN_BENCHMARK)
```

Platform-specific toolchain logic (macOS OpenMP, MoltenVK, cross-compilation) lives in `CMakeLists.txt` and reads environment variables exported by the Makefile (`CC`, `CXX`, `VULKAN_SDK`, `LIBOMP_PREFIX`, `ARCH`).

### Testing

```bash
make test-file          # single image upscale (manual, needs models in testdata/models/)
make test-folder        # batch upscale (manual)
make unit-test          # build and run Catch2 unit tests
make integration-test   # run CLI integration tests (Python)
make sanitize-test      # build and run tests with ASan + UBSan
```

Unit tests are in `tests/` (Catch2 C++ tests + a Python CLI integration test). Manual smoke tests use `make test-file`/`make test-folder` against images in `testdata/`.

### Build Info

The binary embeds build metadata (preset, compiler, git hash, timestamp). Use `-V` to display it:

```bash
./build/release/src/upscayl-bin -V
```

Verbose mode (`-v`) also prints a condensed build info line to stderr.

### Useful Targets

- `make info` — print detected OS, arch, compilers, preset, paths
- `make help` — list all available targets
- `make submodules` — initialize/update git submodules (auto-runs before build)
- `make install-vulkan-sdk` — download pinned Vulkan SDK into repo (idempotent)
- `make check-vk-update` — check for newer Vulkan SDK versions
- `make rebuild` — clean current preset and rebuild from scratch
- `make clean-all` — remove build, logs, output, and downloaded SDK

## Architecture

### Processing Pipeline (src/main.cpp)

Three-stage multithreaded pipeline using task queues:

1. **Load stage** — reads images from disk (STB/WebP decoders)
2. **Process stage** — upscales via RealESRGAN on GPU
3. **Save stage** — writes output, applies resize/compression

Thread counts are configurable via `-j load:proc:save` (default `1:2:2`).

### RealESRGAN Engine (src/realesrgan.cpp/h)

- Loads NCNN `.param`/`.bin` model files
- Tile-based processing to manage GPU memory
- Vulkan compute shaders handle pre/post processing (GLSL -> SPIR-V compiled at build time)
- Supports TTA (test-time augmentation) for quality improvement

### Shader Compilation

CMake compiles `.comp` GLSL shaders to SPIR-V via `glslangValidator`, generating three precision variants each (FP32, FP16, INT8) as hex-encoded C headers.

### Git Submodules

- `src/ncnn` — NCNN inference framework
- `src/libwebp` — WebP codec

Submodules are auto-initialized by `make build` if missing. Manual init: `make submodules`.

## Platform Differences

- **Windows**: Uses wide-char (wchar_t) paths, WIC image codecs, `win32dirent.h` compatibility layer. CI builds via raw cmake (MSVC, not Make-compatible).
- **macOS**: MoltenVK for Vulkan; OpenMP via Homebrew; universal binary via `lipo` in release workflow. Platform-specific cmake flags are in CMakeLists.txt (reads LIBOMP_PREFIX, VULKAN_SDK, ARCH env vars).
- **Linux**: Standard Vulkan drivers, GCC-9+ required for C++17 `<filesystem>`
