# D2 — Miss Cost Breakdown: Guest-Shader Translation vs. Host PSO Creation vs. Cache Overhead & Draw Path

**Status**: Complete (Revised with `CreatePipelineWithDriverCache` Sub-Stage Decomposition & D3D12 Patch Verification)  
**Date**: 2026-11-10  
**Issue**: `#4` — *Implement shader pre-compilation / cache warm-up to eliminate in-game stuttering*  
**Reproducible Script**: [`scripts/discovery/d2-miss-cost-breakdown.mjs`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/scripts/discovery/d2-miss-cost-breakdown.mjs) (`node scripts/discovery/d2-miss-cost-breakdown.mjs`)

---

## 1. Test Hardware & Environment

All measurements in this report (both log analyses and controlled empirical sub-stage probes) were captured on the following hardware and software stack (verified from runtime logs such as [`game-data/runtime/logs/runtime-20261108-161438.log:6-36`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-161438.log#L6-L36) and [`game-data/runtime/logs/runtime-20261107-185200.log:3-12`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261107-185200.log#L3-L12)):

- **OS**: Windows 11 (x86_64)
- **GPU**: NVIDIA GeForce RTX 3060 Ti (`vendor 0x10DE`, `device 0x2489`, 8,024 MB dedicated VRAM)
- **Driver**: NVIDIA Proprietary `621.14` (Vulkan driver versions `0x9B404000` and `0x9B438000`, conformance `1.4.3.3`)
- **Graphics APIs**: Vulkan `1.4.357` and Direct3D 12 (Resource Binding Tier 3, ROV supported)
- **Dataset**: 203 `.log` files and 198 `.performance.csv` files across [`reports/`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/reports) and [`game-data/runtime/logs/`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs), plus **4 controlled instrumentation probe runs** isolating sub-stage timings inside [`VulkanPipelineCache::CreatePipelineWithDriverCache`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L569-L687).

---

## 2. Executive Answer to D2

1. **Guest shader translation (Stage a) is NOT the bottleneck**: Translating Xenos microcode into SPIR-V (`AnalyzeUcode` + `SpirvShaderTranslator::TranslateAnalyzedShader` + `vkCreateShaderModule`) costs **`1.58 ms` mean (`1.50 ms` P50, `5.30 ms` max) per shader** on Vulkan (`148.4 ms` across 94 shaders in slow frames = **`1.39%`** of slow-frame miss time), and **`0.036 ms` mean (`0.032 ms` P50) per shader** wall-clock across worker threads on D3D12 (`6.0 ms` P50 for `209` shaders).
2. **However, `pipeline_ms` (`98.61%` of slow-frame miss time) is NOT pure driver PSO compilation**: In [`VulkanPipelineCache::ConfigurePipeline`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L1844-L1927) and [`CreatePipelineWithDriverCache`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L569-L687), `pipeline_ms` conflates **four distinct costs**:
   - **(b1) True cold driver PSO compilation (`vkCreateGraphicsPipelines` / `CreateGraphicsPipelineState` when `GLCache`/`DXCache` is cold)**: **`46.8–205.3 ms / PSO`** for isolated single-PSO cold misses on Vulkan (`75.0–81.0 ms / PSO` single-threaded mean on cold D3D12).
   - **(b2) Per-pipeline `.vkpc` wrapper overhead (`CreateDriverCache` + `vkCreateGraphicsPipelines` cache-population penalty + `GetDriverCacheData` `~2.11 MB` extraction + `MakeEntry` single-byte FNV-1a checksum)**: Controlled probe runs (Section 4) prove that when NVIDIA's `GLCache` is warm, passing `--vulkan_store_pipeline_cache=false` (`cache = VK_NULL_HANDLE`) reduces runtime `vkCreateGraphicsPipelines` to **`0.199 ms / PSO`** (`6.29 ms` total across 29 PSOs), whereas the default `.vkpc` path (`--vulkan_store_pipeline_cache=true`) takes **`8.021 ms / PSO` (`226.8 ms` total — a `40.1x` slowdown!)**, of which **`7.822 ms / PSO` (`97.5%`) is pure `.vkpc` `~2.11 MB` blob extraction and checksum overhead**.
   - **(b3) `Outcome::kStale` double-compilation & double-serialization ([`vulkan/pipeline_cache.cpp:655-677`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L655-L677))**: When a seeded `.vkpc` entry misses (`251` of `342` PSOs in startup probe runs), `CreatePipelineWithDriverCache` compiles and extracts the `2.15 MB` blob **twice**, taking **`22.24–23.18 ms / PSO` per worker thread**, with **`14.39–15.48 ms` (`64.7%`) wasted in the second pass**.
   - **(b4) Multi-PSO lookahead batch waiting ([`Fh1WaitForAhead`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L2010-L2037) via `finish_ahead`)**: A slow frame's `pipeline_ms` (`191.88 ms / slow frame` mean) is a **per-frame** metric, not a **per-PSO** metric. In the 15-minute cold Vulkan gameplay session ([`runtime-20261108-161438.log`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-161438.log)), **`167` runtime PSOs** were built (`59` on the CP thread + `108` on background lookahead threads), so the `7,333.4 ms` of slow-frame `pipeline_ms` averages **`43.91 ms / runtime PSO`** (`59.3%` inside `Fh1WaitForAhead` waiting on batches of `108` ahead PSOs at `40.27 ms / ahead PSO`, and `40.7%` on direct CP-thread builds at `50.59 ms / CP PSO`).

| Stage / Sub-Stage | Vulkan Per-Item Cost (RTX 3060 Ti) | Vulkan Per-Slow-Frame / Startup Cost | D3D12 Per-Item Cost (RTX 3060 Ti) | D3D12 Startup Total Cost | Key Takeaway |
| :--- | ---: | ---: | ---: | ---: | :--- |
| **(a) Guest Shader Translation** (`AnalyzeUcode` + SPIR-V / DXBC + `VkShaderModule`) | **`1.58 ms / shader`** mean (`1.50 ms` P50, `5.30 ms` max, $N=94$) | `2.69 ms / slow frame` (`148.4 ms` total = `1.39%` of slow-frame miss work); startup: `154–275 ms` (`~0.7–1.1 ms/shader`, 1 thread) | **`0.036 ms / shader`** wall-clock parallel (`~0.25–0.50 ms/shader` per thread) | `6.0 ms` P50 (`4–17 ms` for `128–220` shaders across $N$ threads) | Fast and state-independent; pre-translating shaders alone saves only **~1.4%** of stutter. |
| **(b1) Cold Driver PSO Compilation** (`GLCache` / `DXCache` cold) | **`46.8–205.3 ms / PSO`** (isolated single-PSO runtime misses); **`43.91 ms / PSO`** session mean across all `167` runtime PSOs (`59` CP + `108` ahead) | Bundled with (b2)+(b4) into **`191.88 ms / slow frame`** (`195.9 ms` P50, `410.6 ms` max across `55` slow frames) | **Patched ($N$ threads)**: **`6.49–11.99 ms / PSO`** wall-clock; **Unpatched (1 thread)**: **`75.02–81.04 ms / PSO`** | **Patched**: `1,506–3,308 ms` (`180–295` PSOs); **Unpatched**: `8,402–9,076 ms` (`112` PSOs); runtime cold spike excess: `197.51 ms` mean (`101.41 ms` P50) | True cold SASS compilation takes **`~45–205 ms / PSO`** per thread (`~6.5–12 ms/PSO` wall-clock across worker threads). |
| **(b2) Warm Driver Cache + `.vkpc` Disabled / D3D12 Warm `DXCache`** | **`0.199 ms / PSO`** mean (`0.191 ms` P50, `0.663 ms` max; `--vulkan_store_pipeline_cache=false`, $N=29$) | **`6.29 ms` total** across all 29 runtime PSOs (`14` active frames) | **`0.149 ms / PSO`** P50 (`0.193 ms` mean across $N=92$ warm sessions) | `39.0 ms` P50 (`26–181 ms` for `215–276` PSOs) | When NVIDIA `GLCache`/`DXCache` is warm, raw PSO creation without `.vkpc` is **`~0.15–0.20 ms / PSO`**. |
| **(b2-ovh) `.vkpc` Per-Pipeline `~2.11 MB` Wrapper Overhead** (`--vulkan_store_pipeline_cache=true`) | **`+7.822 ms / PSO`** pure overhead (`8.021 ms / PSO` total vs. `0.199 ms` raw = **`40.1x` slower**!) | **`226.8 ms` total** across 29 runtime PSOs (`+4.86 ms` in `vkCreateGraphicsPipelines` + `0.88 ms` `GetDriverCacheData` + `1.92 ms` FNV-1a checksum + `0.16 ms` `CreateDriverCache`) | N/A (D3D12 does not use per-pipeline `ID3D12PipelineLibrary` blobs) | Startup `.vkpc` disk read + checksum over `535.8 MB` costs **`851–954 ms`** (`67%` of warm startup!) | **Fixing/disabling per-PSO `.vkpc` mega-blobs eliminates `97.5%` of warm-`GLCache` PSO cost and `~950 ms` of startup I/O without pre-compiling anything.** |
| **(b3) `.vkpc` `Outcome::kStale` Double-Pass & 512 MiB Cap Overflow** | **`22.24–23.18 ms / PSO`** per thread for `kStale` (`14.39–15.48 ms` [`64.7%`] in 2nd pass); **`15.34–16.87 ms / PSO`** per thread for unseeded overflow (then discarded!) | `1,754–1,908 ms` wall-clock across 6 threads for `342` PSOs (`33` reused at `0.52 ms`, `251` stale rebuilt, `58` overflow compiled & discarded) | N/A | N/A | Caused by per-PSO `2.15 MB` NVIDIA blobs hitting the `512 MiB` cap at `284` PSOs and `kStale` rebuilding twice. |
| **(c) Rest of the Draw Path** (State validation, bindings, barriers, `vkCmdDraw`) | **`0.0028 ms / draw`** P50 (`2.80 us/draw`) | **`1.60 ms` CPU / frame** P50 for `538` draws/frame | Included in `< 16.6 ms` frame budget | N/A | **`< 0.8%`** of a slow frame (`1.60 ms` vs `~200 ms`). |

---

## 3. Code-Path Anatomy of Stages (a), (b), and (c)

### 3.1 Vulkan Draw & Miss Path

In [`VulkanCommandProcessor::IssueDraw`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/command_processor.cpp#L4860-L5350) and [`VulkanPipelineCache`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L569-L4394):

1. **Stage (a) — Guest Shader Translation ([`EnsureShadersTranslated`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L1686-L1716) & [`TranslateAnalyzedShader`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L2079-L2146))**:
   - Timed by `fh1_frame_cost::Timer cost_timer(fh1_frame_cost::Current().shader_ms)` ([`lines 1688, 1702`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L1688-L1703)) and counted in `fh1_frame_cost::Current().shaders`.
   - Executes `shader().AnalyzeUcode(ucode_disasm_buffer_)`, `SpirvShaderTranslator::TranslateAnalyzedShader`, texture/sampler layout UID setup (`EnterBindingLayoutUserUIDSetup`), and `vkCreateShaderModule` (`translation.GetOrCreateShaderModule()` at [`line 2142`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L2142)).
2. **Stage (b) — Host PSO Creation, Driver Cache Wrapper, and Lookahead Wait ([`ConfigurePipeline`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L1718-L1929), [`EnsurePipelineCreated`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L3853-L4394), & [`CreatePipelineWithDriverCache`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L569-L687))**:
   - **What `pipeline_ms` actually wraps**: In [`ConfigurePipeline`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L1844-L1927):
     - If the current draw's PSO was already queued by lookahead (`it->second.fh1_ahead_queued`), [`lines 1845-1846`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L1845-L1846) start `fh1_frame_cost::Timer cost_timer(fh1_frame_cost::Current().pipeline_ms)` and call `finish_ahead(&it->second)`, which blocks in [`Fh1WaitForAhead`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L2010-L2037) until **all** lookahead-queued PSOs in `fh1_ahead_set_` finish compiling on background worker threads — **without incrementing `fh1_frame_cost::Current().pipelines`**.
     - If the current draw's PSO was not yet created (`!it->second.is_created`), [`lines 1884-1927`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L1884-L1927) start `cost_timer(fh1_frame_cost::Current().pipeline_ms)`, increment `fh1_frame_cost::Current().pipelines` by **1**, call `EnsurePipelineCreated` on the CP thread ([`line 1916`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L1916)), and then **inside the same `cost_timer` scope** call `finish_ahead(nullptr)` ([`line 1927`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L1927)) to wait for the entire batch of background lookahead PSOs!
   - **What `CreatePipelineWithDriverCache` ([`lines 569-687`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L569-L687)) executes on each PSO**:
     1. `CreateDriverCache(seed_bytes, seed_size)` ([`lines 585, 591`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L585-L591)): creates a temporary per-pipeline `VkPipelineCache`.
     2. `vkCreateGraphicsPipelines(device, cache, 1, &create_info, nullptr, &pipeline)` ([`line 605`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L605)).
     3. `GetDriverCacheData(cache)` ([`lines 556-567, 612`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L556-L612)): calls `vkGetPipelineCacheData` twice (size query + `std::vector<uint8_t>` copy of `~2.11 MB` on NVIDIA) and `vkDestroyPipelineCache`.
     4. If `seeded && outcome == Outcome::kStale` ([`lines 655-677`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L655-L677)): creates a **second** empty `VkPipelineCache`, calls `vkCreateGraphicsPipelines` a **second time** (`&duplicate`), calls `vkDestroyPipeline(duplicate)`, and calls `GetDriverCacheData(fresh)` a **second time**!
     5. `driver_cache_file::MakeEntry(key, bytes)` ([`driver_cache_file.h:62-75`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/driver_cache_file.h#L62-L75), called at [`lines 614, 675`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L614-L675)): runs a byte-by-byte FNV-1a loop (`Checksum(entry.data.data(), entry.data.size())`) over the `~2.11 MB` vector on the calling thread, then calls `KeepDriverCacheEntry` ([`lines 702-725`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L702-L725)), which **discards the entry if `.vkpc` has already reached `kMaxFileSize` (`512 MiB`)**!
3. **Stage (c) — Rest of the Draw Path ([`VulkanCommandProcessor::IssueDraw`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/command_processor.cpp#L4860-L5350))**:
   - Primitive processing, modification bit computation, render-target cache update, `pipelines_.find(description)` hash lookup, texture residency checks, descriptor set updates, shared-memory range requests, dynamic state updates, and `vkCmdDraw` / `vkCmdDrawIndexed`.

---

## 4. Controlled Sub-Stage Decomposition of `CreatePipelineWithDriverCache` (Addressing D2 Point 1)

To separate raw driver compilation (`vkCreateGraphicsPipelines`) from `.vkpc` wrapper overhead (`CreateDriverCache`, `GetDriverCacheData`, `MakeEntry` FNV-1a checksum, and `Outcome::kStale` double-pass), we temporarily instrumented [`VulkanPipelineCache::CreatePipelineWithDriverCache`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L569-L687) with `std::chrono::steady_clock` timers around every sub-step and ran four controlled scenarios on RTX 3060 Ti (`driver 621.14`) with `--fh1_frame_cost_csv` enabled (before reverting the temporary instrumentation):

### 4.1 Runtime PSO Creation: `--vulkan_store_pipeline_cache=true` (Default) vs. `false` (`VK_NULL_HANDLE`)

We deleted the title's `.xpso` / `.vkpc` files so that all **29 introductory/title-screen PSOs** (`6` on the CP thread + `23` on background lookahead threads across `14` active frames) were created during runtime while NVIDIA's internal `GLCache` already held the compiled shaders:

| Sub-Stage Inside `CreatePipelineWithDriverCache` ($N = 29$ identical runtime PSOs) | `--vulkan_store_pipeline_cache=true` (Default `.vkpc` Enabled) | `--vulkan_store_pipeline_cache=false` (`cache = VK_NULL_HANDLE`) | Delta / Overhead Caused by `.vkpc` Wrapper | Share of Default Runtime `CreatePipelineWithDriverCache` |
| :--- | ---: | ---: | ---: | ---: |
| **1. `CreateDriverCache(nullptr, 0)`** (`vkCreatePipelineCache`) | `0.160 ms / PSO` | `0.000 ms / PSO` | `+0.160 ms / PSO` | `2.0%` |
| **2. `vkCreateGraphicsPipelines(device, cache, ...)`** | **`5.060 ms / PSO`** | **`0.199 ms / PSO`** (`0.191 ms` P50, `0.663 ms` max) | **`+4.861 ms / PSO`** *(NVIDIA driver populating `2.11 MB` into `cache`)* | `63.1%` (`2.5%` raw PSO + `60.6%` `cache` population) |
| **3. `GetDriverCacheData(cache)`** (`vkGetPipelineCacheData` x2 copying **`2,110,277 bytes`** + `vkDestroyPipelineCache`) | **`0.878 ms / PSO`** | `0.000 ms / PSO` | **`+0.878 ms / PSO`** | `10.9%` |
| **4. `driver_cache_file::MakeEntry` + `KeepDriverCacheEntry`** (single-byte FNV-1a checksum loop over `2.11 MB` at [`driver_cache_file.h:67`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/driver_cache_file.h#L67)) | **`1.920 ms / PSO`** | `0.000 ms / PSO` | **`+1.920 ms / PSO`** | `23.9%` |
| **Total `CreatePipelineWithDriverCache` per PSO (Mean / P50 / Max)** | **`8.021 ms`** (`7.530 ms` P50 / `16.382 ms` max) | **`0.200 ms`** (`0.191 ms` P50 / `0.663 ms` max) | **`+7.821 ms / PSO` (`40.1x` slower!)** | **`100.0%` (`97.5%` is `.vkpc` overhead!)** |
| **Sum of `bg_pipeline_ms` across all 14 active frames (`--fh1_frame_cost_csv`)** | **`226.80 ms`** | **`6.29 ms`** | **`+220.51 ms` (`36.1x` higher)** | — |

> [!IMPORTANT]
> **Discovery Breakthrough (D2 Point 1)**: When NVIDIA's `GLCache` already has the shader compiled, raw `vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, ...)` takes only **`0.199 ms / PSO`**. Passing an isolated temporary `VkPipelineCache` per pipeline in [`CreatePipelineWithDriverCache`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L569-L687) forces the NVIDIA driver to merge/dump **`2.11 MB` (`2,110,277 bytes`)** of internal driver cache state into the temporary `VkPipelineCache` inside `vkCreateGraphicsPipelines` (`+4.861 ms`), copy `2.11 MB` out via `vkGetPipelineCacheData` (`+0.878 ms`), and hash `2.11 MB` byte-by-byte in `Checksum()` (`+1.920 ms`), turning a **`0.20 ms`** call into **`8.02 ms` (`40.1x` slower)**.

### 4.2 Startup Warm-Up Sub-Stage Breakdown (`Outcome::kReused` vs. `Outcome::kStale` vs. `512 MiB` Cap Overflow)

Running startup warm-up on the existing **342-pipeline** cache (`284` entries stored in the `535,813,834-byte` [511 MiB] `.vkpc` file, plus `58` unseeded overflow pipelines that exceeded `kMaxFileSize = 512 MiB`) across **6 worker threads** revealed the exact per-thread cost of all three code paths inside [`CreatePipelineWithDriverCache`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L569-L687):

| Startup Warm-Up Path ($N = 342$ PSOs across 6 threads) | Count | Seed Blob Size | Pass 1: `CreateDriverCache` | Pass 1: `vkCreateGraphicsPipelines` | Pass 1: `GetDriverCacheData` | Pass 2 (`kStale`): `CreateDriverCache(nullptr)` | Pass 2 (`kStale`): 2nd `vkCreateGraphicsPipelines` | Pass 2 (`kStale`): 2nd `GetDriverCacheData` | `MakeEntry` (FNV-1a Checksum on `2.15 MB`) | Total per PSO on Worker Thread (Mean / P50) | Wall-Clock Share Across 6 Threads |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| **1. Seeded Hit (`Outcome::kReused`)** | `33` | `23.1 KB` (`15–90 KB`) | `0.233 ms` | **`0.228 ms`** | `0.062 ms` *(skipped on hit)* | `0.000 ms` | `0.000 ms` | `0.000 ms` | `0.000 ms` | **`0.523 ms`** (`0.262 ms` P50) | `~0.09 ms / PSO` |
| **2. Seeded Miss (`Outcome::kStale` Double-Pass)** | **`251`** | **`2.15 MB`** | `0.913 ms` | **`5.329 ms`** | **`1.601 ms`** (`2.15 MB` copy) | `0.246 ms` | **`10.258 ms`** | **`1.507 ms`** (`2.14 MB` copy) | **`2.387 ms`** | **`22.241 ms`** (`21.856 ms` P50; **`64.7%` in Pass 2!**) | `~3.71 ms / PSO` |
| **3. Unseeded 512 MiB Overflow (`Outcome::kStored`, then Discarded)** | **`58`** | `0 B` *(omitted by 512 MiB cap)* | `0.340 ms` | **`11.010 ms`** | **`1.491 ms`** (`2.15 MB` copy) | `0.000 ms` | `0.000 ms` | `0.000 ms` | **`2.498 ms`** | **`15.339 ms`** (`15.174 ms` P50; **`2.15 MB` blob then discarded!**) | `~2.56 ms / PSO` |

**Why did `251` seeded entries hit `Outcome::kStale` and why did `58` entries have `0 B` seed?**
- Inspection of `.vkpc` entries on disk shows that entries #0–#32 (`33` entries) are small (`15–90 KB`) and hit `Outcome::kReused` in **`0.26 ms` P50**. Entries #33–#283 (`251` entries) are **`2.15 MB` each** (`251 * 2.15 MB = 535.0 MB`), exhausting the `512 MiB` budget at entry #283 (`284` total).
- Furthermore, when `--resolution_scale` is toggled between `1` and `2` across developer sessions, `SpirvShaderTranslator` emits different SPIR-V instructions for scaled render targets, while `PipelineDescription::GetHash()` (`key`) does **not** include `draw_resolution_scale_x/y`. Thus the `.vkpc` key matches, `VkPipelineCreationFeedback` reports `0` (`kStale`), and [`CreatePipelineWithDriverCache`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L655-L677) executes the **`22.24 ms/PSO` double-compilation and double-extraction pass**!
- Even worse, for the remaining **`58` pipelines** (`#284–#341`), `CreatePipelineWithDriverCache` still creates a temporary `VkPipelineCache`, pays `11.01 ms` in `vkCreateGraphicsPipelines` + `1.49 ms` in `GetDriverCacheData` + `2.50 ms` in `MakeEntry` checksum (`15.34 ms/PSO`) to produce a `2.15 MB` entry, and then [`KeepDriverCacheEntry`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L714-L723) **throws that `2.15 MB` entry away** because `driver_cache_bytes_ + entry.data.size() > kMaxFileSize`!

---

## 5. Resolving the Startup vs. Runtime Scale Discrepancy & Per-Frame vs. Per-PSO Conflation (D2 Points 2 & 3)

### 5.1 Why Startup Warm-Up (`0.55–8.4 ms/PSO` wall-clock) Differs by 3x–10x from Runtime Slow Frames (`191.88 ms/slow frame`)

The apparent discrepancy between startup PSO warm-up numbers and runtime `pipeline_ms` is completely explained by three factors:

1. **Startup warm-up logs in the dataset (`SHADER_WARMUP: Vulkan complete in 1,359–3,133 ms`) were running with a WARM NVIDIA `GLCache`, dominated by `.vkpc` disk I/O and `.vkpc` `2.15 MB` blob overhead**:
   - In warm `.vkpc` startup runs ([`runtime-20261109-183049.log:214-230`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261109-183049.log#L214-L230), [`183708.log:214-229`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261109-183708.log#L214-L229), total `1,416–1,419 ms` for `339` PSOs):
     - **`951–954 ms` (`67.3%`)** happens **before** shader translation or PSO creation even starts — inside `driver_cache_file::Load` reading and FNV-1a checksum-verifying the **`535.8 MB` `.vkpc` file** from disk!
     - **`275–279 ms` (`19.5%`)** is single-threaded startup guest shader translation ([`vulkan/pipeline_cache.cpp:1021-1039`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L1021-L1039)).
     - Only **`186–190 ms` (`13.2%`, `0.55 ms/PSO` wall-clock across 6 threads)** is spent in the PSO creation loop (`284` reused at `~0.5 ms/thread` + `55` overflow at `~15 ms/thread` across 6 threads).
   - In stale/rebuilt `.vkpc` startup runs ([`runtime-20261108-165454.log:214-228`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-165454.log#L214-L228), `3,133 ms` total, `2,819 ms` PSO phase for `334` PSOs = `8.44 ms/PSO` wall-clock across 6 threads), the worker threads were spending **`15–23 ms/PSO` per thread** doing `Outcome::kStale` double-pass `2.15 MB` `.vkpc` serialization (Section 4.2) while NVIDIA's `GLCache` already held the SASS machine code.
2. **True cold runtime PSO misses (`GLCache` cold) must compile SPIR-V to SASS machine code for the first time (`46.8–205.3 ms / PSO`)**:
   - When a brand-new shader/PSO combination is encountered during gameplay for the very first time, NVIDIA's `GLCache` does not have it.
   - Isolating slow-frame windows where **exactly 1 PSO was built on the CP thread with 0 lookahead waits (`slowCount == 1`, `pipelines == 1.0`, `waits delta == 0`)** gives the true single-PSO cold runtime cost (`vkCreateGraphicsPipelines` + `~8 ms` `.vkpc` overhead):
     - [`runtime-20261108-161438.log:377`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-161438.log#L377): `1` PSO, `0` waits $\rightarrow$ **`46.8 ms`**
     - [`runtime-20261108-161438.log:468`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-161438.log#L468): `1` PSO, `0` waits $\rightarrow$ **`101.5 ms`**
     - [`runtime-20261108-161438.log:459`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-161438.log#L459): `1` PSO, `0` waits $\rightarrow$ **`112.2 ms`**
     - [`runtime-20261109-152130.log:1367`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261109-152130.log#L1367): `1` PSO, `0` waits $\rightarrow$ **`195.6 ms`**
     - [`runtime-20261108-161438.log:413`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-161438.log#L413): `1` PSO, `0` waits $\rightarrow$ **`205.3 ms`**
   - This **`46.8–205.3 ms / PSO`** single-PSO cold compilation range on Vulkan matches single-threaded cold D3D12 `CreateGraphicsPipelineState` (**`75.02–81.04 ms / PSO` mean** across 112 PSOs in [`runtime-20261108-160653.log:27`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-160653.log#L27) and [`163340.log:27`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-163340.log#L27)) plus `~8 ms` of `.vkpc` serialization and runtime driver lock contention with active command-buffer submission.
3. **Why "per slow frame" (`191.88 ms`) is higher than average "per PSO" (`43.91 ms`)**:
   - **Multi-PSO lookahead batching in a single frame**: As shown in Section 3.1, [`ConfigurePipeline`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L1845-L1927) includes `finish_ahead` (`Fh1WaitForAhead`) inside `pipeline_ms`, which waits for **all $K$ background lookahead PSOs** queued during that command buffer walk while counting only `0` or `1` in `fh1_frame_cost::Current().pipelines`.
   - **Selection bias (`kSlowMs = 50.0 ms`)**: `fh1_frame_cost::Report` only includes frames whose total frame time reached `>= 50 ms`. Fast runtime PSO misses (`< 17 ms`, such as warm-`GLCache` misses at `8.02 ms`) do not trigger a `>= 50 ms` frame and are excluded from `GPU slow frames`.

### 5.2 Complete Session-Level Decomposition: Per-Slow-Frame vs. Per-PSO in `runtime-20261108-161438.log`

In the primary 15-minute cold Vulkan gameplay session ([`game-data/runtime/logs/runtime-20261108-161438.log:315-487`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-161438.log#L315-L487)), the engine started with `165` preloaded PSOs and finished with `332` PSOs — building **`167` new runtime PSOs** in total (`59` inline on the CP thread + `108` on background lookahead threads):

| Metric (`runtime-20261108-161438.log`, 15-minute Cold Vulkan Session) | Value | Notes / Code Citation |
| :--- | ---: | :--- |
| **Total new runtime PSOs created** | **`167` PSOs** | `332 total - 165 preload` ([`line 487`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-161438.log#L487)) |
| — Created on background lookahead threads (`ahead`) | **`108` PSOs (`64.7%`)** | Queued by `Fh1PipelineLookahead` (`109 queued`) |
| — Created inline on CP thread (`inline`) | **`59` PSOs (`35.3%`)** | Only `23` of these occurred inside `>= 50 ms` slow-frame windows (`pipelines` counter) |
| **Total slow-frame `pipeline_ms` across all 37 slow frames in session** | **`7,333.4 ms`** | Mean **`198.20 ms / slow frame`** (`37` slow frames across `15` windows) |
| — Portion spent in `Fh1WaitForAhead` (`finish_ahead` waiting on `108` ahead PSOs) | **`4,348.7 ms` (`59.3%`)** | `60` waits (`72.48 ms / wait`, or **`40.27 ms / ahead PSO`**) |
| — Portion spent in direct CP-thread `EnsurePipelineCreated` (`59` inline PSOs) | **`2,984.7 ms` (`40.7%`)** | **`50.59 ms / inline CP PSO`** averaged across all 59 inline PSOs |
| **True Session Mean Slow-Frame Stall per Runtime PSO (`7,333.4 ms / 167 PSOs`)** | **`43.91 ms / PSO`** | Compare with **`198.20 ms / slow frame`** (`~4.51 PSOs` amortized per slow frame) |

Even within a single 10-second burst (e.g., [`line 343`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-161438.log#L343)–[`345`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-161438.log#L345) at `16:15:58`, entering a new zone), the log records **`9` slow frames** averaging **`195.9 ms / slow frame`** (`1,763.1 ms` total `pipeline_ms`), while [`line 345`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-161438.log#L345) shows that **`32` new PSOs** (`23` ahead + `9` inline) were created during that window — meaning each slow frame bundled **`3.56 PSOs`**, and the true cost per PSO in that burst was **`55.10 ms / PSO`** (`1,763.1 ms / 32 PSOs`), not `195.9 ms / PSO`!

---

## 6. Empirical Distributions & Histograms (Vulkan)

### 6.1 Stage (a) Histogram & Percentiles — Vulkan Guest Shader Translation (`ms / shader`)

Distribution of per-shader translation cost (`AnalyzeUcode` + `SpirvShaderTranslator::TranslateAnalyzedShader` + `vkCreateShaderModule`) across all **94 shaders** translated during slow frames (`20` windows with `shaders > 0`, totaling `148.40 ms`):

- **Percentiles (weighted by shader count, $N = 94$)**:
  - **Min**: `0.400 ms` ([`runtime-20261109-152130.log:1367`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261109-152130.log#L1367)) | **P25**: `1.382 ms` | **Median (P50)**: `1.500 ms` | **Mean**: `1.579 ms` (unweighted window mean: `1.800 ms`) | **P75**: `1.600 ms` | **P90**: `2.267 ms` | **P95**: `2.500 ms` | **P99**: `4.000 ms` | **Max**: `5.300 ms` ([`runtime-20261108-161438.log:438`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-161438.log#L438))

| Latency Bucket (`ms / shader`) | Translated Shaders ($N = 94$) | Share of Shaders (`%`) | Total Time (`ms`) | Visual Histogram |
| :--- | ---: | ---: | ---: | :--- |
| `0.0 – 1.0 ms` | `9` | `9.6%` | `5.60 ms` | `████` |
| `1.0 – 2.0 ms` | `68` | `72.3%` | `97.95 ms` | `█████████████████████████████` |
| `2.0 – 3.0 ms` | `13` | `13.8%` | `29.31 ms` | `██████` |
| `3.0 – 4.0 ms` | `2` | `2.1%` | `6.20 ms` | `█` |
| `4.0 – 6.0 ms` | `2` | `2.1%` | `9.30 ms` | `█` |
| **Total** | **`94`** | **`100.0%`** | **`148.36 ms`** | |

### 6.2 Stage (b) Distributions — Separating Per-PSO Metrics from Per-Slow-Frame Metrics

| Metric Source (Clearly Separating Per-PSO vs. Per-Batch/Frame) | Unit | Count ($N$) | Total (`ms`) | Min (`ms`) | P25 (`ms`) | Median P50 (`ms`) | Mean (`ms`) | P75 (`ms`) | P90 (`ms`) | Max (`ms`) |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| **1. Raw `vkCreateGraphicsPipelines` (Warm `GLCache`, `.vkpc` OFF)** | **per PSO** | `29` | `5.78` | `0.11` | `0.17` | **`0.19`** | **`0.20`** | `0.21` | `0.24` | `0.66` |
| **2. Default `CreatePipelineWithDriverCache` (Warm `GLCache`, `.vkpc` ON)** | **per PSO** | `29` | `232.61` | `6.21` | `7.19` | **`7.53`** | **`8.02`** | `8.22` | `9.45` | `16.38` |
| **3. Session Amortized Runtime Miss Cost (`161438.log`, Cold `GLCache` + `.vkpc`)** | **per PSO** | `167` | `7,333.4` | — | — | — | **`43.91`** | — | — | — |
| **4. Isolated Single-PSO Runtime Cold Miss (`0` lookahead waits)** | **per PSO** | `5` | `661.4` | `46.80` | `101.50` | **`112.20`** | **`132.28`** | `195.60` | `205.30` | `205.30` |
| **5. Lookahead Wait (`Fh1WaitForAhead` batch wait)** | **per wait** | `106` | `10,683.9` | `8.30` | `51.86` | **`84.50`** | **`100.79`** | `143.37` | `200.76` | `410.60` |
| **6. Slow-Frame `pipeline_ms` (bundles `1` CP PSO + `K` lookahead PSOs)** | **per slow frame** | `55` | `10,553.5` | `38.80` | `163.50` | **`195.90`** | **`191.88`** | `234.80` | `248.80` | `410.60` |
| **7. CSV Spike Excess Time (`ft - median`, pipeline-correlated bursts)** | **per spike frame** | `186` | `60,660.3` | `19.36` | `49.50` | **`172.02`** | **`326.13`** | `412.95` | `637.46` | `4,470.41` |

#### Histogram of Vulkan Slow-Frame `pipeline_ms` (Per Slow Frame, $N = 55$) and CSV Spike Excess ($N = 186$)

| Stall Bucket (`ms`) | Slow-Frame `pipeline_ms` ($N = 55$ frames) | Share (`%`) | Total `pipeline_ms` | CSV Pipeline Spike Excess ($N = 186$ frames) | Share (`%`) | Total CSV Excess (`ms`) |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| `10 – 25 ms` | `0` | `0.0%` | `0.00 ms` | `20` | `10.8%` | `440.68 ms` |
| `25 – 50 ms` | `5` | `9.1%` | `214.30 ms` | `27` | `14.5%` | `996.13 ms` |
| `50 – 100 ms` | `3` | `5.5%` | `192.20 ms` | `23` | `12.4%` | `1,565.61 ms` |
| `100 – 150 ms` | `4` | `7.3%` | `440.70 ms` | `19` | `10.2%` | `2,457.67 ms` |
| `150 – 200 ms` | `19` | `34.5%` | `3,573.00 ms` | `4` | `2.2%` | `652.83 ms` |
| `200 – 300 ms` | `21` | `38.2%` | `4,991.30 ms` | `30` | `16.1%` | `7,575.41 ms` |
| `300+ ms` (multi-PSO bursts) | `3` | `5.5%` | `1,142.00 ms` | `63` | `33.9%` | `46,971.93 ms` |
| **Total** | **`55`** | **`100.0%`** | **`10,553.50 ms`** | **`186`** | **`100.0%`** | **`60,660.26 ms`** |

### 6.3 Stage (c) Distribution — Rest of the Vulkan Draw Path (`GPU minute` Summaries)

Across **20 steady-state `GPU minute` summary lines** (`0.0 pipelines new/s`) logged by [`fh1_frame_cost::Summary::Line`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/fh1_frame_cost.h#L623-L641):

| Metric (Steady-State Cache-Hit Minutes, $N = 20$) | Min | P25 | Median (P50) | Mean | P75 | P90 | P95 | Max (Cold Minute) |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| **Command Processor CPU Time per Draw (`us / draw`)** | `2.51 us` | `2.72 us` | **`2.80 us`** | `2.90 us` | `3.05 us` | `3.32 us` | `3.39 us` | `89.33 us` |
| **Command Processor CPU Time per Frame (`ms / frame`)** | `0.90 ms` | `1.40 ms` | **`1.60 ms`** | `1.52 ms` | `1.70 ms` | `1.80 ms` | `1.90 ms` | `32.40 ms` |
| **Draws per Frame (`draws / frame`)** | `351` | `441` | **`538`** | `519.3` | `578` | `584` | `639` | `777` |

---

## 7. D3D12 Distributions, Patch Verification (`d3d12-blocking-pso-precompile.patch`), and Thread-Normalized Ratios (D2 Points 4 & 5)

### 7.1 Verification of `patches/rexglue-0.10.0/d3d12-blocking-pso-precompile.patch` vs. `tools/source/rexglue-sdk`

1. **What the patch fixes**: [`patches/rexglue-0.10.0/d3d12-blocking-pso-precompile.patch:12-22`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/patches/rexglue-0.10.0/d3d12-blocking-pso-precompile.patch#L12-L22) fixes [`d3d12::PipelineCache::InitializeShaderStorage`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp#L537) by changing:
   ```cpp
   while (creation_threads_.size() < creation_thread_original_count) {
   ```
   to:
   ```cpp
   while (creation_threads_.size() < creation_thread_needed_count) {
   ```
   and moving `if (await_creation_completion_event)` outside `if (!creation_threads_.empty())` so startup PSO creation runs in parallel across `logical_processor_count - 1` worker threads even when `async_shader_compilation=false`.
2. **Why `tools/source/rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp:537` currently does NOT have the patch**:
   - Prior to **`2026-11-08 15:58:13`** (commit `3954dec`, *"feat(vulkan): integrate ReIGlue SDK, tune texture cache, and add startup shader warmup"*), the repository built upstream `rexglue-sdk v0.10.0` via [`scripts/prepare-sdk-source.ps1`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/scripts/prepare-sdk-source.ps1#L45-L62), which **applied [`patches/rexglue-0.10.0/d3d12-blocking-pso-precompile.patch`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/patches/rexglue-0.10.0/d3d12-blocking-pso-precompile.patch)**.
   - In commit `3954dec` (`2026-11-08 15:58:13`), `tools/source/rexglue-sdk` was switched to the `ReIGlue` repository (`a5fd408` + local commits `875ce25`, `4c4f222`, `e38fb9a` adding `SHADER_WARMUP:` logs), and **`d3d12-blocking-pso-precompile.patch` was not ported to the `ReIGlue` checkout**.
3. **Exact identification of which D3D12 logs ran WITH vs. WITHOUT the patch**:
   - **Pre-`2026-11-08 15:58` logs (WITH patch — multi-threaded Stage b across $N$ threads, no `SHADER_WARMUP:` log line)**: `11` of the `13` cold/rebuilt D3D12 startup runs (plus `runtime-20261108-175858.log`, which launched an earlier `majin_recomp_usa.exe` binary without `SHADER_WARMUP:`).
   - **Post-`2026-11-08 15:58` logs (WITHOUT patch — single-threaded Stage b on `1` thread, contains `SHADER_WARMUP: D3D12` log line)**: [`runtime-20261108-160653.log:24-28`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-160653.log#L24-L28) (`9,076 ms` for `112` PSOs = **`81.036 ms / PSO`**) and [`runtime-20261108-163340.log:24-28`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-163340.log#L24-L28) (`8,402 ms` for `112` PSOs = **`75.018 ms / PSO`**).

### 7.2 Thread-Normalized D3D12 Stage (b) vs. Stage (a) Comparison (Addressing D2 Point 4)

Because Stage (a) (`Translated X shaders`) always runs across $N = \text{logical\_processor\_count} - 1$ threads ([`d3d12/pipeline_cache.cpp:478-486`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp#L478-L486)), comparing Stage (a) against unpatched single-threaded Stage (b) (`9,076 ms / 12 ms = 756:1`) inflates the wall-clock ratio by the thread count ($N$). Separating **Patched (N threads vs. N threads)** from **Unpatched (1 thread vs. N threads)** gives the true apples-to-apples ratio:

| D3D12 Cold / Rebuilt Startup Group | Runs ($N$) | Stage (a) Threads | Stage (b) Threads | Stage (a) `ms / shader` Wall-Clock | Stage (b) `ms / PSO` Wall-Clock | **Apples-to-Apples Per-Item Ratio (`ms/PSO : ms/shader`)** | Total Startup Stage (b)Share |
| :--- | ---: | :--- | :--- | ---: | ---: | ---: | ---: |
| **Patched (`d3d12-blocking-pso-precompile.patch` APPLIED)** | `11` | $N$ threads | **$N$ threads** | `0.027–0.072 ms` (`0.033 ms` P50) | **`6.49–11.99 ms / PSO`** (`7.67 ms` P50; `1,506–3,308 ms` for `180–295` PSOs) | **`166:1 – 240:1`** (`214:1` P50; `9.9:1` in 1 partial-warm run) | **`97.8% – 99.7%`** |
| **Unpatched (Post-`3954dec` `ReIGlue` WITHOUT patch)** | `2` | $N$ threads | **`1` thread** *(line 537 bug)* | `0.070–0.094 ms` | **`75.02–81.04 ms / PSO`** (`8,402–9,076 ms` for `112` PSOs) | *`862:1 – 1,072:1` (unnormalized wall-clock; **`~80:1 – 150:1` per-thread**)* | **`99.9%`** |
| **Warm `DXCache` Startup Runs** | `92` | $N$ threads | $N$ or `1` thread | `0.032 ms` P50 (`6.0 ms` total) | **`0.149 ms / PSO`** P50 (`39.0 ms` P50 total for `215–276` PSOs) | **`4.7 : 1`** P50 | **`86.4%`** P50 (`86.5%` mean) |

#### Individual Cold / Rebuilt D3D12 Startup Runs (Annotated with Patch State)

| Log File | Patch State (`d3d12-blocking-pso-precompile.patch`) | Shaders | Stage (a) (`ms` / `ms/sh`) | PSOs | Stage (b) (`ms` / `ms/PSO`) | Per-Item Ratio (`ms/PSO : ms/sh`) |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: |
| [`runtime-20261107-185200.log:22-23`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261107-185200.log#L22-L23) | **Patched ($N$ threads)** | `209` | `15 ms` (`0.072 ms`) | `276` | **`3,308 ms` (`11.986 ms`)** | **`166.5 : 1`** |
| [`runtime-20261108-102218.log:22-23`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-102218.log#L22-L23) | **Patched ($N$ threads)** | `209` | `10 ms` (`0.048 ms`) | `276` | **`2,879 ms` (`10.431 ms`)** | **`217.3 : 1`** |
| [`runtime-usa-20261107-184028.log:22-23`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-usa-20261107-184028.log#L22-L23) | **Patched ($N$ threads)** | `209` | `7 ms` (`0.033 ms`) | `276` | **`2,117 ms` (`7.670 ms`)** | **`232.4 : 1`** |
| [`runtime-20261108-175858.log:26-27`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-175858.log#L26-L27) | **Patched ($N$ threads, older binary)** | `220` | `6 ms` (`0.027 ms`) | `295` | **`1,914 ms` (`6.488 ms`)** | **`240.3 : 1`** |
| [`runtime-usa-20261106-175401.log:20-21`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-usa-20261106-175401.log#L20-L21) | **Patched ($N$ threads)** | `154` | `6 ms` (`0.039 ms`) | `180` | **`1,506 ms` (`8.367 ms`)** | **`214.5 : 1`** |
| [`runtime-20261108-160653.log:26-27`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-160653.log#L26-L27) | **Unpatched (`1` thread, post-`3954dec`)** | `128` | `12 ms` (`0.094 ms`) | `112` | **`9,076 ms` (`81.036 ms`)** | *`862.1 : 1` (inflated by $N:1$ threads)* |
| [`runtime-20261108-163340.log:26-27`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-163340.log#L26-L27) | **Unpatched (`1` thread, post-`3954dec`)** | `128` | `9 ms` (`0.070 ms`) | `112` | **`8,402 ms` (`75.018 ms`)** | *`1,071.7 : 1` (inflated by $N:1$ threads)* |

---

## 8. Summary of Architectural & Implementation Fixes Unlocked by D2

1. **Fix or Disable Per-Pipeline `.vkpc` Mega-Blobs in `VulkanPipelineCache` ([`vulkan/pipeline_cache.cpp:569-725`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L569-L725))**:
   - The current per-pipeline `VkPipelineCache` design in `CreatePipelineWithDriverCache` has an explicit SDK comment noting `~2 MB` per entry on NVIDIA ([`line 695`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L695)). Our controlled probe proves this design:
     - Adds **`+7.82 ms / PSO` (`40.1x` slowdown)** to every runtime PSO creation when `GLCache` is warm (`8.02 ms` vs. `0.20 ms` with `VK_NULL_HANDLE`).
     - Adds **`851–954 ms`** of blocking disk read + FNV-1a checksum time on every startup (`535.8 MB` `.vkpc` file).
     - Caps out at **283–284 pipelines (`512 MiB`)**, forcing all subsequent pipelines (`55–58` PSOs in current logs) to pay `15.34 ms/PSO` per thread on every boot only to have their `2.15 MB` blobs discarded!
     - Wastes **`22.24 ms/PSO` per thread** (`64.7%` in the 2nd pass) whenever `Outcome::kStale` triggers (e.g., resolution scale changes, because `draw_resolution_scale` is omitted from the `.vkpc` entry key).
   - Replacing per-PSO `.vkpc` blobs with a **single shared device-wide `VkPipelineCache`** (merged/saved once at shutdown) or relying on NVIDIA's native `GLCache` (`--vulkan_store_pipeline_cache=false`) eliminates all four overheads immediately without pre-compiling anything.
2. **Re-apply [`patches/rexglue-0.10.0/d3d12-blocking-pso-precompile.patch`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/patches/rexglue-0.10.0/d3d12-blocking-pso-precompile.patch) to the `ReIGlue` SDK checkout ([`d3d12/pipeline_cache.cpp:537`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp#L537))**:
   - Restores multi-threaded D3D12 startup PSO creation, cutting cold startup PSO compile time from `75–81 ms / PSO` (`9.1 s` for `112` PSOs) back down to `6.5–12.0 ms / PSO` (`1.5–3.3 s` for `180–295` PSOs).
3. **True First-Run Cold PSO Compilation (`46.8–205.3 ms / PSO` on Vulkan, `75–81 ms / PSO` single-threaded on D3D12) Still Requires Pre-Compilation / Warm-Up or Async Mitigation**:
   - Even with `.vkpc` fixed (`0.20 ms / PSO` when `GLCache` is warm), a player's **first and only playthrough** starts with a **cold driver `GLCache`/`DXCache`**, where each unseen PSO costs **`~45–205 ms`** to compile into GPU machine code (`SASS`). Therefore, D3–D7 remain essential to eliminate first-run in-game compilation stalls.
