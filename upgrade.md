# PJRD Upgrade: Adaptive Thread Selection (`-nth -2`)

## Overview

Implemented **Adaptive Thread Selection** in PJRD via `-nth -2` without modifying any JPEG encoding algorithms (DCT, quantization, RDOQ, Huffman/arithmetic coding, or bitstream generation) or the `xThreadPool` implementation.

All existing `-nth` modes retain their original behavior:
- `-nth 0`: Serial execution (no thread pool)
- `-nth -1` (default): Use all detected hardware threads (`std::thread::hardware_concurrency()`)
- `-nth N` ($N \ge 1$): Fixed cap `min(N, hardware_concurrency)`
- `-nth -2`: **Adaptive Thread Selection**

---

## Selection Algorithm

1. **Hardware Concurrency Fallback:** If `std::thread::hardware_concurrency()` returns `0`, fall back to `1`.
2. **Parallel Work-Unit Calculation (`xCodecCommon::calcNumParallelWorkUnits`):**
   - **MT mode (`-ri 0`):** Number of MCU rows (`calcNumMCUsInHeight(PictureSize, ChromaFormat)`).
   - **MT+R mode (`-ri > 0` or `-ri -1` resolved to 1 MCU row):** Number of restart slices:
     $$\left\lceil \frac{\text{NumMCUsInWidth} \times \text{NumMCUsInHeight}}{\text{RestartIntervalUsed}} \right\rceil$$
     computed with 64-bit integer arithmetic to avoid overflow and floating-point rounding issues.
3. **Thread Count Selection (`xCodecCommon::selectAdaptiveThreadCount`):**
   $$\text{SelectedThreads} = \min(\text{HardwareThreads}, \text{AvailableWorkUnits})$$
   If `SelectedThreads <= 1`, returns `0` so `xAppEnc::setupMultithreading()` uses the existing serial execution path without allocating `xThreadPool`.

---

## Modified & Added Files

| File | Summary of Changes |
| :--- | :--- |
| `libs/pmbb-jpeg/src/xJPEG_CodecCommon.h` | Declared `calcNumMCUsInHeight`, `calcNumParallelWorkUnits`, and `selectAdaptiveThreadCount`. |
| `libs/pmbb-jpeg/src/xJPEG_CodecCommon.cpp` | Implemented vertical MCU count, MT/MT+R work-unit calculation, and adaptive thread selection logic. |
| `apps/PJRD/src/xAppPJRD.h` | Added `m_AvailableWorkUnits = 0` member to `xAppEnc`. |
| `apps/PJRD/src/xAppPJRD.cpp` | Added `-nth -2` handling in `setupMultithreading()`, updated `c_HelpString`, `formatConfiguration()`, and `formatMultithreading()` to report `ThreadSelectionMode`, `AvailableWorkUnits`, and `NumberOfThreadsUsed`. |
| `libs/pmbb-jpeg/CMakeLists.txt` | Registered `"CodecCommon"` in `LIST_TESTS`. |
| `libs/pmbb-jpeg/test/test-CodecCommon.cpp` | Added `doctest` unit tests for MCU dimensions, MT/MT+R work units, large restart intervals, `hardware_concurrency() == 0` fallback, and serial fallback. |
| `CMakeLists.txt` | Restored comment header at lines 68–70 to fix a stray pasted text block. |

---

## Build & Unit-Test Results (Visual Studio 2026 Build Tools, MSVC 19.52.37029)

- **Release Build (`buildVS2026`):**
  ```powershell
  cmake -G "Visual Studio 18 2026" -A x64 -S . -B buildVS2026 -DPMBB_GENERATE_MULTI_MICROARCH_LEVEL_BINARIES:BOOL=True -DPMBB_GENERATE_SINGLE_APP_WITH_WITH_RUNTIME_DISPATCH:BOOL=True
  cmake --build buildVS2026 --config Release --parallel 8
  ```
  Result: Built `buildVS2026\PJRD\Release\PJRD.exe` (`exit code 0`).

- **Debug Build & CTest (`buildTestsVS2026`):**
  ```powershell
  cmake -G "Visual Studio 18 2026" -A x64 -S . -B buildTestsVS2026 -DPMBB_GENERATE_TESTING:BOOL=True -DPMBB_GENERATE_MULTI_MICROARCH_LEVEL_BINARIES:BOOL=False
  cmake --build buildTestsVS2026 --config Debug --parallel 8
  ctest --test-dir buildTestsVS2026 -C Debug --output-on-failure
  ```
  Result: **20 / 21 tests passed**:
  - `pmbb-jpeg-TEST-CodecCommon` **Passed** (`0.11 sec`).
  - `pmbb-jpeg-TEST-JFIF` failed (`test-JFIF.cpp:172,194`), a pre-existing upstream failure unrelated to these changes.

---

## Runtime Verification & Benchmark (`example/PJRD/baboon.png`, 512×512, `-ff PNG -q 90`)

Measured on a 12-thread CPU (`HardwareConcurency = 12`):

### Functional Verification (with `-r` decode & PSNR check)

| Case | Flags | Mode / Work Units | Threads Used | JPEG Size | PSNR-Y | Status |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | `-nth 0 -ri 0` | `Serial` | `0 (serial)` | `120286 B` | `36.834146 dB` | Passed |
| 2 | `-nth -1 -ri 0` | `All` | `12` | `120286 B` | `36.834146 dB` | Passed |
| 3 | `-nth -2 -ri 0` | `Adaptive` / `32 (MT MCU rows)` | `12` | `120286 B` | `36.834146 dB` | Passed |
| 4 | `-nth -2 -ri -1` | `Adaptive` / `32 (MT+R slices)` | `12` | `120398 B` | `36.834146 dB` | Passed |
| 5 | `-nth -2 -ri 1` | `Adaptive` / `1024 (MT+R slices)` | `12` | — | — | Deadlocked (pre-existing queue limit) |
| 5b | `-nth -2 -ri 16` | `Adaptive` / `64 (MT+R slices)` | `12` | `120496 B` | `36.834146 dB` | Passed |
| 6 | `-nth -2 -ri 10000` | `Adaptive` / `1 (MT+R slices)` | `0 (serial)` | `120292 B` | `36.834146 dB` | Passed (serial fallback) |

### Performance Comparison (5-run average, `-v 3`, no `-r` decode)

| Case | Flags | Selected Threads | Work Units | JPEG Size | Avg Processing Time | Avg Application Time | Speedup vs `-nth 0` (Proc / App) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | `-nth 0 -ri 0` | `0 (serial)` | N/A | `120286 B` | `0.163 s` | `0.166 s` | `1.00x` / `1.00x` |
| 2 | `-nth -1 -ri 0` | `12` | N/A | `120286 B` | `0.033 s` | `0.037 s` | `4.94x` / `4.49x` |
| 3 | `-nth -2 -ri 0` | `12` | `32 (MT MCU rows)` | `120286 B` | `0.032 s` | `0.036 s` | `5.09x` / `4.61x` |
| 4 | `-nth -2 -ri -1` | `12` | `32 (MT+R slices)` | `120398 B` | `0.030 s` | `0.033 s` | `5.43x` / `5.03x` |

---

## Known Limitations

- **Small manual `-ri` on large images (`-ri 1` on 512×512):** `xAppEnc::setupMultithreading()` sets `MaxNumTasks = m_PictureSize.getY() + 1` (`513`) and `xAdvancedEncoder::create()` initializes `m_ThPI` with `m_PictureSize.getY()` (`512`). When `m_NumOfSlices` exceeds `512` (e.g., `1024` slices for `-ri 1` at 512×512), `executeStoredTasks()` deadlocks inserting all tasks into the fixed-size ring buffer.
- **`pmbb-jpeg-TEST-JFIF`:** Pre-existing test failure in `libs/pmbb-jpeg/test/test-JFIF.cpp`.
