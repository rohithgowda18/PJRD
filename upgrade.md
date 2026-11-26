# PJRD Upgrade: Adaptive Thread Selection (`-nth -2`) & Batched Task Execution

## Overview

Implemented **Adaptive Thread Selection** in PJRD via `-nth -2` and fixed a pre-existing thread-pool task-queue deadlock for small restart intervals (e.g., `-ri 1`), without modifying any JPEG encoding algorithms (DCT, quantization, RDOQ, Huffman/arithmetic coding, or bitstream generation).

All existing `-nth` modes retain their original behavior:
- `-nth 0`: Serial execution (no thread pool)
- `-nth -1` (default): Use all detected hardware threads (`std::thread::hardware_concurrency()`)
- `-nth N` ($N \ge 1$): Fixed cap `min(N, hardware_concurrency)`
- `-nth -2`: **Adaptive Thread Selection**

---

## 1. Adaptive Thread Selection Algorithm

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

## 2. Thread-Pool Queue Deadlock Fix (`-ri 1`)

- **Root Cause:** `xThreadPoolInterfaceFunction::executeStoredTasks()` previously submitted all `m_StoredTasks` in a single batch before calling `waitUntilTasksFinished()`. On `512x512` with `-ri 1`, `1024` slice tasks exceeded `m_WaitingTasks` (`513`) and `m_CompletedTasks` (`512`) ring-buffer capacities, causing `xPtrRing::insertWait` / `removeWait` to overflow/deadlock.
- **Fix:** Updated `xThreadPoolInterfaceFunction::executeStoredTasks()` in `libs/pmbb-core/src/xThreadPool.cpp` to submit and wait in sequential batches of `MaxBatchSize = max(1, getMaxNumTasksInBatch() - 1)` whenever `TotalTasks > MaxBatchSize`, preserving task order, synchronization, and `m_UnusedTasks` recycling with zero overhead for smaller batches.

---

## 3. Modified & Added Files

| File | Summary of Changes |
| :--- | :--- |
| `libs/pmbb-core/src/xThreadPool.cpp` | Batched submission in `xThreadPoolInterfaceFunction::executeStoredTasks()` to prevent queue overflow/deadlock when tasks exceed queue capacity. |
| `libs/pmbb-jpeg/src/xJPEG_CodecCommon.h` | Declared `calcNumMCUsInHeight`, `calcNumParallelWorkUnits`, and `selectAdaptiveThreadCount`. |
| `libs/pmbb-jpeg/src/xJPEG_CodecCommon.cpp` | Implemented vertical MCU count, MT/MT+R work-unit calculation, and adaptive thread selection logic. |
| `apps/PJRD/src/xAppPJRD.h` | Added `m_AvailableWorkUnits = 0` member to `xAppEnc`. |
| `apps/PJRD/src/xAppPJRD.cpp` | Added `-nth -2` handling in `setupMultithreading()`, updated `c_HelpString`, `formatConfiguration()`, and `formatMultithreading()` to report `ThreadSelectionMode`, `AvailableWorkUnits`, and `NumberOfThreadsUsed`. |
| `libs/pmbb-jpeg/CMakeLists.txt` | Registered `"CodecCommon"` in `LIST_TESTS`. |
| `libs/pmbb-jpeg/test/test-CodecCommon.cpp` | Added `doctest` unit tests for MCU dimensions, MT/MT+R work units, large restart intervals, `hardware_concurrency() == 0` fallback, and serial fallback. |
| `CMakeLists.txt` | Restored comment header at lines 68–70 to fix a stray pasted text block. |

---

## 4. Build & Unit-Test Results (Visual Studio 2026 Build Tools, MSVC 19.52.37029)

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
  - `pmbb-core-TEST-xThreadPool`: **Passed** (`0.21 sec`)
  - `pmbb-jpeg-TEST-CodecCommon`: **Passed** (`0.10 sec`)
  - `pmbb-jpeg-TEST-JFIF`: **Failed** (`0.37 sec`, 23 assertions in `test-JFIF.cpp:172,194`). Independent execution confirmed this is a pre-existing upstream bug in `xJFIF::SeekAllMarkers()` (`AbsPos = FilePos + Pos` does not account for `SeekBuffer.modifyRead(2 + Pos)`), unrelated to our changes.

---

## 5. Runtime Verification & Benchmarks (12-Thread CPU)

### A. 7-Case Runtime Regression (`example/PJRD/baboon.png`, 512×512, `-ff PNG -q 90 -v 3`, with `-r` decode)

| Case | Flags | Mode / Work Units | Threads Used | JPEG Size | Recon Size | PSNR-Y | Status |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | `-nth 0 -ri 0` | `Serial` | `0 (serial)` | `120286 B` | `597159 B` | `36.834146 dB` | Passed |
| 2 | `-nth -1 -ri 0` | `All` | `12` | `120286 B` | `597159 B` | `36.834146 dB` | Passed |
| 3 | `-nth -2 -ri 0` | `Adaptive` (`32` MT MCU rows) | `12` | `120286 B` | `597159 B` | `36.834146 dB` | Passed |
| 4 | `-nth -2 -ri -1` | `Adaptive` (`32` MT+R slices) | `12` | `120398 B` | `597159 B` | `36.834146 dB` | Passed |
| 5 | `-nth -2 -ri 1` | `Adaptive` (`1024` MT+R slices) | `12` | `123286 B` | `597159 B` | `36.834146 dB` | Passed |
| 6 | `-nth -2 -ri 16` | `Adaptive` (`64` MT+R slices) | `12` | `120496 B` | `597159 B` | `36.834146 dB` | Passed |
| 7 | `-nth -2 -ri 10000` | `Adaptive` (`1` MT+R slice) | `0 (serial)` | `120292 B` | `597159 B` | `36.834146 dB` | Passed (serial fallback) |

- **Output correctness:** SHA-256 hashes of `1_st_ri0.jpeg`, `2_mt_orig_ri0.jpeg`, and `3_mt_adap_ri0.jpeg` are **bit-identical**. MT+R outputs are slightly larger due to `DRI`/`RSTm` markers and DC predictor resets at slice boundaries, with identical `PSNR-Y` (`36.834146 dB`).

### B. Encoding-Only Benchmark (5-Run Average, `baboon.png`, `-ps 512x512 -ff PNG -q 90 -v 3`, no `-r`)

| Flags | Mode / Work Units | Threads Used | JPEG Size | Avg `Encode` Time | Avg `ProcessingTime` | Avg `ApplicationTime` | Encode Speedup vs `-nth 0` |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `-nth 0 -ri 0` | `Serial` | `0 (serial)` | `120286 B` | `261.74 ms` | `0.2836 s` | `0.2886 s` | `1.00x` |
| `-nth -1 -ri 0` | `All` | `12` | `120286 B` | `62.42 ms` | `0.0816 s` | `0.0888 s` | `4.19x` |
| `-nth -2 -ri 0` | `Adaptive` (`32` MT MCU rows) | `12` | `120286 B` | `65.77 ms` | `0.0848 s` | `0.0920 s` | `3.98x` |
| `-nth -2 -ri -1` | `Adaptive` (`32` MT+R slices) | `12` | `120398 B` | `58.41 ms` | `0.0738 s` | `0.0798 s` | `4.48x` |
| `-nth -2 -ri 1` | `Adaptive` (`1024` MT+R slices) | `12` | `123286 B` | `65.07 ms` | `0.0860 s` | `0.1018 s` | `4.02x` |

### C. Small-Image Thread-Pool Overhead Investigation (5-Run Average, `-ff PNG -q 90 -ri 0 -v 3`, no `-r`)

| Image | Mode | Threads Used | Avg `Encode` Time | Avg `ProcessingTime` | Avg `ApplicationTime` | `App - Proc` | Encode Speedup | App Speedup |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `64x64` | Serial (`-nth 0`) | `0 (serial)` | `2164.02 us` | `0.0038 s` | `0.0092 s` | `5.40 ms` | `1.00x` | `1.00x` |
| `64x64` | Adaptive (`-nth -2`) | `4` | `1487.43 us` | `0.0028 s` | `0.0080 s` | `5.20 ms` | **`1.45x`** | **`1.15x`** |
| `128x128` | Serial (`-nth 0`) | `0 (serial)` | `8409.28 us` | `0.0106 s` | `0.0184 s` | `7.80 ms` | `1.00x` | `1.00x` |
| `128x128` | Adaptive (`-nth -2`) | `8` | `4080.12 us` | `0.0064 s` | `0.0122 s` | `5.80 ms` | **`2.06x`** | **`1.51x`** |
| `256x256` | Serial (`-nth 0`) | `0 (serial)` | `37684.30 us` | `0.0424 s` | `0.0508 s` | `8.40 ms` | `1.00x` | `1.00x` |
| `256x256` | Adaptive (`-nth -2`) | `12` | `11391.20 us` | `0.0164 s` | `0.0228 s` | `6.40 ms` | **`3.31x`** | **`2.23x`** |
| `512x512` | Serial (`-nth 0`) | `0 (serial)` | `231643.39 us` | `0.2518 s` | `0.2566 s` | `4.80 ms` | `1.00x` | `1.00x` |
| `512x512` | Adaptive (`-nth -2`) | `12` | `67954.98 us` | `0.0896 s` | `0.0974 s` | `7.80 ms` | **`3.41x`** | **`2.63x`** |
| `1920x1080` | Serial (`-nth 0`) | `0 (serial)` | `1275711.78 us` | `1.3652 s` | `1.3788 s` | `13.60 ms` | `1.00x` | `1.00x` |
| `1920x1080` | Adaptive (`-nth -2`) | `12` | `328470.77 us` | `0.3992 s` | `0.4096 s` | `10.40 ms` | **`3.88x`** | **`3.37x`** |

**Conclusion:** Even at `64x64` (4 MCU rows), 4-thread adaptive execution is faster than serial (`1.45x` encode, `1.15x` total application time), so no minimum-work threshold was added.

---

## 6. Repository Status

- Pushed to `https://github.com/rohithgowda18/PJRD.git` (`main`, commit `bbc0094`).
- Known pre-existing limitation: `pmbb-jpeg-TEST-JFIF` fails due to `xJFIF::SeekAllMarkers()`.
