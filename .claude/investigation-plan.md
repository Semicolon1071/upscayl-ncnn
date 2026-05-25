# NCNN Performance Regression: Investigation Plan

Last updated: 2026-03-30

## Background

After upgrading the NCNN submodule from 20220420 to 20260113, upscayl-ncnn runs **4x slower**
on Intel Iris Xe Graphics (ADL GT2):

| Build | fp32 wall time | fp16 wall time |
| --- | --- | --- |
| Old NCNN 20220420 | 1m05s (baseline) | not tested |
| New NCNN 20260113 | 4m34s (4x regression) | 2m15s (1.5x with optimization) |

CPU time is identical (~6.3s). The regression is 100% GPU execution time.

### What we know

- The regression is **uniform across all 351 Convolution layers** (~1.6x per layer with fp16,
  ~4x per layer with fp32). No single bottleneck layer.
- The hot-path shader is `convolution_pack4_3x3s1d1_winograd_gemm.comp` (Winograd GEMM kernel,
  ~99.5% of GPU time).
- **The GLSL source is functionally identical** between old and new NCNN for Intel Xe's
  configuration (fp16_storage + fp16_arithmetic, pack4, local memory enabled). All differences
  are cosmetic (macro renames, variable renames with numerically equivalent values).
- Intel Xe (ADL GT2) does NOT support `VK_KHR_cooperative_matrix` -- only Intel Xe2
  (Lunar Lake/Battlemage) does. So the cooperative matrix GEMM path is never taken.

### What has been ruled out

- bf16 (never requested), queue selection (same queue), Winograd disable (made things worse),
  int8_storage (no effect), pack8 removal (Intel Xe uses pack4), transform coefficients (<0.1%
  of runtime), blob memory layout (strides numerically equivalent), GEMM shader source (proven
  identical).

### Prime suspect: glslang version jump

NCNN bundles GLSL shader source as hex-encoded text. At runtime, it compiles GLSL to SPIR-V
using a **bundled glslang library**, then the Intel Vulkan driver JIT-compiles the SPIR-V to
GPU machine code.

- Old NCNN: KhronosGroup/glslang commit `4afd691` (~2022)
- New NCNN: nihui/glslang fork commit `fe88f42` (~2026)
- These are **7 major versions apart**

Different glslang versions produce different SPIR-V from identical GLSL, which the GPU driver
then compiles to different machine code. This is the most likely cause of the 4x regression.

---

## Experiments

### Experiment 1: SPIR-V Comparison (the smoking gun test)

**Goal:** Prove that old and new NCNN produce different SPIR-V from the same GLSL.

**Prerequisite:** Install SPIR-V tools.

```bash
# Fedora
sudo dnf install spirv-tools
# This provides spirv-dis (disassembler) and spirv-val (validator)
```

**Approach A: Dump SPIR-V from within NCNN at runtime**

This is the most accurate method because it captures the exact SPIR-V that NCNN feeds to the
GPU driver, including all runtime preprocessing (macro expansion, specialization constants).

Add a SPIR-V dump hook in NCNN's pipeline creation. In `src/ncnn/src/gpu.cpp`, find the
`compile_spirv_module` function (or equivalent -- search for `glslang::TProgram` or
`GlslangToSpv`). After the SPIR-V blob is produced, write it to a file:

```cpp
// Add after GlslangToSpv() call produces the std::vector<unsigned int> spirv:
{
    static int shader_id = 0;
    char path[256];
    snprintf(path, sizeof(path), "/tmp/ncnn-spirv-%04d.spv", shader_id++);
    FILE* f = fopen(path, "wb");
    if (f) {
        fwrite(spirv.data(), sizeof(unsigned int), spirv.size(), f);
        fclose(f);
        fprintf(stderr, "[spirv-dump] wrote %s (%zu words)\n", path, spirv.size());
    }
}
```

Then build and run both old and new NCNN:

```bash
# Build new NCNN with SPIR-V dump
make clean build
make test-file 2>&1 | grep spirv-dump
# SPIR-V files land in /tmp/ncnn-spirv-*.spv
mkdir -p logs/spirv-new
mv /tmp/ncnn-spirv-*.spv logs/spirv-new/

# Build old NCNN with same dump
git -C src/ncnn checkout 20220420
git -C src/ncnn submodule update --init --recursive
make clean build
make test-file 2>&1 | grep spirv-dump
mkdir -p logs/spirv-old
mv /tmp/ncnn-spirv-*.spv logs/spirv-old/

# Restore
git -C src/ncnn checkout 20260113
git -C src/ncnn submodule update --init --recursive
make clean build
```

Compare the SPIR-V for the Winograd GEMM shader (it will be one of the larger `.spv` files --
the GEMM shader is ~143 lines of GLSL, most other shaders are smaller):

```bash
# Disassemble to human-readable SPIR-V assembly
spirv-dis logs/spirv-old/ncnn-spirv-XXXX.spv > logs/spirv-old-gemm.spvasm
spirv-dis logs/spirv-new/ncnn-spirv-XXXX.spv > logs/spirv-new-gemm.spvasm
diff logs/spirv-old-gemm.spvasm logs/spirv-new-gemm.spvasm
```

To identify which `.spv` file is the Winograd GEMM shader: look for the one that contains
`LOCAL_MEMORY_UNROLL_INCH` patterns (8-element shared memory arrays), or sort by file size
and check the larger ones. You can also add the shader name to the dump filename by grepping
for the shader source text.

**Approach B: Compile preprocessed GLSL with system glslangValidator**

This is simpler but less accurate -- it uses the system glslangValidator, not the bundled one.
However, it can confirm whether the GLSL source itself (ignoring glslang version) produces
identical SPIR-V.

The preprocessed GLSL files still contain NCNN-specific macros (`sfpvec4`, `afpvec4`, etc.)
that need to be resolved. For Intel Xe with fp16_storage + fp16_arithmetic + local memory,
create a preamble file:

```bash
cat > /tmp/ncnn_intel_xe_preamble.glsl << 'PREAMBLE'
#define NCNN_fp16_storage 1
#define NCNN_fp16_arithmetic 1
#define NCNN_shader_local_memory 1
#define NCNN_image_shader 0

#define sfpvec4 f16vec4
#define afpvec4 f16vec4
#define afpmat4 f16mat4
#define lfpvec4 f16vec4

#define buffer_ld4(buf,i) buf[i]
#define buffer_st4(buf,i,v) {buf[i]=v;}
#define buffer_sm4(buf,i) buf[i]
#define sfp2lfpvec4(v) v
#define lfp2afpvec4(v) v

#define psc(x) (x==0?p.x:x)
PREAMBLE
```

Then compile each preprocessed GLSL:

```bash
# Combine preamble + shader source
cat /tmp/ncnn_intel_xe_preamble.glsl \
    build/src/ncnn/src/layer/vulkan/shader/convolution_pack4_3x3s1d1_winograd_gemm.text2hex.txt \
    > /tmp/new_gemm.comp

cat /tmp/ncnn_intel_xe_preamble.glsl \
    build-old/src/ncnn/src/convolution_pack4_3x3s1d1_winograd_gemm.text2hex.txt \
    > /tmp/old_gemm.comp

# Compile to SPIR-V (need to strip duplicate #version lines and resolve conflicts)
# The old shader has #if guards that the preamble resolves
glslangValidator -V /tmp/new_gemm.comp -o /tmp/new_gemm.spv
glslangValidator -V /tmp/old_gemm.comp -o /tmp/old_gemm.spv

# Compare
spirv-dis /tmp/new_gemm.spv > /tmp/new_gemm.spvasm
spirv-dis /tmp/old_gemm.spv > /tmp/old_gemm.spvasm
diff /tmp/new_gemm.spvasm /tmp/old_gemm.spvasm
```

Note: This approach may need some manual GLSL fixups (duplicate `#version`, conflicting
extension declarations). The runtime dump (Approach A) avoids these issues entirely.

**Expected outcomes:**

- If SPIR-V differs between old and new builds (Approach A): **glslang confirmed as root cause**.
  Proceed to Experiment 3 (glslang bisection).
- If SPIR-V is identical when using the same glslangValidator (Approach B) but differs in
  Approach A: this doubly confirms glslang is the variable.
- If SPIR-V is identical in both approaches: glslang is NOT the cause. The regression must be
  in NCNN's runtime behavior (pipeline configuration, push constant values, dispatch dimensions).
  Proceed to Experiment 4.

---

### Experiment 2: fp32 Per-Layer Profiling

**Goal:** Confirm the 4x regression factor at per-layer granularity under fp32 (not fp16).

The existing profiling data (in memory) measured 1.6x per layer under fp16 arithmetic. The
overall 4x regression is measured under fp32. We need per-layer fp32 data to confirm the
regression is uniform and not concentrated in specific layers.

**Steps:**

```bash
# 1. Temporarily revert fp16 arithmetic
#    In src/realesrgan.cpp, change:
#      net.opt.use_fp16_arithmetic = true;
#    to:
#      net.opt.use_fp16_arithmetic = false;

# 2. Build and run profile with new NCNN
make profile
make test-file-profile
# Save log
cp logs/profile-test-file-*.log logs/profile-fp32-new.log

# 3. Switch to old NCNN and rebuild
git -C src/ncnn checkout 20220420
git -C src/ncnn submodule update --init --recursive
make clean profile
make test-file-profile
cp logs/profile-test-file-*.log logs/profile-fp32-old.log

# 4. Restore
git -C src/ncnn checkout 20260113
git -C src/ncnn submodule update --init --recursive
make clean build
# Restore use_fp16_arithmetic = true in src/realesrgan.cpp
```

**Analysis:**

```bash
# Extract per-layer conv times and compare
grep 'Convolution' logs/profile-fp32-old.log | head -20
grep 'Convolution' logs/profile-fp32-new.log | head -20

# Sum all conv layer times
grep 'Convolution' logs/profile-fp32-old.log | awk '{sum+=$NF} END{printf "Old total: %.0f ms\n", sum}'
grep 'Convolution' logs/profile-fp32-new.log | awk '{sum+=$NF} END{printf "New total: %.0f ms\n", sum}'
```

**Expected outcome:** Each conv layer should be ~4x slower in new NCNN (not 1.6x as with
fp16). This confirms that fp16 arithmetic masks ~60% of the regression by reducing memory
bandwidth pressure, and the underlying issue affects all layers uniformly.

---

### Experiment 3: glslang Bisection

**Goal:** Find the specific glslang commit that causes the regression.

**Prerequisite:** Experiment 1 must confirm that SPIR-V differs between old and new builds.

The glslang submodule has 7 commits between old and new NCNN. Binary search to find which
commit introduces the SPIR-V change that regresses performance.

**Approach:**

```bash
# List the glslang commits between old and new
cd src/ncnn/glslang
git log --oneline 4afd691..fe88f42

# For each candidate commit:
cd /home/andreas/repo/upscayl-ncnn
git -C src/ncnn/glslang checkout <commit-hash>
make clean build
time make test-file
# Record wall time
```

Since there are only 7 commits, a full linear scan is feasible (no need for true bisection).
Each build+test cycle takes ~5-10 minutes.

**Expected outcome:** One specific glslang commit will show a jump in wall time. This commit
can then be analyzed to understand what SPIR-V codegen change caused the regression.

---

### Experiment 4: glslang Swap (the definitive fix test)

**Goal:** Prove the fix by building new NCNN (20260113) with old glslang.

If Experiment 1 confirms glslang is the cause, this experiment proves the fix works.

```bash
# Save current glslang reference
cd /home/andreas/repo/upscayl-ncnn

# Point new NCNN's glslang submodule to the old commit
git -C src/ncnn/glslang checkout 4afd691

# Build (new NCNN code + old glslang compiler)
make clean build

# Test
time make test-file
# Expected: ~1m05s (matching old NCNN baseline)
```

**If this works (wall time returns to ~1m05s):**

- The root cause is definitively the glslang version
- A permanent fix would be to pin glslang to the old commit (or a known-good intermediate)
- This could be reported upstream to NCNN (nihui/ncnn) as a performance regression on
  Intel Xe hardware caused by the glslang fork

**If this doesn't work (wall time still ~4m34s):**

- The cause is NOT glslang alone
- Proceed to Experiment 5

---

### Experiment 5: RenderDoc GPU Profiling (fallback)

**Goal:** Get per-dispatch GPU timing and shader occupancy data.

Only needed if Experiments 1-4 don't identify the cause.

```bash
# Install RenderDoc if not present
sudo dnf install renderdoc

# Capture a frame
renderdoccmd capture -o logs/renderdoc-capture.rdc -- \
    ./build/src/upscayl-bin \
    -i ./images/Wikipedia-logo-v2-webp.webp \
    -o ./output/Wikipedia-logo-v2-webp-rdc.webp \
    -m models -n upscayl-standard-4x -c 0

# Open in GUI
renderdoccmd replay logs/renderdoc-capture.rdc
```

**What to look for:**

- In the Event Browser: find `vkCmdDispatch` calls, sort by GPU duration
- In Pipeline State: confirm which shader variant is selected
- Compare dispatch count, work group dimensions, and per-dispatch time between old and new
- Look for unexpected pipeline barriers, memory copies, or synchronization

---

## Recommended Experiment Order

1. **Experiment 1A** (SPIR-V dump from runtime) -- most direct proof, ~30 min
2. **Experiment 4** (glslang swap) -- if Exp 1 confirms, this proves the fix, ~10 min
3. **Experiment 3** (glslang bisection) -- find the exact commit, ~1 hour
4. **Experiment 2** (fp32 per-layer profiling) -- supplementary data, ~20 min
5. **Experiment 5** (RenderDoc) -- only if everything else fails

Experiments 1A and 4 together would be sufficient to confirm the root cause AND prove the fix.
The others are for completeness and upstream reporting.

---

## Potential Fixes (once root cause is confirmed)

### Option A: Pin glslang to known-good version

Simplest fix. In the NCNN submodule, override the glslang submodule to the old commit:

```bash
git -C src/ncnn/glslang checkout 4afd691
```

Risk: May miss glslang bug fixes or new features needed by newer NCNN shaders.

### Option B: Pre-compile SPIR-V at build time

Instead of relying on runtime GLSL compilation, compile shaders to SPIR-V during the CMake
build using a known-good glslangValidator and embed the SPIR-V directly. This would require
patching NCNN's shader loading to accept pre-compiled SPIR-V.

Risk: Significant engineering effort. NCNN's runtime compilation is designed for portability
(different GPU capabilities get different preprocessor defines).

### Option C: Report upstream

File an issue on [https://github.com/nihui/ncnn](https://github.com/nihui/ncnn) or [https://github.com/Tencent/ncnn](https://github.com/Tencent/ncnn) describing:

- Hardware: Intel Iris Xe Graphics (ADL GT2)
- Regression: 4x slowdown in fp32, 1.6x in fp16
- Root cause: glslang version produces suboptimal SPIR-V for the Winograd GEMM kernel
- Evidence: Same GLSL source, different SPIR-V, different performance
- Affected shader: `convolution_pack4_3x3s1d1_winograd_gemm.comp`

This is the most sustainable long-term fix.

### Option D: Keep fp16 arithmetic as a workaround

The current `use_fp16_arithmetic = true` setting reduces the regression from 4x to 1.5x.
This is already applied in `src/realesrgan.cpp`. Quality has been visually confirmed as
acceptable. This is a pragmatic workaround while waiting for an upstream fix.

---

## Key File Paths

| File | Description |
| --- | --- |
| `src/realesrgan.cpp` | Main processing code, has profiling instrumentation and `use_fp16_arithmetic = true` |
| `src/ncnn/src/gpu.cpp` | NCNN GPU initialization, shader macro definitions, GLSL compilation |
| `src/ncnn/src/layer/vulkan/convolution_vulkan.cpp` | Pipeline creation, dispatch, Winograd selection |
| `src/ncnn/src/layer/vulkan/shader/convolution_pack4_3x3s1d1_winograd_gemm.comp` | The hot-path GEMM shader source |
| `src/ncnn/glslang/` | Bundled glslang submodule (the prime suspect) |
| `build/` | New NCNN (20260113) build artifacts |
| `build-old/` | Old NCNN (20220420) build artifacts |
| `build-profile/` | Profile build (NCNN_BENCHMARK=ON) |
| `build/src/ncnn/src/layer/vulkan/shader/convolution_pack4_3x3s1d1_winograd_gemm.text2hex.txt` | New preprocessed GLSL |
| `build-old/src/ncnn/src/convolution_pack4_3x3s1d1_winograd_gemm.text2hex.txt` | Old preprocessed GLSL |
| `logs/` | All profiling logs from 2026-03-30 |
| `.claude/projects/-home-andreas-repo-upscayl-ncnn/memory/project_ncnn_perf_regression.md` | Full investigation history |

## Notes for Future Claude Sessions

- The `src/ncnn` submodule should be at commit 20260113 (new). If it's at a different commit,
  someone may have been running experiments.
- `src/realesrgan.cpp` has profiling instrumentation guarded by `#if NCNN_BENCHMARK` and
  `use_fp16_arithmetic = true`. Both are intentional changes from this investigation.
- The `build-old/` directory contains a complete build of NCNN 20220420. It was built with
  the submodule temporarily checked out to the old version, then restored.
- The memory file at `project_ncnn_perf_regression.md` has the complete investigation history
  including all ruled-out causes, profiling data, and shader analysis.
- The user is new to C/C++ -- explain steps clearly.
