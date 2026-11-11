# D1: Frame-Time Spike Attribution (Vulkan & D3D12)

> **Date**: 2026-11-10
> **Hardware / Environment**: Windows 11, NVIDIA GeForce RTX 3060 Ti (`vendor 0x10DE`, `device 0x2489`, 8 GB VRAM), NVIDIA Driver `621.14` (`0x9B404000` / `0x9B438000`, Vulkan API `1.4.357`).
> **Reproducible Script**: [`scripts/discovery/d1-stutter-attribution.mjs`](../../scripts/discovery/d1-stutter-attribution.mjs) (`node scripts/discovery/d1-stutter-attribution.mjs`)
> **Decision Unlocked**: **GO** on the shader/pipeline initiative (with explicit methodological caveats below). In Vulkan (the default backend), **69.1% of all CSV frame-time spikes (89.3% of excess spike time lost)** coincide by timestamp with `Creating graphics pipeline state` or [`fh1_frame_cost`](../../tools/source/rexglue-sdk/src/graphics/fh1_frame_cost.h#L23-L71) slow-frame windows, and **82.1% of all `fh1_frame_cost` slow frames (98.9% of measured GPU subsystem work in slow frames)** are directly recorded in `shader_ms + pipeline_ms` while texture cache misses account for **`0.0%` (`0.6 ms` total)**.

---

## 1. Executive Summary & Attribution Tables

### 1.1 Per-Frame CSV Spike Attribution (`*.performance.csv` + Paired `.log` Timestamp Correlation)

- **Spike Definition (per D1 spec)**: `frame_time_us > 2 * rolling_median(60 frames)` **AND** `frame_time_us > 25,000 us` (`25 ms`).
- **Dataset**: All **198** CSV files under [`reports/`](../../reports) and [`game-data/runtime/logs/`](../../game-data/runtime/logs) (**177** valid non-empty CSV sessions totaling **394,038 frames** / **117.7 minutes** of runtime; 21 files are 0-byte or header-only from immediate exits or DLL split sessions).
- **Attribution Method (Inference via Timestamp Correlation)**: Because `REXGLUE_ENABLE_PERF_COUNTERS` is compiled out in `Release` builds ([`tools/source/rexglue-sdk/CMakeLists.txt:31`](../../tools/source/rexglue-sdk/CMakeLists.txt#L31)), the `pipeline_cache_misses` and `texture_cache_misses` columns are `0` in all 198 Release CSVs (see Section 2.3). Consequently, CSV spike attribution is an **inference by timestamp correlation** between reconstructed CSV frame timestamps (anchored on `[Majin performance] Release counter mode:` at the first swap) and paired `.log` lines (`Creating graphics pipeline state`, `GPU slow frames:`, `[Majin loading]`, `[Majin save]`). Direct in-engine per-frame timer verification is provided independently in Section 1.2 via [`fh1_frame_cost`](../../tools/source/rexglue-sdk/src/graphics/fh1_frame_cost.h#L23-L71).

#### Vulkan Backend (76 valid CSV sessions, 113,241 frames, 2,316.2 s total runtime)

| Cause Category (Inferred via Log Timestamp Correlation) | Spikes (`N = 269`) | % of Spikes | Total Spike Duration (ms) | Excess Time Lost > Median (ms) | % of Excess Time Lost |
| :--- | ---: | ---: | ---: | ---: | ---: |
| **Pipeline / Shader Miss** (`Creating graphics pipeline state` / `fh1_frame_cost`) | **186** | **69.1%** | **64,062.2 ms** | **60,660.3 ms** | **89.3%** |
| **Boot / Level Load / Save I/O** (`[Majin loading]`, `[Majin save]`, audio/title init) | 59 | 21.9% | 6,967.2 ms | 5,727.8 ms | 8.4% |
| **Window / Swapchain Recreation** (`VulkanPresenter: Created ... swapchain`) | 2 | 0.7% | 94.1 ms | 60.7 ms | 0.1% |
| **Unexplained / Guest Pacing Slip** (single-frame 33->51..71 ms VSync/pacing step) | 22 | 8.2% | 1,874.9 ms | 1,494.7 ms | 2.2% |
| **Total (Vulkan)** | **269** | **100.0%** | **72,998.5 ms** | **67,943.5 ms** | **100.0%** |

#### D3D12 Backend (101 valid CSV sessions, 280,797 frames, 4,745.3 s total runtime — Directional Proxy)

> *Methodological Note on D3D12*: Unlike Vulkan, D3D12 logs per-draw `Creating graphics pipeline with VS ...` at `REXGPU_DEBUG` level ([`tools/source/rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp:2685-2689`](../../tools/source/rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp#L2685-L2689)), which is suppressed in default `info` logs. However, D3D12 logs exact startup cache counts (`Translated X shaders` and `Created Z graphics pipelines ... from the storage`, [`lines 515, 690`](../../tools/source/rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp#L515-L690)), allowing sessions to be grouped into **Cold / Growing-Cache sessions** (where new shaders/PSOs were compiled during gameplay) and **Warm-Cache sessions** (where `0` new PSOs were compiled). Because these are unstandardized development sessions with varying routes and durations rather than a controlled A/B benchmark, this D3D12 comparison is a **directional proxy** rather than a controlled experiment.

| Session / Cause Group (Directional Proxy) | Spikes (`N = 295`) | % of Spikes | Total Spike Duration (ms) | Excess Time Lost > Median (ms) | % of Excess Time Lost |
| :--- | ---: | ---: | ---: | ---: | ---: |
| **Cold / Growing-Cache Sessions** (13 sessions, 75,148 frames; **338.5 ms lost / 1k frames**) | **132** | **44.7%** | **27,948.4 ms** | **26,071.8 ms** | **66.2%** |
| **Warm-Cache: Boot / Level Load / Save I/O** (88 warm sessions, 205,649 frames) | 139 | 47.1% | 14,503.0 ms | 11,927.0 ms | 30.3% |
| **Warm-Cache: Window / Focus / Capture** | 5 | 1.7% | 264.0 ms | 180.7 ms | 0.5% |
| **Warm-Cache: Unexplained / Guest Pacing** (**only 5.7 ms lost / 1k frames**) | 19 | 6.4% | 1,498.1 ms | 1,181.0 ms | 3.0% |
| **Total (D3D12)** | **295** | **100.0%** | **44,213.5 ms** | **39,360.4 ms** | **100.0%** |

- Directionally across the D3D12 dataset, non-I/O mid-gameplay spikes drop from **136.6 ms/1k frames** (`67` spikes in `75,148` frames during cold/growing sessions) to **5.7 ms/1k frames** (`19` minor spikes in `165,952` frames during warm sessions). Because session routes and durations differ across logs, this **~95.8% difference is strictly directional** and should not be cited as a controlled benchmark reduction (controlled baseline runs are deferred to D8).

---

### 1.2 Direct Sub-Millisecond GPU Telemetry Attribution (Vulkan `fh1_frame_cost`)

Starting with the `2026-11-08 15:56` build, the Vulkan command processor records exact per-frame wall-clock timers for every GPU subsystem via [`rex::graphics::fh1_frame_cost::Frame`](../../tools/source/rexglue-sdk/src/graphics/fh1_frame_cost.h#L23-L71) and emits a `GPU slow frames:` summary line whenever frames exceed `max(50 ms, 1.5x typical_ms)` after the initial 30-second boot window. Unlike the CSV correlation in Section 1.1, [`fh1_frame_cost`](../../tools/source/rexglue-sdk/src/graphics/fh1_frame_cost.h#L23-L71) is **direct in-engine instrumentation** inside [`VulkanCommandProcessor`](../../tools/source/rexglue-sdk/src/graphics/vulkan/command_processor.cpp#L1174-L1229) and [`VulkanPipelineCache`](../../tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L1688-L1927).

Across all **203** runtime logs, **29 slow-frame windows** (**67 slow frames total**) were recorded:

| Subsystem / Cause (`fh1_frame_cost`) | Slow Frames Affected (`N = 67`) | % of Slow Frames | Total Measured Subsystem Time Across Slow Frames | % of Measured Slow-Frame Work |
| :--- | ---: | ---: | ---: | ---: |
| **PSO Creation + Driver Cache + Lookahead Wait** (`pipeline_ms`) | **55** | **82.1%** | **10,553.5 ms** | **97.5%** |
| **Guest Shader Translation** (`shader_ms`) | *(subset of the 55 frames above)* | *(see above)* | **148.4 ms** | **1.4%** |
| **Subtotal: Shaders + Pipelines (`shader_ms + pipeline_ms`)** | **55** | **82.1%** | **10,701.9 ms** | **98.9%** |
| **Guest Command Wait (`WAIT_REG_MEM` / `guest_wait_ms`)** | 4 | 6.0% | 97.0 ms | 0.9% |
| **Guest Memory Uploads (`upload_ms`)** | *(background across frames)* | 0.0% primary | 20.9 ms | 0.2% |
| **Texture Cache (`texture_create_ms + texture_load_ms`)** | **0** | **0.0%** | **0.6 ms** | **0.006%** |
| **Render Targets (`render_target_ms`)** | **0** | **0.0%** | **0.0 ms** | **0.0%** |
| **Resolve Readbacks (`readback_ms + readback_wait_ms`)** | **0** | **0.0%** | **0.0 ms** | **0.0%** |
| **GPU Fence Waits (`gpu_wait_ms`)** | **0** | **0.0%** | **0.0 ms** | **0.0%** |
| **Minor VSync / Pacing Step (51–71 ms vs 33 ms typical)** | 8 | 11.9% | *(unattributed ~18–38 ms step)* | — |

**Key Observations from `fh1_frame_cost`:**
1. **100% of severe spikes (`worst >= 94 ms`, up to `448 ms`) are caused by `shader_ms + pipeline_ms`.** All 12 non-pipeline slow frames (4 `guest_wait_ms` + 8 `unclear`) had `worst` between **51 ms and 71 ms** (a single missed 30 FPS / 33.3 ms frame boundary).
2. **Texture cache misses do NOT cause frame-time spikes.** Across all 67 slow frames (even in frames that loaded 40–64 textures / 45–56 MB of guest texture data, e.g., [`runtime-20261108-161438.log:441`](../../game-data/runtime/logs/runtime-20261108-161438.log#L441)), `texture_create_ms + texture_load_ms` totaled **0.6 ms** (`< 0.01 ms` per slow frame on average). The texture cache memory limits tuned in [`src/majin_app.h:220-231`](../../src/majin_app.h#L220-L231) (`4096 MB` soft / `8192 MB` hard) completely prevent texture eviction (`0.0 evicted` in every log).
3. **Preview for D2 (with `pipeline_ms` caveat)**: Within the `10,701.9 ms` of shader/pipeline work in slow frames, `pipeline_ms` (`10,553.5 ms`, `98.6%`) dwarfs guest shader translation (`shader_ms` = `148.4 ms`, `1.4%`) by `~71:1`. However, as decomposed in [`docs/discovery/D2-miss-cost-breakdown.md`](D2-miss-cost-breakdown.md), `pipeline_ms` bundles raw `vkCreateGraphicsPipelines`, per-pipeline `.vkpc` `~2 MB` blob serialization overhead, and multi-PSO lookahead batch waiting (`Fh1WaitForAhead`), so `pipeline_ms` must not be interpreted as pure single-PSO driver compilation time.

---

## 2. SDK Telemetry Audit & Methodological Caveats

### 2.1 Caveat Check: What does `pipeline_cache_misses` count?

We inspected [`tools/source/rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp:900-1013`](../../tools/source/rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp#L900-L1013) (`PipelineCache::ConfigurePipeline`):

1. **Guest shader analysis and translation** (`vertex_shader->shader().AnalyzeUcode(...)` and `TranslateAnalyzedShader(...)`) execute first at [`lines 927-981`](../../tools/source/rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp#L927-L981) **without** touching any `rex::perf` counter.
2. Next, `GetCurrentStateDescription(...)` builds the full `PipelineDescription` key (shader hashes + modifications + render target formats + depth/stencil/blend/raster state) at [`lines 984-990`](../../tools/source/rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp#L984-L990).
3. If `description` matches `current_pipeline_` ([`lines 992-997`](../../tools/source/rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp#L992-L997)), `ConfigurePipeline` returns `true` immediately (*without* incrementing `kPipelineCacheHits`).
4. Next, it searches the in-memory `pipelines_` hash map (`pipelines_.equal_range(hash)`, [`lines 1000-1010`](../../tools/source/rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp#L1000-L1010)). If found, it calls `PROFILE_PIPELINE_CACHE_HIT()` ([`line 1005`](../../tools/source/rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp#L1005)) and returns.
5. Only when the full `PipelineDescription` is not in `pipelines_` does it call **`PROFILE_PIPELINE_CACHE_MISS()` ([`line 1012`](../../tools/source/rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp#L1012))**, allocate a new `Pipeline`, and call `CreateD3D12Pipeline(runtime_description)` ([`line 1038`](../../tools/source/rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp#L1038)).

**Conclusion on Counter Definition**:
- `pipeline_cache_misses` (`kPipelineCacheMisses`) counts **unique PSO cache misses (PSO creations)** only — **not** shader translations.
- Furthermore, in [`tools/source/rexglue-sdk/src/core/perf/counter.cpp:57-75, 104-115`](../../tools/source/rexglue-sdk/src/core/perf/counter.cpp#L57-L115), `kDrawCalls`, `kCommandBufferStalls`, `kTextureCacheMisses`, and `kPipelineCacheMisses` have `kIsGauge = false`, meaning `ResetFrameCounters()` atomically snapshots and zeroes (`exchange(0)`) them every frame.

### 2.2 Three SDK Telemetry Bugs Uncovered During D1

While auditing the 198 `.performance.csv` files and SDK perf counters, we uncovered three critical telemetry implementation details that must be accounted for in D2–D8:

1. **Bug 1 — `REXGLUE_ENABLE_PERF_COUNTERS` is compiled out in `Release` builds ([`CMakeLists.txt:31`](../../tools/source/rexglue-sdk/CMakeLists.txt#L31))**:
   - In [`tools/source/rexglue-sdk/CMakeLists.txt:31`](../../tools/source/rexglue-sdk/CMakeLists.txt#L31) (and `src/core/CMakeLists.txt:137`, `src/system/CMakeLists.txt:108`), the macro is gated by `$<$<NOT:$<CONFIG:Release>>:REXGLUE_ENABLE_PERF_COUNTERS>`.
   - In `Release` builds, all `PROFILE_*` macros in [`tools/source/rexglue-sdk/include/rex/perf/counter.h:163-188`](../../tools/source/rexglue-sdk/include/rex/perf/counter.h#L163-L188) compile to empty no-ops, and [`CommandProcessor::ExecutePacketType3_XE_SWAP`](../../tools/source/rexglue-sdk/src/graphics/command_processor.cpp#L1370-L1406) runs the `#if !defined(REXGLUE_ENABLE_PERF_COUNTERS)` path, which sets **only** `kFrameTimeUs` and `kFps`. Consequently, columns `draw_calls` through `pipeline_cache_misses` are `0` in all 198 existing Release `.performance.csv` files.
2. **Bug 2 — The Vulkan backend never calls `PROFILE_*` macros (`rex::perf`)**:
   - Even in non-Release builds, `PROFILE_DRAW_CALL()`, `PROFILE_CMD_BUFFER_STALL()`, `PROFILE_TEXTURE_CACHE_HIT/MISS()`, and `PROFILE_PIPELINE_CACHE_HIT/MISS()` exist **only** in `src/graphics/d3d12/*`.
   - The Vulkan backend (`src/graphics/vulkan/*`) instead instruments everything through [`rex::graphics::fh1_frame_cost`](../../tools/source/rexglue-sdk/src/graphics/fh1_frame_cost.h#L23-L71), which can write a rich 46-column per-frame CSV (`t, ms, cpu_ms, gpu_wait_ms, guest_wait_ms, draws, textures_created, ..., shaders, shader_ms, pipelines, pipeline_ms, ...`) whenever `--fh1_frame_cost_csv=<path>` is passed ([`src/graphics/vulkan/command_processor.cpp:214, 1174-1229`](../../tools/source/rexglue-sdk/src/graphics/vulkan/command_processor.cpp#L1174-L1229)).
3. **Bug 3 — `fh1_frame_cost::Report::MainCause()` mislabels synchronous lookahead pipeline waits as `"the game"` ([`fh1_frame_cost.h:412`](../../tools/source/rexglue-sdk/src/graphics/fh1_frame_cost.h#L412))**:
   - When `vulkan_pipeline_lookahead` (default `true`, [`src/graphics/vulkan/fh1_pipeline_lookahead.cpp:39`](../../tools/source/rexglue-sdk/src/graphics/vulkan/fh1_pipeline_lookahead.cpp#L39)) encounters a missing PSO ([`src/graphics/vulkan/pipeline_cache.cpp:1806`](../../tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L1806)), it walks ahead in the command buffer, queues missing PSOs onto background worker threads (`creation_threads_`), and then blocks the command processor thread inside [`Fh1WaitForAhead()`](../../tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L2010-L2037).
   - While sleeping in `Fh1WaitForAhead()`, `fh1_frame_cost::Timer cost_timer(fh1_frame_cost::Current().pipeline_ms)` ([`pipeline_cache.cpp:1845, 1854, 1927`](../../tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L1845-L1927)) correctly records the wall-clock stall in `pipeline_ms` (e.g., `410.6 ms` in [`runtime-20261108-161438.log:365`](../../game-data/runtime/logs/runtime-20261108-161438.log#L365)), **but** the command processor thread's own CPU cycle counter (`QueryThreadCycleTime`) does not advance while sleeping (`9 ms CPU`).
   - Because [`Report::MainCause()`](../../tools/source/rexglue-sdk/src/graphics/fh1_frame_cost.h#L412) checks `if (slow_.cpu_per_frame && cpu > 0 && cpu + gpu_wait < 0.5 * ms) return Cause::kGame;` **before** checking `costs[]` (`Cause::kShaders`), any slow frame where the command processor slept waiting for worker threads to compile pipelines is mislabeled in the log text as `cause: the game (the command processor waited for its commands)` despite having `pipeline_ms = 91.4 ms .. 410.6 ms` and `0 ms game waits`!

### 2.3 Methodological Caveats on D1 Results

1. **CSV Spike Attribution Is Inferred by Timestamp Correlation (Not Direct Counter Attribution)**:
   - Because `REXGLUE_ENABLE_PERF_COUNTERS` is off in `Release` ([`CMakeLists.txt:31`](../../tools/source/rexglue-sdk/CMakeLists.txt#L31)), the `.performance.csv` files cannot attribute spikes directly from their `pipeline_cache_misses` column. The **69.1% of spikes (89.3% of excess time)** figure in Section 1.1 is an inference based on millisecond-level timestamp correlation between CSV frames and `.log` lines (`Creating graphics pipeline state` and `GPU slow frames:`). It is corroborated by direct in-engine [`fh1_frame_cost`](../../tools/source/rexglue-sdk/src/graphics/fh1_frame_cost.h#L23-L71) timers (Section 1.2: **82.1% of slow frames, 98.9% of measured GPU work**).
2. **D3D12 Cold-vs-Warm Comparison Is a Directional Proxy Only**:
   - The 101 D3D12 sessions compare historical development runs that differed in gameplay route, length, and test purpose rather than a controlled A/B experiment on an identical route. The observed drop from `136.6 ms / 1k frames` to `5.7 ms / 1k frames` (**~95.8% reduction**) is directional evidence that a warm PSO cache eliminates mid-gameplay spikes on D3D12, not a controlled benchmark number to cite as a formal baseline.
3. **Sample Bias (Partial Development Routes)**:
   - All 177 valid sessions come from development testing over early/partial routes of the game. While cold-cache sessions accurately reflect a player's first-run experience in those zones, total full-game shader/pipeline population and late-game growth remain unknown until D3.

---

## 3. Evidence Samples from Existing Logs & CSVs

### 3.1 Vulkan: Exact Timestamp Match Between CSV Spikes and `Creating graphics pipeline state`

In sessions where runtime PSO creations were logged at `[info]` level, every major CSV spike aligns to the millisecond with a burst of `Creating graphics pipeline state with VS ...` lines:

- **[`game-data/runtime/logs/runtime-20261108-105535.log`](../../game-data/runtime/logs/runtime-20261108-105535.log)** (Vulkan, started with 20 preloaded pipelines, compiled 91 new pipelines during gameplay; 35 CSV spikes totaling **14,984.4 ms** excess time):
  - At `10:55:45.363–10:55:45.423` ([`lines 246-257`](../../game-data/runtime/logs/runtime-20261108-105535.log#L246-L257)), 6 new PSOs are compiled on first sight of the scene (`VS 22E2D80085719B22`, `VS C4675F9686D7D8CC`, `VS 21835F97BF07CA7C`).
  - At `10:55:52.568–10:55:53.171` ([`lines 271-280`](../../game-data/runtime/logs/runtime-20261108-105535.log#L271-L280)), 6 more PSOs are compiled mid-gameplay.
- **[`reports/runtime-usa-20261106-214434.log`](../../reports/runtime-usa-20261106-214434.log)** (Vulkan, started with 19 preloaded pipelines, compiled 145 new pipelines; 18 CSV spikes totaling **11,490.9 ms** excess time):
  - **18 out of 18 spikes (100%)** coincide with `Creating graphics pipeline state with VS ...` bursts in the log.
- **[`game-data/runtime/logs/runtime-20261108-161438.log`](../../game-data/runtime/logs/runtime-20261108-161438.log)** (Vulkan, 15-minute session through cutscene and level transitions; started at 165 pipelines, ended at 332 pipelines — **167 new pipelines built**):
  - [`line 329`](../../game-data/runtime/logs/runtime-20261108-161438.log#L329): `GPU slow frames: 1 of 302 in 10 s over 50 ms (worst 264 ms, typical 33 ms) ... shaders 2.0 (2.9 ms), pipelines 0.0 (252.4 ms)`
  - [`line 343`](../../game-data/runtime/logs/runtime-20261108-161438.log#L343): `GPU slow frames: 9 of 257 in 10 s over 50 ms (worst 406 ms, typical 33 ms) ... shaders 1.7 (2.5 ms), pipelines 0.6 (195.9 ms)`
  - [`line 349`](../../game-data/runtime/logs/runtime-20261108-161438.log#L349): `GPU slow frames: 5 of 277 in 10 s over 50 ms (worst 337 ms, typical 33 ms) ... shaders 1.0 (2.5 ms), pipelines 1.0 (248.8 ms)`
  - [`line 365`](../../game-data/runtime/logs/runtime-20261108-161438.log#L365): `GPU slow frames: 1 of 301 in 10 s over 50 ms (worst 448 ms, typical 33 ms) ... shaders 2.0 (6.2 ms), pipelines 0.0 (410.6 ms)`
  - [`line 427`](../../game-data/runtime/logs/runtime-20261108-161438.log#L427): `GPU slow frames: 2 of 290 in 10 s over 50 ms (worst 429 ms, typical 33 ms) ... shaders 1.5 (3.4 ms), pipelines 1.0 (365.7 ms)`
  - Across the entire 15-minute session ([`lines 315-487`](../../game-data/runtime/logs/runtime-20261108-161438.log#L315-L487)), lookahead performed **60 synchronous waits totaling 4,348.7 ms** ([`line 487`](../../game-data/runtime/logs/runtime-20261108-161438.log#L487)) across **108 background-compiled PSOs** plus **59 inline synchronous PSO creations** on the command processor thread.

### 3.2 D3D12: Cold Cache vs. Warm Cache Progression (Directional Sample)

Comparing consecutive D3D12 development runs (`reports/runtime-usa-20261106-*.log`):

| Session Log | Preloaded Shaders | Preloaded PSOs | New PSOs Built in Run | Frames | Spikes | Excess Spike Time Lost |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| [`runtime-usa-20261106-163730.log`](../../reports/runtime-usa-20261106-163730.log) | 0 (Cold) | 0 (Cold) | **+151** | 14,913 | **36** | **5,311.3 ms** |
| [`runtime-usa-20261106-163848.log`](../../reports/runtime-usa-20261106-163848.log) | 131 | 151 | **+26** | 1,304 | **6** | **983.5 ms** |
| [`runtime-usa-20261106-163938.log`](../../reports/runtime-usa-20261106-163938.log) | 152 | 177 | **+3** | 5,743 | **3** | **388.0 ms** |
| [`runtime-usa-20261106-184055.log`](../../reports/runtime-usa-20261106-184055.log) | 182 | 215 | **0 (Warm)** | 2,795 | **4** | **134.7 ms** |
| [`runtime-usa-20261106-230337.log`](../../reports/runtime-usa-20261106-230337.log) | 0 (Cold) | 0 (Cold) | **+215** | 1,813 | **26** | **13,422.8 ms** |
| [`runtime-usa-20261106-230447.log`](../../reports/runtime-usa-20261106-230447.log) | 182 | 215 | **+1** | 4,928 | **3** | **250.0 ms** |

---

## 4. Recommendations for D2–D8

1. **Proceed to D2 & D3 (Gate Passed)**: Pipeline/shader compilation and its associated driver-cache/lookahead stalls are conclusively the primary cause of in-game frame-time spikes on Vulkan (**89.3%** of excess CSV spike ms; **98.9%** of measured `fh1_frame_cost` slow-frame GPU work), while texture cache misses are negligible (`0.6 ms` total).
2. **Use `--fh1_frame_cost_csv=<path>` for Vulkan in D2 and D8**: Passing `--fh1_frame_cost_csv` ([`tools/source/rexglue-sdk/src/graphics/vulkan/command_processor.cpp:214, 1174-1229`](../../tools/source/rexglue-sdk/src/graphics/vulkan/command_processor.cpp#L1174-L1229)) exports direct per-frame `shader_ms`, `pipeline_ms`, `bg_pipelines`, and `bg_pipeline_ms` telemetry without relying on Release-disabled `rex::perf` counters.
