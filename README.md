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
| `make clean` | Remove build directory |
| `make info` | Print detected platform, compilers, paths |
| `make help` | Show all targets |

## Testing

### Unit Tests

```bash
make unit-test
```

This builds and runs the Catch2 unit test suite. No GPU or model files required. Tests are built with `-DBUILD_TESTS=ON` and executed via CTest.

### Integration Tests

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
