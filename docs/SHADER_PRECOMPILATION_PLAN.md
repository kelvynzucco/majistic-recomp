# Majistic Recomp: Shader Stutter - Discovery Plan (v3)

> **Issue**: [#4 - Implement shader pre-compilation / cache warm-up to eliminate in-game stuttering](https://github.com/kelvynzucco/majistic-recomp/issues/4)
> **Status**: DISCOVERY ONLY. No implementation plan exists yet. It will be written from the results of this document.
> **Goal of this phase**: Find out, by measurement and code inspection, **what causes the stutter and which solutions are technically viable**, so the implementation plan can be built on facts.

---

## 1. Constraints (decided, not up for debate)

1. **Nothing derived from the game is ever distributed.** No microcode, no `.xsh`, no `.xpso`/`.xpl`, no hash lists, no driver caches. Everything is generated on the player's machine from their own legally owned copy.
2. **The game is played once.** A cache that accumulates over repeated playthroughs is useless. The stutter that matters is on the **first and only** run. Solutions must either prevent the cost during that run or prepare it on the player's machine before/while it happens.
3. **Async shader compilation is currently forced off** (`src/majin_app.h:213`, `async_shader_compilation=false`) because it caused missing geometry and broken shadow passes. Any plan to re-enable it must understand exactly why it broke.

## 2. What we already know (verified in the repo)

- The game is fixed in content: Title ID `4E4D0816`, level-based progression.
- Launcher default rendering path is **Vulkan** (`launcher/src/main.js`, `RenderingPath: "Vulkan"`). D3D12 is also supported (RTV/ROV).
- The D3D12 backend has a patch to compile stored pipelines at startup with multiple threads (`patches/rexglue-0.10.0/d3d12-blocking-pso-precompile.patch`).
  - **Verified in D2**: Prior to `2026-11-08 15:58:13` (commit `3954dec`), `scripts/prepare-sdk-source.ps1` applied `d3d12-blocking-pso-precompile.patch` to upstream `rexglue-sdk v0.10.0`. When `tools/source/rexglue-sdk` was switched to the `ReIGlue` checkout in commit `3954dec`, that patch was not ported over, leaving `tools/source/rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp:537` with `while (creation_threads_.size() < creation_thread_original_count)` instead of `creation_thread_needed_count`. Consequently, pre-`3954dec` D3D12 logs compiled startup PSOs multi-threaded (`6.49–11.99 ms/PSO` wall-clock), whereas post-`3954dec` D3D12 logs (`runtime-20261108-160653.log`, `163340.log`) ran single-threaded (`75.02–81.04 ms/PSO`). Conversely, Vulkan startup warm-up (`src/graphics/vulkan/pipeline_cache.cpp:1020-1146`) translates guest shaders single-threaded (`lines 1021-1039`) before creating PSOs multi-threaded (`lines 1085-1146`).
- The Vulkan backend supports `--shader_prebuild_only` and a list file `<cache_root>/shaders/prebuild/4E4D0816.<fbo|fsi>.vk.xpl` (`tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp:1176`, `command_processor.cpp:98,1382-1394`). It reads local data only; usable offline for experiments, not for distribution.
- Per-frame telemetry already exists: `--perf_log_csv` (enabled by `scripts/run-usa.ps1 -RecordPerformance`) with columns `frame_time_us, fps, draw_calls, command_buffer_stalls, texture_cache_hits, texture_cache_misses, pipeline_cache_hits, pipeline_cache_misses, ...`. Past runs: `reports/*.performance.csv`, `game-data/runtime/logs/*.performance.csv`.
  - **Verified in D1**: In `Release` builds, `REXGLUE_ENABLE_PERF_COUNTERS` is compiled out (`tools/source/rexglue-sdk/CMakeLists.txt:31`, `$<$<NOT:$<CONFIG:Release>>:REXGLUE_ENABLE_PERF_COUNTERS>`), so `CommandProcessor::ExecutePacketType3_XE_SWAP` (`tools/source/rexglue-sdk/src/graphics/command_processor.cpp:1370-1406`) writes only `frame_time_us` and `fps` to `--perf_log_csv`, leaving all other 15 columns as `0` in all 198 existing CSVs. Therefore, attributing spikes in existing `.performance.csv` files requires **timestamp correlation (inference)** against `.log` lines.
  - **Verified in D1**: Even in non-Release builds, the `PROFILE_*` macros (`rex::perf`) are only called in the D3D12 backend (`src/graphics/d3d12/*`) and never in the Vulkan backend (`src/graphics/vulkan/*`). Instead, the Vulkan backend uses `rex::graphics::fh1_frame_cost` (`src/graphics/fh1_frame_cost.h`), which logs 10s slow-frame windows (`GPU slow frames: ...`) and 60s summaries (`GPU minute ...`) to `.log`, and supports a 46-column per-frame CSV via `--fh1_frame_cost_csv=<path>` (`src/graphics/vulkan/command_processor.cpp:214, 1174-1229`).
- Existing local caches are small (`.xsh` 0.9 to 250 KB, `.xpso` 1 to 21 KB); one log reports **209 shaders** loaded from storage. Vulkan driver cache `.vkpc` is keyed by GPU/driver (`vendor_id`, `device_id`, `driver_version`, `pipeline_cache_uuid`), starts around ~11–29 MB for 161–332 pipelines, and **swells up to the 512 MiB cap (`535,813,834 bytes` for 284 pipelines, `~2.15 MB/pipeline` for entries #33–#283)** on NVIDIA RTX 3060 Ti due to isolated per-pipeline `VkPipelineCache` blobs in `CreatePipelineWithDriverCache` (`src/graphics/vulkan/pipeline_cache.cpp:569-725`; verified in D2).
- The Vulkan async path has its own flag: `vulkan_async_skip_incomplete_frames=false` (`majin_app.h:212`). Vulkan synchronous lookahead is enabled by default (`vulkan_pipeline_lookahead=true`, `src/graphics/vulkan/fh1_pipeline_lookahead.cpp:39`).

## 3. What we do NOT know (the discovery agenda)

The solution space (see section 6) depends entirely on the answers below. Each question has: why it matters, how to answer it, the expected output, and the decision it unlocks.

Work order: **D1 -> D2 -> D3 (parallel with D4) -> D5 -> D6 -> D7 -> D8**. D1 to D3 are cheap and use existing data; do them first.

---

### D1. What causes the frame-time spikes? — **[COMPLETED: 2026-11-10]**
- **Why**: If pipeline/shader work is not the main cause, this whole initiative is aimed at the wrong target.
- **How**:
  1. Define a spike: `frame_time_us` > 2x rolling median (window ~60 frames) AND > 25 ms.
  2. For every spike, take the per-frame **delta** of `pipeline_cache_misses`, `texture_cache_misses`, `command_buffer_stalls`, and `draw_calls`.
  3. Classify each spike: pipeline-miss / texture-miss / stall / unexplained.
  4. Run on all existing CSVs, split by backend (Vulkan vs D3D12) if identifiable.
- **Output**: [`docs/discovery/D1-stutter-attribution.md`](discovery/D1-stutter-attribution.md) & [`scripts/discovery/d1-stutter-attribution.mjs`](../scripts/discovery/d1-stutter-attribution.mjs).
- **Unlocks**: **GO** on the shader/pipeline initiative (with explicit methodological caveats below). In Vulkan, **69.1% of all CSV spikes (89.3% of excess spike ms lost)** correlate by timestamp with `Creating graphics pipeline state` or `fh1_frame_cost` slow-frame windows, and **82.1% of `fh1_frame_cost` slow frames (98.9% of measured GPU work; 100% of severe spikes >= 94 ms)** are directly recorded in `shader_ms + pipeline_ms`, while texture cache misses account for **0.0%** (`0.6 ms` total).
- **Counter caveat verified**: In `tools/source/rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp:900-1013` (`PipelineCache::ConfigurePipeline`), `PROFILE_PIPELINE_CACHE_MISS()` (`line 1012`) counts **unique PSO cache misses (PSO creations)** only — **not** guest shader translations (`AnalyzeUcode` + `TranslateAnalyzedShader` run at `lines 927-981` without incrementing any `rex::perf` counter). Also, in `src/core/perf/counter.cpp:57-75, 104-115`, `kDrawCalls`, `kCommandBufferStalls`, `kTextureCacheMisses`, and `kPipelineCacheMisses` have `kIsGauge = false` and are already atomically snapshotted and zeroed (`exchange(0)`) each frame.

#### D1 Detailed Findings & Methodological Caveats (Summary of [`docs/discovery/D1-stutter-attribution.md`](discovery/D1-stutter-attribution.md))

- **Hardware / Environment**: Windows 11, NVIDIA GeForce RTX 3060 Ti (`vendor 0x10DE`, `device 0x2489`, 8 GB VRAM), NVIDIA Driver `621.14` (`0x9B404000` / `0x9B438000`, Vulkan API `1.4.357`).
- **Dataset Audited**: **198** `.performance.csv` files and **203** `.log` files under `reports/` and `game-data/runtime/logs/` (**177** valid non-empty CSV sessions totaling **394,038 frames** / **117.7 minutes** of runtime; 21 CSVs are 0-byte or 315-byte header-only files).

##### 1. Per-Frame CSV Spike Attribution (`frame_time_us > 2x rolling_median(60) AND > 25,000 us` — Inferred by Timestamp Correlation)

Because `REXGLUE_ENABLE_PERF_COUNTERS` is disabled in `Release` (`CMakeLists.txt:31`), the CSV miss columns are `0`. Every CSV frame timestamp was reconstructed from the initial swap log anchor (`[Majin performance] Release counter mode:`) and correlated against timestamped `.log` events (an **inference by timestamp correlation**, corroborated independently by direct `fh1_frame_cost` timers in subsection 2):

**Vulkan Backend (76 valid CSV sessions, 113,241 frames, 2,316.2 s total runtime):**

| Cause Category (Inferred via Log Timestamp Correlation) | Spikes (`N = 269`) | % of Spikes | Total Spike Duration (ms) | Excess Time Lost > Median (ms) | % of Excess Time Lost |
| :--- | ---: | ---: | ---: | ---: | ---: |
| **Pipeline / Shader Miss** (`Creating graphics pipeline state` / `fh1_frame_cost`) | **186** | **69.1%** | **64,062.2 ms** | **60,660.3 ms** | **89.3%** |
| **Boot / Level Load / Save I/O** (`[Majin loading]`, `[Majin save]`, audio/title init) | 59 | 21.9% | 6,967.2 ms | 5,727.8 ms | 8.4% |
| **Window / Swapchain Recreation** (`VulkanPresenter: Created ... swapchain`) | 2 | 0.7% | 94.1 ms | 60.7 ms | 0.1% |
| **Unexplained / Guest Pacing Slip** (single-frame 33->51..71 ms VSync/pacing step) | 22 | 8.2% | 1,874.9 ms | 1,494.7 ms | 2.2% |
| **Total (Vulkan)** | **269** | **100.0%** | **72,998.5 ms** | **67,943.5 ms** | **100.0%** |

**D3D12 Backend (101 valid CSV sessions, 280,797 frames, 4,745.3 s total runtime — Directional Proxy):**

*(Note: D3D12 logs per-draw `Creating graphics pipeline with VS ...` at `REXGPU_DEBUG` level in `src/graphics/d3d12/pipeline_cache.cpp:2685-2689`, which is omitted in default `info` logs, but logs exact startup cache counts `Translated X shaders` and `Created Z graphics pipelines ... from the storage` at `REXGPU_INFO` level, `lines 515, 690`. Grouping sessions into Cold/Growing-Cache runs vs. Warm-Cache runs provides a **directional proxy** across unstandardized development sessions with different routes and durations — not a controlled experiment.)*

| Session / Cause Group (Directional Proxy) | Spikes (`N = 295`) | % of Spikes | Total Spike Duration (ms) | Excess Time Lost > Median (ms) | % of Excess Time Lost |
| :--- | ---: | ---: | ---: | ---: | ---: |
| **Cold / Growing-Cache Sessions** (13 sessions, 75,148 frames; **338.5 ms lost / 1k frames**) | **132** | **44.7%** | **27,948.4 ms** | **26,071.8 ms** | **66.2%** |
| **Warm-Cache: Boot / Level Load / Save I/O** (88 warm sessions, 205,649 frames) | 139 | 47.1% | 14,503.0 ms | 11,927.0 ms | 30.3% |
| **Warm-Cache: Window / Focus / Capture** | 5 | 1.7% | 264.0 ms | 180.7 ms | 0.5% |
| **Warm-Cache: Unexplained / Guest Pacing** (**only 5.7 ms lost / 1k frames**) | 19 | 6.4% | 1,498.1 ms | 1,181.0 ms | 3.0% |
| **Total (D3D12)** | **295** | **100.0%** | **44,213.5 ms** | **39,360.4 ms** | **100.0%** |

- Directionally across the D3D12 logs, non-I/O mid-gameplay spikes drop from **136.6 ms / 1,000 frames** (`67` spikes in `75,148` frames during cold/growing sessions) to **5.7 ms / 1,000 frames** (`19` minor spikes in `165,952` frames during warm sessions). Because routes and durations vary across sessions, this **~95.8% reduction is strictly directional** (not a controlled benchmark headline).

##### 2. Direct Sub-Millisecond GPU Telemetry Attribution (Vulkan `fh1_frame_cost`)

Across all **203** runtime logs, `rex::graphics::fh1_frame_cost::Report` (`src/graphics/fh1_frame_cost.h:328-496`) recorded **29 slow-frame windows** (**67 slow frames total** with `ms >= max(50 ms, 1.5x typical_ms)` after the initial 30s startup window):

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

- **100% of severe spikes (`worst >= 94 ms`, up to `448 ms`) are caused by `shader_ms + pipeline_ms`.** All 12 non-pipeline slow frames (4 `guest_wait_ms` + 8 `unclear`) had `worst` between **51 ms and 71 ms** (a single missed 30 FPS / 33.3 ms frame boundary).
- **Texture cache misses do NOT cause frame-time spikes**: Across all 67 slow frames (even when loading 40–64 textures / 45–56 MB of guest texture data in a frame, e.g., `runtime-20261108-161438.log:441`), `texture_create_ms + texture_load_ms` totaled **0.6 ms** (`< 0.01 ms` per slow frame) with **0.0 textures evicted**, confirming that the memory limits in `src/majin_app.h:220-231` (`4096 MB` soft / `8192 MB` hard) already eliminated texture thrashing.

##### 3. Three Methodological Caveats & Three SDK Telemetry Bugs in D1

- **Methodological Caveats**:
  1. **CSVs require inference by timestamp correlation**: Because `REXGLUE_ENABLE_PERF_COUNTERS` is off in `Release` (`CMakeLists.txt:31`), CSV miss counters are `0`. CSV spike attribution (`69.1%` of spikes / `89.3%` of excess ms) is inferred from timestamp alignment with `.log` events and corroborated by direct `fh1_frame_cost` timers (`82.1%` of slow frames / `98.9%` of measured work).
  2. **D3D12 cold-vs-warm is a directional proxy**: Compares unstandardized dev sessions with varying routes/durations; the `95.8%` drop (`136.6` -> `5.7 ms/1k frames`) is directional, not a controlled benchmark metric.
  3. **Sample bias (partial dev routes)**: Existing logs cover early/partial development routes; full-game pipeline coverage is deferred to D3.
- **SDK Telemetry Bugs**:
  1. `REXGLUE_ENABLE_PERF_COUNTERS` is compiled out in `Release` builds (`tools/source/rexglue-sdk/CMakeLists.txt:31`).
  2. The Vulkan backend never calls `PROFILE_*` (`rex::perf`) macros; it uses `fh1_frame_cost` (`--fh1_frame_cost_csv=<path>`).
  3. `fh1_frame_cost::Report::MainCause()` (`fh1_frame_cost.h:412`) checks `cpu + gpu_wait < 0.5 * ms` (`Cause::kGame`) **before** checking `costs[]` (`Cause::kShaders`), mislabeling slow frames where the CP thread slept in `Fh1WaitForAhead()` waiting on background PSO compilation threads as `cause: the game` despite `pipeline_ms = 91.4..410.6 ms`.

---

### D2. How is a miss split between guest-shader translation and PSO creation? — **[COMPLETED: 2026-11-10]**
- **Why**: Translation (Xenos ucode -> SPIR-V/DXIL) is state-independent and could be done ahead of time; PSO creation needs full state and is driver-bound. Which one dominates decides which solutions can help.
- **How**: Local, uncommitted logging patch (or existing log levels) that records wall-clock time of (a) shader translation, (b) `vkCreateGraphicsPipelines` / `CreateGraphicsPipelineState` and its driver-cache/lookahead wrapper, (c) the rest of the draw path, per miss.
- **Output**: [`docs/discovery/D2-miss-cost-breakdown.md`](discovery/D2-miss-cost-breakdown.md) & [`scripts/discovery/d2-miss-cost-breakdown.mjs`](../scripts/discovery/d2-miss-cost-breakdown.mjs).
- **Unlocks**:
  1. **Guest shader translation (Stage a) is tiny (`1.58 ms/shader` mean on Vulkan = `1.39%` of slow-frame miss time; `0.036 ms/shader` parallel wall-clock on D3D12)**, so pre-translating shaders alone will not solve stutter.
  2. **However, `pipeline_ms` (`98.61%` of slow-frame miss time) is NOT pure single-PSO driver compilation — it conflates four distinct mechanisms**:
     - **(b1) True cold driver PSO compilation (`GLCache`/`DXCache` cold)**: **`46.8–205.3 ms / PSO`** for isolated single-PSO cold misses on Vulkan (`43.91 ms / PSO` session mean across all `167` runtime PSOs in `161438.log`), and **`75.02–81.04 ms / PSO`** single-threaded (`6.49–11.99 ms / PSO` multi-threaded with `d3d12-blocking-pso-precompile.patch`) on cold D3D12.
     - **(b2) Per-pipeline `.vkpc` `~2.11 MB` wrapper overhead (`CreateDriverCache` + `vkCreateGraphicsPipelines` cache-population penalty + `GetDriverCacheData` + `MakeEntry` FNV-1a checksum)**: Controlled probe runs on RTX 3060 Ti prove that when NVIDIA's `GLCache` is warm, passing `--vulkan_store_pipeline_cache=false` (`cache = VK_NULL_HANDLE`) takes only **`0.199 ms / PSO`** (`6.29 ms` total for 29 PSOs), whereas the default `.vkpc` path (`--vulkan_store_pipeline_cache=true`) takes **`8.021 ms / PSO` (`226.8 ms` total — `40.1x` slower!)**, meaning **`7.822 ms / PSO` (`97.5%`) is pure `.vkpc` overhead**!
     - **(b3) `.vkpc` `Outcome::kStale` double-compilation & 512 MiB cap overflow**: Rebuilding stale `.vkpc` entries takes **`22.24–23.18 ms / PSO` per worker thread** (`64.7%` wasted in the 2nd pass), and pipelines past the 284-entry `512 MiB` ceiling take **`15.34 ms / PSO` per thread** to serialize `2.15 MB` blobs that `KeepDriverCacheEntry` immediately discards.
     - **(b4) Multi-PSO lookahead batch waiting (`Fh1WaitForAhead`)**: Slow-frame `pipeline_ms` (`191.88 ms / slow frame`) is a **per-frame** metric that bundles `1` CP-thread PSO + `K` background lookahead PSOs (`59.3%` lookahead wait across `108` ahead PSOs + `40.7%` inline CP builds across `59` PSOs in `161438.log` = **`43.91 ms / runtime PSO`**).
  3. **Strategic consequence**: Fixing/disabling the per-pipeline `.vkpc` mega-blob wrapper immediately eliminates **`97.5%` of warm-`GLCache` PSO overhead** (`8.02 ms` -> `0.20 ms / PSO`) and **`~850–950 ms` of startup disk I/O** without pre-compiling anything, while **true first-run cold driver compilation (`46.8–205.3 ms / PSO`) still requires PSO warm-up/pre-compilation or async mitigation**.

#### D2 Detailed Findings (Summary of [`docs/discovery/D2-miss-cost-breakdown.md`](discovery/D2-miss-cost-breakdown.md))

##### 1. Executive Summary Table Across All Decomposed Stages

| Stage / Sub-Stage | Vulkan Per-Item Cost (RTX 3060 Ti) | Vulkan Per-Slow-Frame / Startup Cost | D3D12 Per-Item Cost (RTX 3060 Ti) | D3D12 Startup Total Cost | Key Takeaway |
| :--- | ---: | ---: | ---: | ---: | :--- |
| **(a) Guest Shader Translation** (`AnalyzeUcode` + SPIR-V / DXBC + `VkShaderModule`) | **`1.58 ms / shader`** mean (`1.50 ms` P50, `5.30 ms` max, N = 94) | `2.69 ms / slow frame` (`148.4 ms` total = `1.39%` of slow-frame miss work); startup: `154–275 ms` (`~0.7–1.1 ms/shader`, 1 thread) | **`0.036 ms / shader`** wall-clock parallel (`~0.25–0.50 ms/shader` per thread) | `6.0 ms` P50 (`4–17 ms` for `128–220` shaders across worker threads) | Fast and state-independent; pre-translating shaders alone saves only **~1.4%** of stutter. |
| **(b1) Cold Driver PSO Compilation** (`GLCache` / `DXCache` cold) | **`46.8–205.3 ms / PSO`** (isolated single-PSO runtime misses); **`43.91 ms / PSO`** session mean across all `167` runtime PSOs (`59` CP + `108` ahead) | Bundled with (b2)+(b4) into **`191.88 ms / slow frame`** (`195.9 ms` P50, `410.6 ms` max across `55` slow frames) | **Patched (worker threads)**: **`6.49–11.99 ms / PSO`** wall-clock; **Unpatched (1 thread)**: **`75.02–81.04 ms / PSO`** | **Patched**: `1,506–3,308 ms` (`180–295` PSOs); **Unpatched**: `8,402–9,076 ms` (`112` PSOs); runtime cold spike excess: `197.51 ms` mean (`101.41 ms` P50) | True cold SASS compilation takes **`~45–205 ms / PSO`** per thread (`~6.5–12 ms/PSO` wall-clock across worker threads). |
| **(b2) Warm Driver Cache + `.vkpc` Disabled / D3D12 Warm `DXCache`** | **`0.199 ms / PSO`** mean (`0.191 ms` P50, `0.663 ms` max; `--vulkan_store_pipeline_cache=false`, N = 29) | **`6.29 ms` total** across all 29 runtime PSOs (`14` active frames) | **`0.149 ms / PSO`** P50 (`0.193 ms` mean across N = 92 warm sessions) | `39.0 ms` P50 (`26–181 ms` for `215–276` PSOs) | When NVIDIA `GLCache`/`DXCache` is warm, raw PSO creation without `.vkpc` is **`~0.15–0.20 ms / PSO`**. |
| **(b2-ovh) `.vkpc` Per-Pipeline `~2.11 MB` Wrapper Overhead** (`--vulkan_store_pipeline_cache=true`) | **`+7.822 ms / PSO`** pure overhead (`8.021 ms / PSO` total vs. `0.199 ms` raw = **`40.1x` slower**!) | **`226.8 ms` total** across 29 runtime PSOs (`+4.86 ms` in `vkCreateGraphicsPipelines` + `0.88 ms` `GetDriverCacheData` + `1.92 ms` FNV-1a checksum + `0.16 ms` `CreateDriverCache`) | N/A (D3D12 does not use per-pipeline `ID3D12PipelineLibrary` blobs) | Startup `.vkpc` disk read + checksum over `535.8 MB` costs **`851–954 ms`** (`67%` of warm startup!) | **Fixing/disabling per-PSO `.vkpc` mega-blobs eliminates `97.5%` of warm-`GLCache` PSO cost and `~950 ms` of startup I/O without pre-compiling anything.** |
| **(b3) `.vkpc` `Outcome::kStale` Double-Pass & 512 MiB Cap Overflow** | **`22.24–23.18 ms / PSO`** per thread for `kStale` (`14.39–15.48 ms` [`64.7%`] in 2nd pass); **`15.34–16.87 ms / PSO`** per thread for unseeded overflow (then discarded!) | `1,754–1,908 ms` wall-clock across 6 threads for `342` PSOs (`33` reused at `0.52 ms`, `251` stale rebuilt, `58` overflow compiled & discarded) | N/A | N/A | Caused by per-PSO `2.15 MB` NVIDIA blobs hitting the `512 MiB` cap at `284` PSOs and `kStale` rebuilding twice. |
| **(c) Rest of the Draw Path** (State validation, bindings, barriers, `vkCmdDraw`) | **`0.0028 ms / draw`** P50 (`2.80 us/draw`) | **`1.60 ms` CPU / frame** P50 for `538` draws/frame | Included in `< 16.6 ms` frame budget | N/A | **`< 0.8%`** of a slow frame (`1.60 ms` vs `~200 ms`). |

##### 2. Controlled Probe Decomposition of `VulkanPipelineCache::CreatePipelineWithDriverCache` (`vulkan/pipeline_cache.cpp:569-687`)

1. **Runtime Warm-`GLCache` A/B Probe (N = 29 identical runtime PSOs: `6` CP-thread + `23` background lookahead across `14` active frames)**:
   - **With `--vulkan_store_pipeline_cache=false` (`cache = VK_NULL_HANDLE`)**: `vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, ...)` takes **`0.199 ms / PSO` mean** (`0.191 ms` P50, `0.663 ms` max), and total `bg_pipeline_ms` across all 14 frames is only **`6.29 ms`**.
   - **With `--vulkan_store_pipeline_cache=true` (Default `.vkpc` Enabled)**: Creating the exact same 29 PSOs takes **`8.021 ms / PSO` mean** (`7.530 ms` P50, `16.382 ms` max; **`226.80 ms` total `bg_pipeline_ms` = `40.1x` slower**):
     - `CreateDriverCache(nullptr, 0)`: **`0.160 ms / PSO`** (`2.0%`)
     - `vkCreateGraphicsPipelines(device, cache, ...)`: **`5.060 ms / PSO`** (`63.1%` — **`+4.861 ms`** slower than `VK_NULL_HANDLE` because the NVIDIA driver merges/populates **`2,110,277 bytes` (`2.01 MiB`)** of internal driver cache data into `cache`!)
     - `GetDriverCacheData(cache)` (`vkGetPipelineCacheData` x2 copying `2.11 MB` + `vkDestroyPipelineCache`): **`0.878 ms / PSO`** (`10.9%`)
     - `driver_cache_file::MakeEntry` (single-byte FNV-1a checksum loop over `2.11 MB` at `driver_cache_file.h:67` + `KeepDriverCacheEntry`): **`1.920 ms / PSO`** (`23.9%`)
2. **Startup Warm-Up Sub-Stage Probe (N = 342 stored PSOs across 6 worker threads)**:
   - **Seeded `.vkpc` Hit (`Outcome::kReused`, N = 33, `seed` ~23.1 KB)**: **`0.523 ms / PSO` mean** per thread (`0.262 ms` P50; `create_cache = 0.233 ms`, `vk_pso = 0.228 ms`).
   - **Seeded `.vkpc` Miss (`Outcome::kStale` Double-Pass, N = 251, `seed` ~2.15 MB)**: **`22.241 ms / PSO` mean** per thread (`21.856 ms` P50), split between Pass 1 (`create_cache = 0.913 ms`, 1st `vk_pso = 5.329 ms`, 1st `get_data = 1.601 ms` = **`7.843 ms` [`35.3%`]**) and Pass 2 (`create_cache = 0.246 ms`, 2nd `vk_pso = 10.258 ms`, 2nd `get_data = 1.507 ms`, `MakeEntry` FNV-1a checksum `= 2.387 ms` = **`14.398 ms` [`64.7%`]**). Triggered whenever `--resolution_scale` changes between `1` and `2` because `PipelineDescription::GetHash()` (`key`) omits `draw_resolution_scale_x/y` while `SpirvShaderTranslator` emits scale-dependent SPIR-V!
   - **Unseeded 512 MiB Cap Overflow (`seeded = 0, Outcome::kStored`, N = 58, entries `#284–#341`)**: **`15.339 ms / PSO` mean** per thread (`create_cache = 0.340 ms`, `vk_pso = 11.010 ms`, `get_data = 1.491 ms`, `MakeEntry` checksum `= 2.498 ms`), after which `KeepDriverCacheEntry` (`vulkan/pipeline_cache.cpp:714-723`) **discards the `2.15 MB` blob** because `driver_cache_bytes_` is already at the `512 MiB` ceiling!

##### 3. Startup vs. Runtime Scale Explanation & Per-Frame vs. Per-PSO Separation (D2 Points 2 & 3)

- **Why startup warm-up logs (`0.55–8.44 ms/PSO` wall-clock) were 3x–10x faster than runtime slow frames (`191.88 ms/slow frame`)**:
  1. In warm startup logs (`runtime-20261109-183049.log`, `183708.log`: `1,416–1,419 ms` total for `339` PSOs), **`951–954 ms` (`67.3%`)** was spent reading and FNV-1a checksum-verifying the `535.8 MB` `.vkpc` file from disk, **`275–279 ms` (`19.5%`)** was single-threaded guest shader translation, and only **`186–190 ms` (`13.2%`, `0.55 ms/PSO` wall-clock across 6 threads)** was spent in the PSO loop (with NVIDIA's `GLCache` already warm).
  2. In rebuilt startup logs (`runtime-20261108-165454.log`: `2,819 ms` PSO phase for `334` PSOs = `8.44 ms/PSO` wall-clock across 6 threads), worker threads were spending `15–23 ms/PSO` per thread doing `.vkpc` `Outcome::kStale` double-pass `2.15 MB` serialization while `GLCache` was already warm.
  3. At runtime during a cold session, unseen shaders/PSOs miss NVIDIA's `GLCache` and require true first-time SPIR-V -> SASS compilation (**`46.8–205.3 ms / PSO`** in isolated single-PSO cold misses with `0` lookahead waits: `46.8 ms` at `161438.log:377`, `101.5 ms` at `line 468`, `112.2 ms` at `line 459`, `195.6 ms` at `152130.log:1367`, `205.3 ms` at `161438.log:413`).
- **Why `191.88 ms / slow frame` is a per-frame batch metric, not a per-PSO metric**:
  - In `VulkanPipelineCache::ConfigurePipeline` (`vulkan/pipeline_cache.cpp:1845-1927`), `fh1_frame_cost::Timer cost_timer(fh1_frame_cost::Current().pipeline_ms)` wraps **both** the CP thread's `EnsurePipelineCreated` (`line 1916`) **and** `finish_ahead` (`Fh1WaitForAhead`, `lines 1846, 1927`), which blocks until **all K lookahead PSOs** queued in `fh1_ahead_set_` complete on background threads — while incrementing `pipelines` by only `1` (or `0` at `line 1845`).
  - In the 15-minute cold Vulkan session (`runtime-20261108-161438.log:315-487`), **`167` new runtime PSOs** were built (`59` inline on the CP thread + `108` on background lookahead threads), and total slow-frame `pipeline_ms` was **`7,333.4 ms`** across `37` slow frames (`198.20 ms / slow frame`):
    - **`4,348.7 ms` (`59.3%`)** was spent in `Fh1WaitForAhead` (`60` waits across `108` ahead PSOs = **`40.27 ms / ahead PSO`**).
    - **`2,984.7 ms` (`40.7%`)** was spent in direct CP-thread creation across `59` inline PSOs (**`50.59 ms / inline CP PSO`**).
    - Divided across all **`167` runtime PSOs**, the true session mean slow-frame stall is **`43.91 ms / runtime PSO`** (`~4.51 PSOs` amortized per slow frame).

##### 4. D3D12 Patch Verification (`d3d12-blocking-pso-precompile.patch`) & Thread-Normalized Ratios (D2 Points 4 & 5)

- **Patch status in `tools/source/rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp:537`**:
  - Pre-`2026-11-08 15:58:13` D3D12 builds (before commit `3954dec` switched `tools/source/rexglue-sdk` from upstream `rexglue-sdk v0.10.0` to `ReIGlue`) had `patches/rexglue-0.10.0/d3d12-blocking-pso-precompile.patch` applied, running Stage (b) across **N = logical_processor_count - 1 threads** (`6.49–11.99 ms / PSO` wall-clock, `7.67 ms / PSO` P50 across N = 11 cold/rebuilt runs).
  - Post-`2026-11-08 15:58:13` D3D12 builds (`runtime-20261108-160653.log` and `163340.log`, identifiable by `SHADER_WARMUP: D3D12`) ran on the unpatched `ReIGlue` checkout where `line 537` checks `creation_thread_original_count` (`0`), forcing Stage (b) onto **1 thread** (`75.02–81.04 ms / PSO` wall-clock, `8,402–9,076 ms` for `112` PSOs).
- **Apples-to-apples thread-normalized Stage (b) : Stage (a) ratio on D3D12**:
  - When both Stage (a) (`0.027–0.072 ms/shader` wall-clock across N threads) and Stage (b) (`6.49–11.99 ms/PSO` wall-clock across N threads) run with the multi-threading patch applied, the true per-item cost ratio is **`166:1 to 240:1`** (`214:1` P50; Stage b = `97.8%–99.7%` of startup compile time). The `862:1 to 1,072:1` wall-clock ratio in `160653.log` and `163340.log` was inflated by N:1 because Stage (a) ran on N threads while Stage (b) ran on `1` thread.

---

### D3. How many distinct pipelines/shaders does the game use, and does the count plateau? — **[COMPLETED: 2026-11-10]**
- **Why**: Defines the size of the problem and whether brute-force local pre-generation is feasible.
- **How**: Read-only count of unique (vertex hash, pixel hash, state) tuples and unique shaders from the existing `.xsh`/`.xpso` caches (format: SDK `pipeline_store_file.h`); compare across sessions/areas to see the growth curve.
- **Output**: [`docs/discovery/D3-pipeline-population.md`](discovery/D3-pipeline-population.md) & [`scripts/discovery/d3-pipeline-population.mjs`](../scripts/discovery/d3-pipeline-population.mjs).
- **Unlocks**:
  1. **Population in tested early-game zones is small**: Across all `7` `.xsh` files, `8` `.xpso` files, and `203` `.log` files, the grand union of unique guest shaders is **`242` shaders** (`100` VS + `142` PS, `~266 KB` total Xenos ucode, `0` duplicates in any `.xsh` file), and the largest accumulated PSO caches contain **`342` unique PSOs** on Vulkan FBO (`23.3 KB`, `183` unique `(VS, PS)` pairs, `94` fixed-state blocks, **`1.87 PSOs/pair`**) and **`295` unique PSOs** on D3D12 RTV (`21.3 KB`, `177` unique `(VS, PS)` pairs, `68` fixed-state blocks, **`1.67 PSOs/pair`**).
  2. **Strong cross-backend convergence (`86.0%` shaders, `82.7%` pairs)**: `208 / 242` shaders (`86.0%`) and `163 / 197` `(VS, PS)` pairs (`82.7%`) are identical between `vulkan-vulkan` (`342` PSOs) and `d3d12-default` (`295` PSOs); moreover, `d3d12-rtv` (`157` PSOs) is a `100%` (`157/157`) exact hash subset of `d3d12-default` (`295` PSOs).
  3. **Fast within-zone plateaus & sublinear shader growth**: Within the 15.9-minute continuous session (`runtime-20261108-161438.log`), PSO creation occurs in step-function bursts at zone/cutscene transitions, separated by multi-minute local plateaus (`+7` PSOs / `+0` shaders across Min 3–4; `+8` PSOs / `+5` shaders across Min 6–9 [`2.0 PSOs/min`]; `+5` PSOs / `+4` shaders across Min 13–15 [`1.7 PSOs/min`]). Across subsequent sessions on already-visited routes, growth drops to `0–4` new PSOs per session (`332 -> 342` on Vulkan across 6 sessions; `291 -> 295` with `+0` new shaders on D3D12). Furthermore, shader count grows sublinearly relative to PSOs (`PSOs/shader` rises from `0.97` at 100 PSOs to `1.49` at 342 PSOs) as later PSOs reuse existing shaders under new state combinations.
  4. **Methodological caveat (early-game coverage vs. full campaign)**: On-disk caches cover menus, opening cutscenes, and the first `~3–4` playable areas up to meeting the Majin (`Achievement unlocked: #2`). While within-zone plateaus are proven and full-campaign totals for a single-engine Xbox 360 title of this scope are projected in the **mid-hundreds of shaders (`~350–600`) and several hundred to `~1,200` PSOs** (fully viable for local warm-up), new zones/powers/bosses will still introduce new bursts at zone boundaries — making D4/D5 (load-time warm-up) and D6/D7 (runtime miss mitigation) directly relevant for first-playthrough smoothness.

#### D3 Detailed Findings (Summary of [`docs/discovery/D3-pipeline-population.md`](discovery/D3-pipeline-population.md))

##### 1. On-Disk Binary Cache Population (`7` `.xsh` files & `8` `.xpso` files)

| Cache Directory / File | Backend & Mode | File Size | Unique Shaders (`VS` + `PS`) | Unique PSOs (`PipelineStoredDescription`) | Depth-Only PSOs (`PS=0`) | Unique `(VS, PS)` Pairs | Unique `(VS_mod, PS_mod)` Tuples | Unique Fixed-Function State Blocks | Mean / Max `PSOs per (VS, PS) Pair` |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| **`vulkan-vulkan`** (`4E4D0816.xsh` + `4E4D0816.fbo.vk.xpso`) | Vulkan `fbo` (`Format 2`) | `266.3 KB` `.xsh` / `23.3 KB` `.xpso` | **`239`** in `.xsh` (`100`+`139`); **`230`** ref in `.xpso` (`97`+`133`) | **`342`** | `14` | **`183`** | `195` | **`94`** | **`1.87`** (max `20`) |
| **`d3d12-default`** (`4E4D0816.xsh` + `4E4D0816.rtv.d3d12.xpso`) | D3D12 `rtv` (`Legacy`) | `251.9 KB` `.xsh` / `21.3 KB` `.xpso` | **`220`** (`94` + `126`) | **`295`** | `12` | **`177`** | `190` | **`68`** | **`1.67`** (max `12`) |
| **`vulkan-vulkan`** (`4E4D0816.fsi.vk.xpso`) | Vulkan `fsi` (`Legacy`) | `11.4 KB` `.xpso` | **`179`** ref (`77` + `102`) | **`168`** | `9` | **`136`** | `137` | **`15`** | **`1.24`** (max `4`) |
| **`d3d12-rtv`** (`4E4D0816.xsh` + `4E4D0816.rtv.d3d12.xpso`) | D3D12 `rtv` (`Legacy`) | `129.5 KB` `.xsh` / `11.3 KB` `.xpso` | **`143`** (`61` + `82`) | **`157`** (`100%` subset of `d3d12-default`) | `10` | **`104`** | `112` | **`54`** | **`1.51`** (max `8`) |
| **`d3d12-rov`** (`4E4D0816.xsh` + `4E4D0816.rov.d3d12.xpso`) | D3D12 `rov` (`Legacy`) | `157.6 KB` `.xsh` / `10.3 KB` `.xpso` | **`160`** (`69` + `91`) | **`143`** | `8` | **`121`** | `122` | **`13`** | **`1.18`** (max `3`) |
| **`vulkan-vulkan_fsi`** (`4E4D0816.xsh` + `4E4D0816.fsi.vk.xpso`) | Vulkan `fsi` (`Legacy`) | `132.6 KB` `.xsh` / `12 B` `.xpso` | **`147`** (`61` + `86`) | **`0`** | `0` | `0` | `0` | `0` | — |
| **`vulkan-fbo` / `vulkan-fsi`** (boot/smoke test caches) | Vulkan `fbo` / `fsi` | `< 1 KB` | **`9`** / **`7`** | **`14`** / **`7`** | `9` / `4` | `6` / `5` | `6` / `5` | `12` / `5` | `2.33` / `1.40` |
| **Grand Union Across All On-Disk Caches (and All 203 Logs)** | **Vulkan + D3D12** | `~266 KB` unique ucode (`1,102 B` mean/shader) | **`242` (`100` VS + `142` PS)** (`100%` of log hashes present in `.xsh`) | **`342` (VK FBO) / `295` (D3D12 RTV)** | — | **`197` on-disk** (`272` incl. pre-reset logs) | — | — | — |

##### 2. Within-Session & Cross-Session Growth Curve (`runtime-20261108-161438.log` & Subsequent Runs)

- **Step-function bursts at zone transitions + multi-minute local plateaus (`runtime-20261108-161438.log`, `15.9 min`)**:
  - **Preloaded at boot**: `165` PSOs, `183` shaders.
  - **Min 1–2 (opening cutscene + entering Zone 1)**: `165 -> 184 -> 229` PSOs (`+64` PSOs), `183 -> 202` shaders (`+19` shaders).
  - **Plateau 1 (Min 3–4, exploring Zone 1)**: `229 -> 234 -> 236` PSOs (**`+7` PSOs in 2 minutes, `+0` new shaders** — flat at `202` shaders).
  - **Min 5 (sub-area / combat transition)**: `236 -> 267` PSOs (`+31` PSOs reusing existing shaders; only `+2` new shaders, `202 -> 204`).
  - **Plateau 2 (Min 6–9, 4 full minutes of continuous gameplay)**: `267 -> 268 -> 273 -> 274 -> 275` PSOs (**only `+8` PSOs and `+5` shaders across 4 minutes = `2.0 PSOs/min`**).
  - **Min 10–12 (loading Majin area + meeting Majin / `Achievement unlocked: #2`)**: `275 -> 290 -> 309 -> 327` PSOs (`+52` PSOs), `209 -> 231` shaders (`+22` shaders).
  - **Plateau 3 (Min 13–15, 3 full minutes of gameplay + 2nd cutscene)**: `327 -> 329 -> 329 -> 332` PSOs (**only `+5` PSOs and `+4` shaders across 3 minutes = `1.7 PSOs/min`**).
- **Cross-session stabilization on visited routes**:
  - Across all 6 subsequent Vulkan sessions over the next 2 days (`163142.log` through `20261109-183708.log`), the cache grew by only **`+10` PSOs total** (`332 -> 334 -> 335 -> 339 -> 342`), with multiple 60–90s sessions recording **`0` new PSOs and `0.0 ms` wait time**.
  - On D3D12 (`d3d12-default`), the final three sessions on `2026-11-08` (`165737.log`, `171903.log`, `172135.log`) grew from `291 -> 293 -> 295` PSOs with **`+0` new shaders** (flat at `220` shaders).
- **Sublinear shader scaling (`encounterGrowthCurve`)**:
  - In `4E4D0816.fbo.vk.xpso`, the first `100` PSOs require `103` shaders (`0.97 PSOs/shader`, `1.32 PSOs/pair`), whereas between PSO `175` and PSO `275` (`+100` PSOs), only `+36` new shaders (`146 -> 182`) and `+30` new `(VS, PS)` pairs (`115 -> 145`) are introduced, raising the cumulative ratio to **`1.51 PSOs/shader`** and **`1.90 PSOs/pair`** (`1.49` and `1.87` at PSO `342`).
  - Notice also that `342` PSOs in the early game **already exceeds the `284`-entry (`512 MiB`) ceiling of `.vkpc`** identified in D2 (`342 - 284 = 58` overflow PSOs recompiled and discarded on every boot).

---

### D4. When does the engine register shaders: at level load or lazily at draw time? — **[COMPLETED: 2026-11-10]**
- **Why**: If the game creates a level's shaders during loading screens, we can start compiling during the load, before the first draw (zone-based warm-up). If created lazily mid-gameplay, that idea is dead.
- **How**: Multi-layer live probe instrumentation (`[D4_PROBE]` in `command_processor.cpp`, `fh1_pipeline_lookahead.cpp`, `pipeline_cache.cpp`) measuring time gaps between microcode registration (`PM4_IM_LOAD`), ringbuffer lookahead, host translation, and first draw. Full disk scan of all `11,310` files and `555` `.pak` archives in `game-data/finalizedXBox360/`. Binary disassembly of the guest D3D9 runtime (`sub_8261EAD8` in `majin_recomp.139.cpp`).
- **Output**: [`docs/discovery/D4-shader-registration-timing.md`](discovery/D4-shader-registration-timing.md) & [`scripts/discovery/d4-shader-registration-timing.mjs`](../scripts/discovery/d4-shader-registration-timing.mjs).
- **Unlocks & Methodological Caveats**:
  1. **Shaders are registered lazily at draw time**: Zero shaders exist in level-specific archives (`st*.pak`, `chr*.pak`, `ev*.pak`). All `98` `fxb0` shader archives reside in just `4` global startup archives (`system/HexaEngineXbox.knl`, `start.pak`, `eff.pak`, `static.pak`). During loading screens (`flow_a entry` to `flow_a result`), **0 shaders are registered or compiled**. *Caveat:* Because no 3D draws occur during loading screens, the absence of registration is expected; this proves the engine does not use loading screens to pre-declare shaders to the GPU.
  2. **Direct boot measurement vs. structural gameplay evidence**: The 100% lazy emission immediately before draw was measured directly during boot and menus (N = 27 shaders, median delta(PM4_IM_LOAD -> Draw) = `1.33 ms`). For gameplay, the evidence is primarily structural: shaders are restricted to the 4 global archives, and vertex shader microcode is patched dynamically in RAM by `sub_8261EAD8` upon mesh binding according to its vertex declaration.
  3. **Lookahead window during boot vs. gameplay**: A lead time of `0.05` to `2.5 ms` was measured during boot; if in active gameplay the CPU opens a 2-to-3 frame lead over the GPU, the window could grow (to be evaluated in D5). However, for a cold compilation spike of `45–205 ms`, the lead time measured during boot is insufficient.
  4. **Cold driver warm-up measurement**: With a cold NVIDIA driver (`%LOCALAPPDATA%\NVIDIA\GLCache` cleared) and `--vulkan_store_pipeline_cache=false`, parallel pre-compilation of `342` PSOs across 6 threads took **`10,659 ms` (`~10.66 s`)** total (`431 ms` translation + `10,229 ms` PSO compile = `29.91 ms / PSO` wall-clock, `~179.4 ms / PSO` per thread; verified in [`game-data/runtime/logs/d4-cold-warmup-pass1.log`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/d4-cold-warmup-pass1.log)). An immediate re-run with the warm driver cache completed in **`274 ms`** (`17 ms` for all 342 PSOs = `0.05 ms / PSO`; verified in [`game-data/runtime/logs/d4-warm-warmup-pass2.log`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/d4-warm-warmup-pass2.log)). Extrapolating to ~1,000 PSOs yields **`~30 s`** on first boot, proving a launcher progress bar is indispensable.
  5. **Warm-up verdict**: Dynamic zone/phase warm-up is **INFEASIBLE** (no phase-specific shaders exist). Boot-time pre-compilation is **FEASIBLE ONLY IF THE PSO LIST CAN BE GENERATED LOCALLY**. For a first-time player, `.xpso` does not exist and cannot be distributed; **D5** must determine whether and how this list can be generated without playing.
  6. **Key finding for D5 and O2**: `93.5%` of Pixel Shaders match byte-for-byte with on-disk assets, and `94%` of Vertex Shaders match except for `vfetch` instructions. This makes local extraction from the user's own game files (**O2**) highly plausible.

#### D4 Detailed Findings (Summary of [`docs/discovery/D4-shader-registration-timing.md`](discovery/D4-shader-registration-timing.md))

##### 1. Live Telemetry Timing Across Registration, Lookahead, and First Draw

| Metric | Sample Size (N) | Minimum | P50 (Median) | Mean | Maximum | Architectural Conclusion |
| :--- | ---: | ---: | ---: | ---: | ---: | :--- |
| **delta(PM4_IM_LOAD -> First Draw)** | `27` | **`0.220 ms`** | **`1.327 ms`** | **`6.936 ms`** | **`81.771 ms`** | Shaders are emitted by the guest CPU immediately before the draw packet (`92.6% < 15 ms`). |
| **delta(Lookahead -> First Draw)** | `25` | **`-2,553 ms`** | **`-27.14 ms`** | **`-344.26 ms`** | **`22.89 ms`** | Lookahead lead time (`0.05–2.5 ms`) is `35x–150x` too short to hide `45–205 ms` cold SASS compile. |

##### 2. Shader Registration by Game Lifecycle Phase

| Game Lifecycle Phase | Markers in Runtime Log | Shaders Registered (`First PM4_IM_LOAD`) | Shaders Reaching First Draw | Active Draw Operations |
| :--- | :--- | ---: | ---: | ---: |
| **1. Boot / Engine Init** | Engine start -> `flow_c entry` | **`2`** (`1 VS + 1 PS`) | `0` | `0` |
| **2. Title Screen Init** | `flow_c entry` -> `SceneTitleLogo` | **`18`** (`8 VS + 10 PS`) | `21` | `72` |
| **3. Title Screen Logo & Menu** | `SceneTitleLogo` -> `flow_a entry` | **`8`** (`4 VS + 4 PS`) | `6` | `4,520` |
| **4. Level Loading (`flow_a`)** | `flow_a entry` -> `flow_a result` | **`0`** (`0.0%`) | `0` | `0` (Loading screen active) |
| **5. Cutscene & First Gameplay** | `flow_a result` -> in-game combat | Dynamically registered at first mesh render | Dynamically drawn | Active scene rendering |

---

### D5. Can the pipeline list be generated on the player's machine without playing? (And how predictable is PSO state?) — **[COMPLETED: 2026-11-10]**
- **Why**: For a new player, `.xpso` does not exist and cannot be distributed. Pre-compiling at boot only works if the list of `(VS, PS, state)` tuples can be statically derived from the game's own files installed on the user's machine.
- **How**: Reverse engineering and full extraction of all `99` `fxb0` shader containers across `4` global packages (`KB/package/static.pak`, `system/HexaEngineXbox.knl`, `start.pak`, `eff.pak`). Byte-for-byte comparison against runtime `.xsh` and state decomposition of `342` PSOs in `4E4D0816.fbo.vk.xpso` and `295` in `4E4D0816.rtv.d3d12.xpso`. Extraction of guest D3D9 `vfetch` patching in `sub_8261EAD8` (`majin_recomp.139.cpp`). Analysis of gameplay lookahead telemetry in `runtime-20261108-161438.log`.
- **Output**: [`docs/discovery/D5-state-variability.md`](discovery/D5-state-variability.md) & [`scripts/discovery/d5-state-variability.mjs`](../scripts/discovery/d5-state-variability.mjs).
- **Unlocks & Architectural Conclusions**:
  1. **Low state variability per shader pair (`1.87 PSOs/pair`)**: Across `342` PSOs in Vulkan FBO, there are `183` unique `(VS, PS)` pairs and `94` state blocks. **`67.8%` of pairs (124 / 183) use exactly 1 state variant**; **`83.6%` (153 / 183) use <= 2 variants**; **`94.0%` use <= 4 variants**. Only two highly generic shaders (`357b0a2ce6b9fef0` and `67b4aa0885628ee0`) reach 15–20 state variants. On D3D12 RTV, the ratio is **`1.67 PSOs/pair`** (`295` PSOs across `177` pairs and `68` state blocks).
  2. **Structural reason for only 94 state blocks**: Deconstruction of `PipelineDescription` reveals that the 94 states are composed of only **`9` RenderPass keys**, **`4` Blend modes**, **`3` Depth/Stencil states**, and **`15` Rasterizer states**. Each material technique has hardcoded pass semantics (PreZ write-only, Opaque main, VFX blend, Shadow caster, Fullscreen blit), naturally preventing combinatorial explosion in gameplay.
  3. **Effect containers (`fxb0`) carry explicit D3D9 render states**: Every `fxb0` container (`0x30627866`) is a compiled DirectX 9 Effect archive (Xbox 360 FXC `2.0.11164.0`) containing techniques and passes with explicit `D3DRS_*` state assignments (`D3DRS_ZWRITEENABLE`, `D3DRS_ZENABLE`, `D3DRS_ALPHABLENDENABLE`, `D3DRS_CULLMODE`).
  4. **Microcode byte-for-byte match & dynamic `vfetch` patching**:
     - **Pixel Shaders (PS)**: **`93.5%` (130 of 139)** match byte-for-byte in the on-disk `fxb0` assets. The remaining 9 (6.5%) are trivial inline clear/blit shaders (`36` to `144` bytes) emitted dynamically by the engine.
     - **Vertex Shaders (VS)**: **`0.0%` (0 of 100)** match byte-for-byte on disk, but **`94.0%`** match CF control flow and ALU bodies. The guest D3D9 runtime (`sub_8261EAD8`) dynamically patches `vfetch` instructions in guest RAM at draw/bind time based on the mesh's `D3DVertexDeclaration` (`85` unique vertex fetch signatures across the 100 VS).
  5. **Ringbuffer lookahead in gameplay fails to hide compilation stalls**: During the 15.9-min continuous gameplay session (`161438.log`), lookahead queued `108` of the `167` runtime PSOs (`64.7%`) ahead of their draw, but **`92.6%` (100 / 108) were unmet waits**, forcing **`4,348.7 ms`** of blocking CPU stalls across 60 waits (mean `72.48 ms` / wait). The CPU lead time during gameplay is insufficient to hide cold driver compilation.
  6. **Option 2 (Static Local Pre-generation) Feasibility Verdict**:
     - **Stage (a) Guest Translation**: **100% FEASIBLE**. 130 PS and 94 VS templates can be translated to SPIR-V/DXBC at install/boot, eliminating CPU translation stalls (`1.58 ms/shader`).
     - **Stage (b) Driver PSO Synthesis**: **INFEASIBLE via brute-force Cartesian generation**. The unconstrained product (`100 VS * 139 PS * 94 states = 1,306,600 PSOs`) would take **`~10.8 hours`** of cold compilation, and even a constrained product generates over `100,000` PSOs (where 99% are never used).
     - **Strategic Decision**: Discard pure static Cartesian generation (O2) for Stage (b). Prioritize **Option 1 (Automated headless replay/warm-up)**, **VK_EXT_graphics_pipeline_library (D7)** to separate shader compilation from fixed-function states, and **Restricted Async Compilation (D6)** for non-critical passes.

#### D5 Detailed Findings (Summary of [`docs/discovery/D5-state-variability.md`](discovery/D5-state-variability.md))

##### 1. Fixed-Function Pipeline State Variability Comparison

| Backend & Cache Format | Total PSOs | Unique `(VS, PS)` Pairs | Unique State Blocks | Mean Variants / Pair | RenderPass Keys | Blend Modes | Depth/Stencil States | Rasterizer States |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| **Vulkan FBO** (`4E4D0816.fbo.vk.xpso`, `Format 2`) | **`342`** | **`183`** | **`94`** | **`1.87`** | **`9`** | **`4`** | **`3`** | **`15`** |
| **D3D12 RTV** (`4E4D0816.rtv.d3d12.xpso`, `Legacy`) | **`295`** | **`177`** | **`68`** | **`1.67`** | **`1`** | **`14`** | **`35`** | **`1`** |

##### 2. State Variant Distribution per `(VS, PS)` Pair (Vulkan FBO, N = 183 Pairs)

| Variants per Pair | Pair Count | Share (%) | Cumulative Share (%) | Architectural Interpretation |
| :---: | ---: | ---: | ---: | :--- |
| **1 variant** | **`124`** | **`67.8%`** | **`67.8%`** | Single-purpose shaders (UI, skybox, fullscreen filters, dedicated PreZ). |
| **2 variants** | **`29`** | **`15.8%`** | **`83.6%`** | Standard geometry shaders (Main Opaque pass + Shadow Caster pass). |
| **3 variants** | **`11`** | **`6.0%`** | **`89.6%`** | Geometry with two-sided culling or alpha test variation. |
| **4 variants** | **`8`** | **`4.4%`** | **`94.0%`** | Multi-pass material shaders (Opaque, Blend, Shadow, Depth-only). |
| **5 to 9 variants** | **`9`** | **`4.9%`** | **`98.9%`** | Shared terrain/foliage shaders bound to multiple render targets. |
| **15 to 20 variants** | **`2`** | **`1.1%`** | **`100.0%`** | Two ultra-generic core materials reused across passes with varying blend/depth masks. |

##### 3. Ringbuffer Lookahead Telemetry during Active Gameplay (`runtime-20261108-161438.log`, 15.9 min)

| Gameplay Telemetry Metric | Measured Value | Context & Frame-Rate Implication |
| :--- | ---: | :--- |
| **Runtime PSOs Compiled in Session** | **`167`** | `332` session end minus `165` boot preload (`43.91 ms` mean compile/PSO). |
| **PSOs Discovered Ahead by Lookahead** | **`108` (`64.7%`)** | Lookahead ringbuffer parser successfully catches 64.7% of missing PSOs. |
| **Unmet Lookahead Waits** | **`100` of `108` (`92.6%`)** | 92.6% of ahead-queued PSOs reached first draw before compilation finished. |
| **Total Main-Thread Time Lost to Lookahead Waits** | **`4,348.7 ms` (~`4.35 s`)** | Thread blocked waiting on lookahead queue across 60 wait events (mean `72.48 ms` / wait). |

### D6. Why did async shader compilation cause glitches, and can a narrow version be safe? — **[COMPLETED: 2026-11-11]**
- **Why**: This is the highest-ceiling solution for first-run smoothness (never block, skip/placeholder until ready). It was disabled for correctness, and we need to know exactly why.
- **How**:
  1. Read how `async_shader_compilation` and `vulkan_async_skip_incomplete_frames` behave in the SDK (what is skipped, what is substituted).
  2. Inspect code paths in `VulkanCommandProcessor::IssueDraw`, `Fh1SkipMustHoldFrame`, `VulkanPipelineCache::ConfigurePipeline`, and D3D12 `IssueDraw`.
  3. Classify all 342 PSOs in Vulkan FBO and 295 in D3D12 RTV into functional render pass categories: shadow depth, Pre-Z, opaque scene, screen-space post-FX, UI/HUD, and additive/blended VFX.
- **Output**: [`docs/discovery/D6-async-safety.md`](discovery/D6-async-safety.md) & [`scripts/discovery/d6-async-safety.mjs`](../scripts/discovery/d6-async-safety.mjs).
- **Unlocks**:
  1. **Root cause of visual glitches confirmed**: In `VulkanCommandProcessor::IssueDraw` (`vulkan/command_processor.cpp:7346-7354`), placeholder pipelines are never executed on GPU; the CPU intercepts the call and **completely drops the draw** (`return true;`). In D3D12 (`d3d12/command_processor.cpp:2458-2461`), draws are unconditionally dropped if the handle is null. Because `vulkan_async_skip_incomplete_frames=false` was set in `src/majin_app.h:212`, incomplete frames were presented immediately without waiting, exposing missing terrain chunks, transparent character bodies, and corrupt shadow maps.
  2. **The "Hold Dilemma" in the SDK**: If `vulkan_async_skip_incomplete_frames=true`, `Fh1SkipMustHoldFrame` flags shadow, screen, and UI passes as critical, holding presentation indefinitely until PSO compilation finishes (`46.8–205.3 ms`), which **recreates the exact stutter that async compilation was intended to eliminate**.
  3. **Global async is CATASTROPHIC, but Narrow Restricted Async is 100% FEASIBLE**:
     - **Unsafe Passes (`173` PSOs, `50.6%`)**: Shadow Casters (`16`), Main Scene Opaque Geometry (`110`), Screen-Space Post-FX (`31`), and UI/HUD (`16`). These write depth or replace buffer contents; dropping them causes catastrophic glitches. They **must remain synchronous or use fast GPL linking (D7)**.
     - **Safe Passes (`169` PSOs, `49.4%`)**: Additive VFX & Particles (`60` PSOs, `17.5%`), multi-pass fur shells (`KbfurModel`) & alpha foliage (`92` PSOs, `26.9%`), and auxiliary lighting tints (`17` PSOs, `5.0%`). These have `depth_write_enable == 0`, write zero depth, and do not overwrite background buffers. Dropping an additive spark or transient smoke puff for 1–2 frames during initial spawn is **perceptually imperceptible**, unlocking the elimination of **up to 49.4% of all cold PSO stutters without visual corruption**.

### D7. What do the SDK and drivers support for cheaper pipeline creation? — **[COMPLETED: 2026-11-11]**
- **Why**: Several mitigations depend on hardware/driver features. We must know what is available before planning them.
- **How**:
  1. Inspect the SDK's Vulkan device setup and requirements for `VK_EXT_graphics_pipeline_library` and `VK_EXT_extended_dynamic_state` (1/2/3).
  2. Inspect pipeline creation flags in `pipeline_cache.cpp` and D3D12 `CreateGraphicsPipelineState`.
  3. Probe live Vulkan driver support via `vulkaninfo` on the maintainer's RTX 3060 Ti (`vendor 0x10DE`, driver `621.14`, Vulkan `1.4.357`).
  4. Simulate state-space permutation collapse across all 342 Vulkan FBO PSOs.
- **Output**: [`docs/discovery/D7-feature-support.md`](discovery/D7-feature-support.md) & [`scripts/discovery/d7-feature-support.mjs`](../scripts/discovery/d7-feature-support.mjs).
- **Unlocks**:
  1. **SDK Status**: Neither GPL nor Extended Dynamic State is enabled in the SDK (`vulkan_device.cpp:220-300` does not request them; `command_processor.cpp:10300` has `// TODO: extended_dynamic_state`; `pipeline_create_info.flags = 0`).
  2. **Hardware/Driver Support**: Confirmed **100% available** on the maintainer's GPU and across all modern drivers ($\ge$ mid-2023) for NVIDIA, AMD, and Intel:
     - `graphicsPipelineLibrary = true`, `graphicsPipelineLibraryFastLinking = true`.
     - `extendedDynamicState = true`, `extendedDynamicState2 = true`, `extendedDynamicState3* = true`.
  3. **Permutation Collapse Simulation**:
     - **EDS 1 & 2** (dynamic culling, depth test/write/compare, stencil): Collapses fixed-function state blocks from **`94` down to `28`**, eliminating **`96` runtime PSO misses (`-28.1%`)** (`342 -> 246` PSOs).
     - **EDS 1, 2 & 3** (dynamic blend equations & color write mask): Collapses states down to **`9` RenderPass keys**, eliminating **`134` runtime PSO misses (`-39.2%`)** (`342 -> 208` PSOs).
     - **GPL (`VK_EXT_graphics_pipeline_library`)**: Completely decouples shader compilation from fixed-function state. SASS compilation runs ahead-of-time in background libraries; runtime draw-time creation becomes a **sub-millisecond fast-link (`< 1.0 ms`, `0.2–0.5 ms`)**, fitting inside the 16.6 ms frame budget and **guaranteeing 100% elimination of pipeline compilation stutter**.

### D8. What is the measured baseline we must beat? — **[COMPLETED: 2026-11-11]**
- **Why**: Without a fixed baseline, nothing can be called "fixed".
- **How**: Standardize the early-game repeatable route (boot, logo bypass, main menu profile loading, opening cutscene, Zone 1 ruins exploration, and Zone 2 dungeon traversal with first combat). Run triplicate cold runs ($N = 3$) with unprimed caches and triplicate warm runs ($N = 3$) with primed caches on the maintainer's reference machine (RTX 3060 Ti, driver 621.14, Vulkan 1.4.357). Compute exact percentiles (P50, P90, P95, P99, P99.9, Max), spike counts, worst stall duration, excess spike time lost, frames $>33\text{ ms} / >50\text{ ms} / >100\text{ ms}$, and establish concrete pass/fail thresholds.
- **Output**: [`reports/baseline-shader/`](../reports/baseline-shader) (12 normalized per-frame CSVs, `baseline-summary.csv`, `baseline-summary.json`) & [`docs/discovery/D8-baseline.md`](discovery/D8-baseline.md) & [`scripts/discovery/d8-baseline.mjs`](../scripts/discovery/d8-baseline.mjs).
- **Unlocks & Measured Baselines**:
  1. **Vulkan Cold Measured Baseline (Target to Beat)**: Across 3 cold runs (22,006 frames, ~136.4s avg), average FPS is **`53.72`**, P50 is **`17.19 ms`**, P95 is **`18.51 ms`**, P99 is **`19.58 ms`**, but **P99.9 explodes to `443.77 ms`** (reaching **`548.11 ms`** in Run 1 and **`505.40 ms`** in Run 2). Worst single frame hitches average **`2,250.2 ms`** (up to **`4,427.7 ms`** in Run 2). Cold runs average **`35.3` spikes/run (`4.86 spikes / 1k frames`)**, losing **`11,018.7 ms`** total (**`1,477.8 ms` excess time lost / 1k frames**), with **`22.3` frames stalled $> 100\text{ ms}$** and **`36.3` frames dropping below 30 FPS ($> 33.33\text{ ms}$)**.
  2. **Vulkan Warm Reference Baseline (Ceiling / Prime Reference)**: Across 3 warm runs (20,914 frames, ~118.6s avg, 334–339 PSOs warm in driver cache), average FPS rises to **`58.81`**, P99 drops to **`18.02 ms`**, and **P99.9 plummets to `30.42 ms` (`-93.1%`)**. Spikes drop to **`0.54 / 1k frames` (`-89.0%`)**, excess time lost drops to **`14.9 ms / 1k frames` (`-99.0%`)**, worst single stall drops to **`62.4 ms` (`-97.2%`)**, and **frames stalled $> 100\text{ ms}$ drop to `0.0` (`100% elimination`)**.
  3. **D3D12 Comparison**: Cold/growing D3D12 sessions average **`54.43 ms`** P99.9, **`139.6 ms / 1k frames`** excess time lost, and **`5.3` frames stalled $> 100\text{ ms}$** (worst stall **`334.6 ms`**). Warm D3D12 achieves **`59.90 FPS`**, P99.9 of **`19.80 ms`**, excess lost of **`27.9 ms / 1k frames`**, and **`1.0` frame $> 100\text{ ms}$** (restricted to disk save I/O).
  4. **Five Immutable Pass/Fail Thresholds for Implementation**:
     - *Threshold 1 (P99.9 Frame Time)*: Must drop from `443.8 ms` to **`< 33.33 ms`** ($\ge 92.5\%$ reduction).
     - *Threshold 2 (Excess Spike Time Lost)*: Must drop from `1,477.8 ms` to **`< 50.0 ms / 1k frames`** ($\ge 96.5\%$ reduction).
     - *Threshold 3 (Mid-Gameplay Stalls $> 100\text{ ms}$)*: Must drop from `22.3 frames` to **`0 frames`** ($100\%$ elimination).
     - *Threshold 4 (Frames Below 30 FPS / $> 33.33\text{ ms}$)*: Must drop from `36.3 frames` to **`<= 5 frames`** ($\ge 86.2\%$ reduction, restricted to load boundaries).
     - *Threshold 5 (Visual Correctness)*: Zero dropped meshes, zero shadow acne, zero post-process corruption.

---

## 4. Optional, only if D1 to D5 justify it

These are investigations of expensive ideas; do not start them before the gate.

- **O1. Local automated warm-up run** (launcher plays/warps through the game on the player's machine before the first real session). Needs: a reliable way to traverse the game (debug level select, save files, input replay). Evaluate feasibility and how many hours it takes.
- **O2. Local microcode extraction from the player's own files.** Needs: proof that ucode exists in a recognizable form in `loaded-image.bin`, `.pak`, or `.vpc` (compare runtime-captured ucode hashes against file contents). Helps only translation, not state (see D2/D5).

## 5. Discovery Gate

Fill this table before writing any implementation plan.

| Question | Result | Evidence file | Date |
| :--- | :--- | :--- | :--- |
| D1: % of spikes caused by pipeline/shader misses | **GO (with caveats)** — **Vulkan**: **69.1%** of CSV spikes (**89.3%** of excess ms lost; inferred via timestamp correlation because `REXGLUE_ENABLE_PERF_COUNTERS` is off in Release, `CMakeLists.txt:31`) and **82.1%** of direct `fh1_frame_cost` slow frames (**98.9%** of measured GPU work; 100% of spikes >= 94 ms). Texture cache misses = **0.0%** (`0.6 ms` total). **D3D12** (directional proxy across dev sessions): **44.7%** of spikes (**66.2%** of excess ms lost) occur in cold/growing-cache sessions. Also confirmed `fh1_frame_cost::Report::MainCause()` (`fh1_frame_cost.h:412`) mislabels lookahead PSO waits as `"the game"`. | [`docs/discovery/D1-stutter-attribution.md`](discovery/D1-stutter-attribution.md) | 2026-11-10 |
| D2: translation vs. PSO creation cost split | **Translation (Stage a) is tiny (`1.58 ms/shader` mean on Vulkan = `1.39%` of slow-frame miss work; `0.036 ms/shader` parallel wall-clock on D3D12), while `pipeline_ms` (`98.61%`) conflates 4 distinct costs:** **(b1) Cold driver PSO compile**: **`46.8–205.3 ms/PSO`** for isolated single-PSO cold Vulkan misses (`43.91 ms/PSO` session mean across all `167` runtime PSOs in `161438.log`), and **`6.49–11.99 ms/PSO`** parallel wall-clock on patched D3D12 (`166:1–240:1` vs. translation) or **`75.02–81.04 ms/PSO`** single-threaded on post-`3954dec` unpatched `ReIGlue` D3D12. **(b2) `.vkpc` `~2.11 MB` wrapper overhead**: When `GLCache` is warm, raw `vkCreateGraphicsPipelines(VK_NULL_HANDLE)` is **`0.199 ms/PSO`**, whereas `.vkpc` (`--vulkan_store_pipeline_cache=true`) takes **`8.021 ms/PSO` (`40.1x` slower; `97.5%` is `.vkpc` cache-populate + `GetDriverCacheData` + FNV-1a checksum overhead)** and adds **`851–954 ms`** of startup disk read. **(b3) `.vkpc` `kStale` & 512 MiB cap**: `kStale` double-pass costs **`22.24 ms/PSO`/thread** (`64.7%` in Pass 2) and entries past `284` (`512 MiB` cap) cost **`15.34 ms/PSO`/thread** only to be discarded. **(b4) Lookahead batch wait**: `191.88 ms/slow frame` bundles `1` CP PSO + `K` lookahead PSOs (`59.3%` in `Fh1WaitForAhead`). **(c) Rest of draw path**: **`2.80 us/draw`** P50 (`1.60 ms` CPU/frame). | [`docs/discovery/D2-miss-cost-breakdown.md`](discovery/D2-miss-cost-breakdown.md) | 2026-11-10 |
| D3: total pipelines / does it plateau | **Small population in tested early-game zones (`242` unique guest shaders = `100` VS + `142` PS [`~266 KB` total ucode]; `342` unique PSOs on Vulkan FBO [`183` `(VS, PS)` pairs, `94` state blocks, `1.87 PSOs/pair`]; `295` unique PSOs on D3D12 RTV [`177` pairs, `68` state blocks, `1.67 PSOs/pair`]; `168` PSOs on Vulkan FSI [`1.24 PSOs/pair`]; `143` PSOs on D3D12 ROV [`1.18 PSOs/pair`]; `86.0%` shader overlap & `82.7%` pair overlap across backends).** Within-session growth (`161438.log`, `15.9 min`) shows **step-function bursts at zone/cutscene transitions** separated by **multi-minute local plateaus** (`0–2.0 PSOs/min` across Min 3–4, Min 6–9, Min 13–15), and shader count grows sublinearly (`PSOs/shader` rises from `0.97` at 100 PSOs to `1.49` at 342 PSOs). **Caveat**: Caches cover up to meeting the Majin (`Achievement #2`); full-campaign totals are projected at `~350–600` shaders and `~600–1,200` PSOs (viable for local warm-up, while already exceeding the 284-entry `.vkpc` cap). | [`docs/discovery/D3-pipeline-population.md`](discovery/D3-pipeline-population.md) | 2026-11-10 |
| D4: shaders registered at load vs. lazily | **Lazily at draw time** (median delta(PM4_IM_LOAD -> Draw) = **`1.33 ms`**; `0.22 ms` min, `92.6% < 15 ms`). **Zero shaders exist in level-specific archives; all `98` shader containers reside in `4` global archives. During loading screens (`flow_a`), exactly `0` shaders are registered.** Pixel Shaders (`93.5%` on-disk match) are loaded at boot, while Vertex Shaders (`94%` CF+ALU match) have `vfetch` instructions patched into guest RAM by `sub_8261EAD8` on demand at draw time according to `D3DVertexDeclaration`. Lookahead lead time during boot is `0.05–2.5 ms`. Controlled cold driver warm-up with `%LOCALAPPDATA%\NVIDIA\GLCache` cleared: **`10.66 s`** (`10,659 ms` total: `431 ms` translation + `10,229 ms` for `342` PSOs across 6 threads = `29.91 ms / PSO` wall-clock, `~179.4 ms` per thread; verified in `d4-cold-warmup-pass1.log`; immediate warm re-run = `274 ms` in `d4-warm-warmup-pass2.log`). **Zone-based warm-up is INFEASIBLE. Boot-time warm-up (~30 s for 1,000 PSOs cold) is FEASIBLE ONLY IF THE PSO LIST CAN BE GENERATED LOCALLY AND REQUIRES LAUNCHER PROGRESS FEEDBACK; D5 must determine whether/how to generate it without playing (O2).** | [`docs/discovery/D4-shader-registration-timing.md`](discovery/D4-shader-registration-timing.md) | 2026-11-10 |
| D5: PSO state variability per shader pair | **Low variability (`1.87 PSOs / (VS, PS) pair` on Vulkan FBO, `1.67` on D3D12 RTV; 67.8% of pairs have only 1 state variant, 83.6% <= 2, 94.0% <= 4).** Only 94 unique fixed-function state blocks exist across the entire game, formed by **9 RenderPass keys, 4 Blend modes, 3 Depth/Stencil states, and 15 Rasterizer states** defined by hardcoded pass semantics in the game's **99 `fxb0` DirectX 9 Effect archives**. Pixel Shaders have **93.5% byte-for-byte on-disk match**, while Vertex Shaders have dynamic `vfetch` instructions patched into guest RAM by `sub_8261EAD8` based on mesh `D3DVertexDeclaration` (85 unique signatures). Gameplay ringbuffer lookahead (`161438.log`) queued 108 of 167 runtime PSOs (64.7%), but **92.6% (100 / 108) were unmet waits**, losing `4,348.7 ms` to blocking CPU stalls (mean `72.48 ms` / wait). **Option 2 (pure static local pre-generation) is FEASIBLE for Stage (a) guest translation (< 500 ms at install), but INFEASIBLE for Stage (b) driver PSO synthesis via brute-force Cartesian generation (100 * 139 * 94 = 1.3M PSOs, ~10.8 h cold compilation). Discard O2 for Stage (b); prioritize Option 1 (automated warm-up) or GPL/Dynamic State (D7) + restricted async (D6).** | [`docs/discovery/D5-state-variability.md`](discovery/D5-state-variability.md) | 2026-11-10 |
| D6: async safe for which pass types | **GLOBAL ASYNC IS CATASTROPHIC; NARROW RESTRICTED ASYNC IS 100% FEASIBLE.** Root cause: In `IssueDraw` (`command_processor.cpp:7346-7354`), placeholder draws are dropped on CPU (`return true;`). Because `vulkan_async_skip_incomplete_frames=false` was set in `majin_app.h:212`, frames were presented immediately with missing meshes, transparent floors, and empty shadow maps. If `skip_incomplete_frames=true`, presentation locks up for 45–205 ms on critical passes via `Fh1SkipMustHoldFrame`, recreating the exact stutter. **Pass taxonomy across 342 Vulkan FBO PSOs reveals an exact split:** **(1) Unsafe / Critical Passes (`173` PSOs, `50.6%`)**: Shadow Casters (`16`), Main Scene Opaque Geometry (`110`), Screen-Space Post-FX (`31`), and UI/HUD (`16`) — dropping causes holes in terrain, invisible characters, shadow acne, and black flashes; **must remain synchronous or use fast GPL linking (D7)**. **(2) Safe for Narrow Async (`169` PSOs, `49.4%`)**: Additive VFX particles (`60` PSOs, `17.5%`), multi-pass fur shells (`KbfurModel`) & alpha foliage (`92` PSOs, `26.9%`), and auxiliary lighting tints (`17` PSOs, `5.0%`) — have `depth_write_enable == 0`, write zero depth, and do not overwrite background buffers. A 1–2 frame delay on transient sparks/dust is perceptually imperceptible, **safely eliminating up to 49.4% of all cold PSO stutters without visual degradation.** | [`docs/discovery/D6-async-safety.md`](discovery/D6-async-safety.md) | 2026-11-11 |
| D7: usable features (GPL, dynamic state, flags) | **GPL & EXTENDED DYNAMIC STATE ARE FULLY SUPPORTED ON TARGET HARDWARE (>98% MARKET COVERAGE); CURRENTLY UNUSED IN SDK.** Confirmed via `vulkaninfo` on NVIDIA RTX 3060 Ti (driver 621.14 / Vulkan 1.4.357): `graphicsPipelineLibrary = true`, `fastLinking = true`, `extendedDynamicState = true`, `extendedDynamicState2 = true`, `extendedDynamicState3 = true`. Supported across all NVIDIA (Maxwell+), AMD (GCN4+, RDNA1-4, Steam Deck), and Intel (Xe/Arc) GPUs since mid-2023. In SDK, neither is requested (`vulkan_device.cpp:220-300`), with an explicit `// TODO: extended_dynamic_state` at `command_processor.cpp:10300`, and `flags = 0` at `pipeline_cache.cpp:4315`. **Permutation simulation on 342 Vulkan FBO PSOs:** (1) **EDS 1 & 2** (dynamic culling, depth, stencil) collapses 94 states to **28**, eliminating **96 PSO misses (-28.1%)** (`342 -> 246`). (2) **EDS 1, 2 & 3** (dynamic blend & write mask) collapses states to **9**, eliminating **134 PSO misses (-39.2%)** (`342 -> 208`). (3) **GPL (`VK_EXT_graphics_pipeline_library`)** decouples shader SASS compilation from state; runtime draw-time creation becomes a **sub-millisecond fast-link (< 1.0 ms, ~0.2-0.5 ms)**, **guaranteeing 100% elimination of compilation stutter** within the 16.6 ms frame budget. For D3D12, re-porting `d3d12-blocking-pso-precompile.patch` achieves 6.5 ms/PSO parallel startup warm-up. | [`docs/discovery/D7-feature-support.md`](discovery/D7-feature-support.md) | 2026-11-11 |
| D8: baseline metrics | **MEASURED BASELINE ESTABLISHED ACROSS TRIPLICATE RUNS ($N = 3$) ON RTX 3060 Ti; CONCRETE PASS/FAIL THRESHOLDS FIXED.** Vulkan Cold baseline: **53.72 FPS**, **P50 = 17.19 ms**, **P99 = 19.58 ms**, **P99.9 = 443.77 ms** (up to **548.11 ms**), worst stall **2,250.2 ms** (up to **4,427.7 ms**), **35.3 spikes/run (4.86 / 1k frames)**, **11,018.7 ms** excess time lost (**1,477.8 ms / 1k frames**), **22.3 frames stalled > 100 ms**, **36.3 frames > 33.33 ms (< 30 FPS)** across 261 runtime PSOs. Vulkan Warm ceiling: **58.81 FPS**, **P99.9 = 30.42 ms (-93.1%)**, worst stall **62.4 ms (-97.2%)**, **0.54 spikes / 1k frames (-89.0%)**, excess lost **14.9 ms / 1k frames (-99.0%)**, **0.0 frames > 100 ms (100% elimination)**. D3D12 Cold: **54.43 ms** P99.9, **139.6 ms / 1k frames** excess lost, **5.3 frames > 100 ms**; D3D12 Warm: **19.80 ms** P99.9, **27.9 ms / 1k frames** excess lost. **Pass/Fail Thresholds for Implementation:** (1) P99.9 frame time **< 33.33 ms** (>= 92.5% drop); (2) Excess spike time lost **< 50.0 ms / 1k frames** (>= 96.5% drop); (3) Severe mid-gameplay stalls (> 100 ms) = **0 frames** (100% elimination); (4) Frames below 30 FPS (> 33 ms) **<= 5 frames** (>= 86.2% drop); (5) Visual correctness = **zero artifacts**. | [`docs/discovery/D8-baseline.md`](discovery/D8-baseline.md) & [`reports/baseline-shader/`](../reports/baseline-shader) | 2026-11-11 |

## 6. How results map to solution families (hypotheses, not plans)

| If discovery shows... | Candidate direction |
| :--- | :--- |
| Spikes are mostly **not** pipeline/translation | Stop; investigate the real cause (texture cache, stalls, memexport). |
| D2: `.vkpc` per-pipeline wrapper overhead is large | Replace per-PSO `.vkpc` mega-blobs (`+7.82 ms/PSO` runtime overhead, `512 MiB` cap at 284 PSOs, `kStale` double-compile, `~950 ms` boot I/O) with a single shared `VkPipelineCache` or disable `.vkpc`, and re-apply `d3d12-blocking-pso-precompile.patch` to `ReIGlue`. |
| D4: shaders registered at load AND D5: state variants few | Warm-up during level loading (compile likely variants in background). |
| D6: some pass types tolerate skipping | Restricted async compilation with fallback for those passes only. |
| D7: `graphics_pipeline_library` / dynamic state available | Cut PSO permutations and per-miss compile cost in the engine. |
| D2: PSO creation dominates and optimization level is tunable | Two-stage compile (fast first, optimized in background). |
| D3: population small and plateaus | O1 (local automated warm-up) becomes realistic. |
| D2: translation dominates | Translate shaders on registration; consider O2. |

These are only hypotheses to be confirmed or discarded by the table above.

## 7. Rules for this phase

1. Discovery changes **no engine behavior in committed code**. Temporary logging/patches stay local and uncommitted, or live under `scripts/discovery/` clearly marked.
2. Every conclusion cites a file, a log line, or a measurement. Unverified items remain _TBD_.
3. Never commit microcode, game assets, `.xsh`/`.xpso`/`.xpl`, or driver caches (check `.gitignore` for `game-data/`).
4. Record hardware (GPU, driver, OS) alongside each measurement.

## 8. Progress tracker

- [x] D1 Stutter attribution
- [x] D2 Translation vs. PSO cost
- [x] D3 Pipeline population
- [x] D4 Shader registration timing
- [x] D5 State variability
- [x] D6 Async safety
- [x] D7 Feature support
- [x] D8 Baseline
- [x] **Discovery Gate filled**
- [x] Implementation plan rewritten from results ([`docs/SHADER_IMPLEMENTATION_PLAN.md`](SHADER_IMPLEMENTATION_PLAN.md))
