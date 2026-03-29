# Upscayl NCNN

CLI image upscaler using RealESRGAN with NCNN and Vulkan GPU acceleration.

## Quick Start

```bash
git clone --recursive git@github.com:upscayl/upscayl-ncnn.git
cd upscayl-ncnn
make check          # verify prerequisites
make build          # build the project
```

The binary is produced at `build/upscayl-bin`.

## Prerequisites

### Linux

```bash
make install-deps
```

This auto-detects your package manager and installs the required packages:

- **Debian/Ubuntu** (apt): `cmake`, `gcc-9`, `g++-9`, `libomp-dev`, `libvulkan-dev`, `glslang-tools`
- **Fedora** (dnf): `cmake`, `gcc`, `gcc-c++`, `libomp-devel`, `vulkan-loader-devel`, `glslang`

### macOS

```bash
make install-deps
```

This installs: `cmake`, `libomp`, `glslang` (via Homebrew).

### Vulkan SDK (optional on Linux, required on macOS)

On Linux, system packages (`libvulkan-dev`, `glslang-tools`) are sufficient. A full Vulkan SDK is optional.

On macOS, you need the Vulkan SDK for MoltenVK. You can install it automatically:

```bash
make install-vulkan-sdk
```

This downloads the SDK into the `vulkan-sdk/` directory inside the repo (gitignored) and prints the `VULKAN_SDK` value to add to your `.env`.

Alternatively, download manually from [vulkan.lunarg.com](https://vulkan.lunarg.com/sdk/home) and set `VULKAN_SDK` in your `.env` file.

## Configuration

Copy `.env.example` to `.env` and adjust as needed:

```bash
cp .env.example .env
```

All variables have sensible defaults. Common overrides:

| Variable | Description | Default |
| -------- | ----------- | ------- |
| `VULKAN_SDK` | Path to Vulkan SDK | (auto-detect) |
| `CC` / `CXX` | C/C++ compiler | `gcc-9`/`g++-9` (Linux), `cc`/`c++` (macOS) |
| `BUILD_DIR` | Build output directory | `build` |
| `PARALLEL_JOBS` | Parallel compilation jobs | number of CPU cores |
| `VK_VERSION` | Vulkan SDK version for download | `1.3.280.0` |
| `LIBOMP_PREFIX` | macOS OpenMP path | `brew --prefix libomp` |

Variables can also be passed on the command line:

```bash
make build CC=gcc-12 CXX=g++-12
make build VULKAN_SDK=/path/to/sdk
make build BUILD_DIR=build-arm64 ARCH=arm64
```

## Build Targets

| Target | Description |
| ------ | ----------- |
| `make` | Build the project (default) |
| `make check` | Verify required build tools are present |
| `make install-deps` | Install system packages (apt/brew) |
| `make install-vulkan-sdk` | Download Vulkan SDK into repo (interactive) |
| `make submodules` | Initialize/update git submodules |
| `make configure` | Run cmake configuration |
| `make build` | Configure and build |
| `make test-file` | Upscale a single test image |
| `make test-folder` | Upscale a folder of test images |
| `make unit-test` | Build and run Catch2 unit tests |
| `make integration-test` | Run CLI integration tests (no GPU needed) |
| `make sanitize-test` | Build and run tests with AddressSanitizer and UBSan |
| `make clean` | Remove build directory |
| `make info` | Print detected platform, compilers, paths |
| `make help` | Show all targets |

## Testing

### Unit Tests

```bash
make unit-test
```

Builds and runs the Catch2 unit test suite (filesystem utilities, image utils, codec round-trips). No GPU or model files required. Tests are built with `-DBUILD_TESTS=ON` and executed via CTest.

### CLI Integration Tests

```bash
make integration-test
```

Runs Python-based tests that exercise the compiled binary's argument validation and error handling. No GPU required — tests cover argument parsing paths that exit before GPU initialization.

### Sanitizer Tests

```bash
make sanitize-test
```

Builds and runs the unit tests with AddressSanitizer and UndefinedBehaviorSanitizer enabled (Debug build). Useful for catching memory errors and undefined behavior. Requires `libasan` to be installed (available by default on Ubuntu; install `libasan` on Fedora).

### Manual GPU Tests

Place model files (`.param` and `.bin`) in the `models/` directory, then:

```bash
make test-file      # single image upscale
make test-folder    # batch upscale
```

## macOS Notes

The Makefile automatically handles macOS-specific cmake flags (MoltenVK, OpenMP via Homebrew, architecture detection). No manual cmake invocation is needed.

For cross-compilation (e.g. building arm64 on an x86_64 machine):

```bash
make build BUILD_DIR=build-arm64 ARCH=arm64
```

## Manual Build (without Make)

If you prefer raw cmake:

```bash
git submodule update --init --recursive
mkdir build && cd build
cmake ..                              # add platform flags as needed
cmake --build . -j $(nproc)
```

See the [Makefile](Makefile) for the full set of platform-specific cmake flags.

## Third-Party Libraries

| Library | Version | Location | License |
| ------- | ------- | -------- | ------- |
| [ncnn](https://github.com/Tencent/ncnn) | commit `20220420` | `src/ncnn/` (submodule) | BSD-3-Clause |
| [libwebp](https://chromium.googlesource.com/webm/libwebp) | v1.2.1 | `src/libwebp/` (submodule) | BSD-3-Clause |
| [stb_image](https://github.com/nothings/stb) | v2.30 | `third_party/stb_image.h` | Public Domain |
| [stb_image_write](https://github.com/nothings/stb) | v1.16 | `third_party/stb_image_write.h` | Public Domain |
| [stb_image_resize2](https://github.com/nothings/stb) | v2.18 | `third_party/stb_image_resize2.h` | Public Domain |
| [Catch2](https://github.com/catchorg/Catch2) | v3.5.2 | fetched at build time (tests only) | BSL-1.0 |
