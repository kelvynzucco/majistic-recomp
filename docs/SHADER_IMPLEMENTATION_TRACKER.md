# Shader Stutter Mitigation: Execution Tracker & Step-by-Step Progress Log

> **Target Title**: *Majin and the Forsaken Kingdom* (Title ID `4E4D0816`)  
> **Master Architecture Document**: [`docs/SHADER_IMPLEMENTATION_PLAN.md`](SHADER_IMPLEMENTATION_PLAN.md)  
> **Created**: 2026-11-11  
> **Status**: **READY TO EXECUTE PHASE A (MILESTONE M0)**  

---

## 1. Executive Status Dashboard

| Metric / Dimension | Current State | Notes |
| :--- | :--- | :--- |
| **Current Active Phase** | **Phase A (Preparation & Quick Wins)** | Ready to start Milestone M0. |
| **Current Active Milestone** | **`M0: Telemetry Integrity & CI Gate`** | Telemetry in SDK + CI automation script. |
| **Current Working Branch** | `main` | Next working branch: `feat/telemetry-and-gate` |
| **Overall Execution Progress** | **7% Completed (1 / 15 Subtasks)** | `analyze-performance-csv.mjs` ready and validated. |
| **GPL Spike Gate Status (M2a)** | **Pending Phase A completion** | Clean-room spike awaits completion of M0 and M1. |
| **Clean-Room Compliance** | **100% Verified** | Zero assets/manifests distributed; external harness kept clean-room in `tools/`. |

---

## 2. Milestone Execution Roadmap & Checklist

### Phase A: Telemetry & Immediate Gains (Estimate: 1–2 days)
- [ ] **M0: Telemetry & CI Gate (`feat/telemetry-and-gate`)**
  - [ ] **M0.1**: Fix attribution bug in `fh1_frame_cost.h` (`MainCause` blaming the game instead of `Fh1WaitForAhead`).
  - [ ] **M0.1b**: Separate diagnostics in CSV: `pipeline_compile_ms` (synchronous SASS) vs `pipeline_wait_ms` (thread stall in `Fh1WaitForAhead`).
  - [ ] **M0.2**: Enable `REXGLUE_ENABLE_PERF_COUNTERS` in Release-Diagnostic CMake configuration via `REXGLUE_FORCE_PERF_COUNTERS=ON`.
  - [x] **M0.3a**: Deliver standalone CLI analyzer `scripts/analyze-performance-csv.mjs` (support for `ms` column, frame time fallback, shader telemetry extraction, and multi-run aggregation).
  - [ ] **M0.3b**: Deliver automation harness `scripts/verify-shader-mitigation.ps1` with `-Cold`, `-Warm`, `-DurationSeconds`, and `-VerifyGate` modes.
  - [ ] **M0.4**: Recapture official baseline under controlled environment with performance counters enabled (`reports/baseline-shader/baseline-vulkan-cold-m0.json`).
- [ ] **M1: Monolithic Shared VkPipelineCache & D3D12 Multi-Thread (`feat/monolithic-vkpc-and-d3d12-parallel`)**
  - [ ] **M1.1a (Diagnostic)**: Validate immediate baseline with `--vulkan_store_pipeline_cache=false` (8.02 $\to$ 0.20 ms/PSO).
  - [ ] **M1.1b (Persistence)**: Implement shared monolithic `VkPipelineCache` (loaded at boot with `pipelineCacheUUID` validation, saved in atomic checkpoints every 10s and at `Shutdown()`).
  - [ ] **M1.2**: Reapply D3D12 parallel compilation patch (`d3d12-blocking-pso-precompile.patch`) fixing worker dispatch in `PipelineCache::InitializeShaderStorage()`.
  - [ ] **M1.3**: Execute automated verification (`verify-shader-mitigation.ps1`) and record metrics (`reports/baseline-shader/m1-vulkan-cold.json` and `m1-vulkan-warm.json`).

---

### Phase B: Immediate Priority — Extended Dynamic State (Estimate: 2–3 days)
- [ ] **M2.1: Extended Dynamic State (EDS 1, 2, 3) (`feat/vulkan-eds-and-gpl`)**
  - [ ] **M2.1a**: Implement EDS 1 & 2 in `IssueDraw` (culling, depth test/write, stencil) $\to$ Collapse to 28 states (-26.9% misses).
  - [ ] **M2.1b**: Implement EDS 3 via probing of specific feature flags (`extendedDynamicState3ColorBlendEnable`, `ColorBlendEquation`, `ColorWriteMask`) $\to$ Collapse to 9 states (-36.0% misses / -123 PSOs). On drivers with partial support, gracefully degrade to 18 or 28 states.
  - [ ] **M2.1c**: Increment cache schema version in `pipeline_cache.h` (`PipelineDescription::kVersion`) to safely invalidate legacy pre-EDS `.xpso` caches (`0x20260301`).
  - [ ] **M2.1d**: Validate in-game that 123 PSOs are eliminated without requiring GPL and without risk to the SPIR-V translator.
  - [ ] **M2.1e**: Reduce the PSO universe from 342 down to 219/233, preparing the realistic post-EDS scenario for the M2a spike.

---

### Phase B+ (Optional): CP CPU Optimizations (Estimate: 0.5–1 day)
- [ ] **M1.5: CrownParkComputing Patches (`feat/cp-cpu-optimizations`)**
  - [ ] **M1.5a**: 64-entry MRU cache in `GetCurrentStateDescription`.
  - [ ] **M1.5b**: 16-entry fast path in `UpdateBindings`.
  - [ ] **M1.5c**: Block copy via `memcpy` for sequential PM4 commands.
  - [ ] **M1.5d**: Validate no regression against warm baseline D8.

---

### Phase C1: Post-EDS Clean-Room Validation Spike (Estimate: 1–2 days | Blocking Gate)
- [ ] **M2a: Clean-Room GPL Validation Spike (`spike/gpl-cleanroom-validation`)**
  - [ ] **M2a.0**: Build standalone clean-room CLI harness in `tools/gpl-spike/` (`gpl_spike.exe`) with zero GPL-3 code in SDK.
  - [ ] **M2a.1**: Feed real post-EDS tuples from `4E4D0816.fbo.vk.xpso` to the harness.
  - [ ] **M2a.2 (Core Gate - Cold Cost)**: Measure cold library compilation latency on NVIDIA driver with purged GLCache.
  - [ ] **M2a.3 (Strict Gate Rule & Break-Even)**: Reject if $T_{\text{cold\_lib}} \ge 0.5 \times T_{\text{cold\_monolithic}}$ OR $P_{95}(\text{fast-link}) \ge 1.0\text{ ms}$ OR GPU Overhead $\ge 5.0\%$ (break-even point: 244 libs vs 219 PSOs $\approx 0.90\times$). If rejected $\to$ Formally abandon GPL (Contingency R1) and advance directly to M3 Async (saving 3–5 days).
  - [ ] **M2a.4 (Location Layout)**: Evaluate feasibility of `Location i = i` 1:1 mapping vs fixed `(vsHash, vsMod)` pairing.
  - [ ] **M2a.5**: Measure fast-link latency on RTX 3060 Ti driver (Threshold: $\ge 95\%$ in $< 1.0\text{ ms}$).
  - [ ] **M2a.6**: Measure GPU execution overhead (linked pipeline draw time vs monolithic $< 2\%$).
  - [ ] **M2a.7**: Validate 0 errors/warnings on Vulkan validation layers.
  - [ ] **GATE DECISION**: If approved $\to$ Advance to Phase C2 (GPL). If rejected $\to$ Advance directly to Phase D (M3 Async).

---

### Phase C2: Vulkan GPL Core & Fallback (Estimate: 3–5 days | Conditioned on M2a Gate)
- [ ] **M2.2/M2.3: 4-Library Architecture & Fallback (6 Entries: 5 Tiers + T2b) (`feat/vulkan-eds-and-gpl`)**
  - [ ] **M2.2**: Extension negotiation in `vulkan_device.cpp` and Fallback Matrix implementation (5 Tiers + sub-tier T2b).
  - [ ] **M2.3**: Implementation of the 4 libraries (`Vertex Input`, `Pre-Raster`, `Fragment Shader`, `Fragment Output`).
  - [ ] **M2.4**: Verification of instant fast-link at draw-call for new state variations of already compiled shaders.

---

### Phase D: Restricted Two-Layer Async (Estimate: 2–3 days)
- [ ] **M3: Safe Masking of Uncompiled Shaders (`feat/restricted-async`)**
  - [ ] **M3.1**: Audit of Category 6B `fxb0` symbols (isolate water, fog, and glass from pure additive particles).
  - [ ] **M3.2**: Implementation of Layer 1 (strict positive allowlist per PSO: Category 6A).
  - [ ] **M3.3**: Implementation of Layer 2 (guard in `IssueDraw` for $\le 6$ vertices protecting screen-aligned quads).
  - [ ] **M3.4**: Presentation Gate configuration (do not hold swapchain waiting for cosmetic particle effects).

---

### Phase E: Local Offline Cache & Polish (Backlog / Optional)
- [ ] **M4: Player Machine Local Tooling (`feat/local-session-cache`)**
  - [ ] **M4.1**: Export/import of local manifest from previous sessions (`.xpl` local to the player).
  - [ ] **M4.2**: Offline pre-translation of local shaders upon setup.
  - [ ] **M4.3**: Informative progress bar in launcher for hardware on Tier T5.

---

## 3. Performance Metrics Evolution Matrix (v5)

| Metric | Cold Baseline (D8) | Warm Baseline (D8 Ref) | Tier 1 Target (1st Cold Boot) | Tier 1 Target (Repeat Sessions) | Post-M0 | Post-M1 | Post-M2.1 (EDS) | Post-M2a | Post-M2 (GPL) | Post-M3 (Final) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **P99.9 (ms)** | `443.77` | `30.42` | **`< 50.0`** | **`< 33.33` (agg)** | - | - | - | - | - | - |
| **Stalls > 100 ms** | `22.3` | `0.0` | **`<= 5`** | **`0.0`** | - | - | - | - | - | - |
| **Stalls > 33.33 ms** | `34.7` | `7.0` | **`<= 12`** | **`<= 8` (session) / `<= 3` (gp)** | - | - | - | - | - | - |
| **Wasted Time (ms/1k)**| `1,477.8` | `14.9` | **`< 250.0`** | **`< 50.0`** | - | - | - | - | - | - |
| **D3D12 Boot (s)** | `~24.0` | `~3.5` | **`<= 3.5`** | **`<= 3.5`** | - | - | - | - | - | - |
| **Visual Integrity** | 100% | 100% | **0 artifacts** | **0 artifacts** | - | - | - | - | - | - |

### Intermediate Targets & Validation Criteria by Milestone (Gate Projections)
- **Post-M0 (Telemetry & CI)**: Controlled baseline recaptured with `.vkpc=false` and `PERF_COUNTERS=ON`; official thresholds calibrated in CI JSON.
- **Post-M1 (Bypass & Monolithic Cache)**: CPU overhead per PSO reduced from $8.02\text{ ms} \to 0.20\text{ ms}$; parallel D3D12 boot $\le 3.5\text{ s}$.
- **Post-M2.1 (EDS 1, 2, 3)**: **36.0% (123 misses)** reduction in PSOs; projected P99.9 $\approx 90\text{ ms}$; projected stalls $> 100\text{ ms} \approx 14$; bump to `kVersion = 0x20260301`.
- **Post-M2a (Gate Spike)**: Empirical mathematical validation: $\Sigma T_{\text{cold\_libs}} < 0.50 \times \Sigma T_{\text{cold\_mono}}$, Fast-link $P_{95} < 1.0\text{ ms}$, GPU overhead $< 2\%$, 0 errors.
- **Post-M3 (Final — Approved GPL Path)**: P99.9 $< 50.0\text{ ms}$; stalls $> 100\text{ ms} \le 5$; wasted time $< 250\text{ ms}/1\text{k frames}$.
- **Post-M3 (Final — Non-GPL Fallback if M2a Rejects)**: P99.9 $< 60.0\text{ ms}$; stalls $> 100\text{ ms} \le 8$; wasted time $< 450\text{ ms}/1\text{k frames}$.
- **Post-M4.1 (Repeat Sessions with Local Cache)**: P99.9 $< 33.33\text{ ms}$; **0 stalls $> 100\text{ ms}$**; wasted time $< 50\text{ ms}/1\text{k frames}$.

---

## 4. Step-by-Step Execution Journal

### Entry 001 — 2026-11-11: Technical Recalibration (Revision 5)
- **Phase / Task**: Architecture Recalibration, `Fh1WaitForAhead` Pinpointing, DoD Separation, and EDS Prioritization.
- **Branch**: `main`
- **Actions Completed**:
  - Inspected `tools/source/rexglue-sdk/src/graphics/pipeline/shader/spirv_translator.cpp:3040-3056`: confirmed interpolator locations are packed sequentially (`++input_location`). Reopened VS/PS coupling risk (DEC-04 updated).
  - Located `Fh1WaitForAhead` in `tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp:2010` (nested git repository).
  - Updated M1.1 to implement shared monolithic `VkPipelineCache` with checkpoints (`ContentCreateEx`, `flow_a`) and shutdown serialization (DEC-05), correcting assumptions about driver `pipelineCacheUUID`.
  - Restructured phase ordering: prioritized **M2.1 Extended Dynamic State (EDS 1, 2, 3)** as Phase B (DEC-06), eliminating 36% of PSOs (342 $\to$ 219) without compiler risk prior to running the M2a spike, including a bump in `PipelineDescription::kVersion` to invalidate legacy `.xpso` caches (DEC-09).
  - Established strict gate rule for M2a spike and calculated cold break-even point (244 cold libs vs 219 post-EDS PSOs $\approx 0.90\times$): if $T_{\text{cold\_lib}} \ge 0.5 \times T_{\text{cold\_monolithic}}$ OR link $\ge 1.0\text{ ms}$ OR overhead $\ge 5\%$, GPL is formally abandoned in favor of EDS + Async (DEC-10).
  - Recalibrated Definition of Done (DoD) dividing targets into **1st Cold Run** (conditioned on GPL approval: $\ge 83\%$ reduction, $\le 5$ stalls $> 100\text{ ms}$; or fallback without GPL: ~49% reduction, $\le 8$ stalls $> 100\text{ ms}$) vs **Repeat Sessions** (P99.9 $< 33.33\text{ ms}$, 0 stalls $> 100\text{ ms}$) (DEC-08).
  - Established strict clean-room directive for M2a harness (DEC-07) in `tools/gpl-spike/`, preserving clear separation between *Simpsons* GPL-3 license and SDK MIT/BSD license.
  - Completed sanitization of 307 absolute links across the repository.
- **Current Status**: Ready to create branch `feat/telemetry-and-gate` and begin **Milestone M0**.
- **Immediate Next Step**:
  - Create branch `feat/telemetry-and-gate`.
  - Apply telemetry attribution fixes in `tools/source/rexglue-sdk/src/graphics/fh1_frame_cost.h` (M0.1 and M0.1b).

---

## 5. Technical Decisions & Deviations Log

| ID | Date | Context / Decision | Technical Justification |
| :---: | :---: | :--- | :--- |
| **DEC-01** | 2026-11-11 | **Zero Distribution of `.xpl` / Manifests** | Preserve strict legal clean-room boundary; focus entirely on runtime mitigation via EDS + GPL + Async. |
| **DEC-02** | 2026-11-11 | **M2a Spike as a Blocking Gate** | Mandatory de-risking evaluating cold library creation cost vs monolithic ($T_{\text{cold\_lib}} \ge 0.5 \times T_{\text{monolithic}}$) before 3–5 days of engine refactoring. |
| **DEC-03** | 2026-11-11 | **Simpsons Architectural Pattern for M2a** | 40–50% reduction in spike structuring effort by referencing headless test harness patterns. |
| **DEC-04** | 2026-11-11 | **Reopening of VS/PS Coupling Risk (Locations)** | `IndependentInterpolationDecoration` only ignores qualifiers, not sequential location packing in `spirv_translator.cpp`. M2a must evaluate feasibility of `Location i = i`. |
| **DEC-05** | 2026-11-11 | **Adoption of Monolithic VkPipelineCache with Checkpoints in M1.1** | Pure bypass (`--vulkan_store_pipeline_cache=false`) serves as a diagnostic, but a shared monolithic cache with checkpoints (`ContentCreateEx`, `flow_a`) preserves local persistence across Windows GLCache purges and prevents crash data loss. Drivers invalidate UUID, but local session consistency is guaranteed. |
| **DEC-06** | 2026-11-11 | **Prioritization of EDS 1, 2, 3 (M2.1) Before M2a Spike** | Collapses 94 fixed states down to 9 render passes (-36.0% / 123 PSOs) with low effort and zero VS/PS interface risk, enabling the M2a spike to evaluate the realistic post-EDS universe (219 PSOs). |
| **DEC-07** | 2026-11-11 | **Clean-Room License Separation for Harness** | *TheSimpsonsGameRecomp* is GPL-3.0 and `rexglue-sdk` is MIT/BSD; the M2a harness will be implemented from scratch in `tools/gpl-spike/` as an isolated tool. |
| **DEC-08** | 2026-11-11 | **Separation of Definition of Done (DoD) into Cold vs Repeat** | Unseen opaque materials incur unavoidable cold SASS compilation on 1st boot. Separate targets between 1st run with GPL ($\ge 83\%$ reduction, $\le 5$ stalls $> 100\text{ ms}$), fallback without GPL (~49% reduction, $\le 8$ stalls $> 100\text{ ms}$), and repeat sessions (P99.9 $< 33.33\text{ ms}$, 0 stalls $> 100\text{ ms}$). |
| **DEC-09** | 2026-11-11 | **Cache Version Bump for `.xpso` on EDS (M2.1)** | Dynamic state transitions (blend, depth, cull) change the layout and hashing of `PipelineDescription`. Incrementing `PipelineDescription::kVersion` forces clean invalidation of incompatible legacy `.xpso` files. |
| **DEC-10** | 2026-11-11 | **Strict M2a Gate Rule & Cold Break-Even Point** | In post-EDS 1st boot, GPL compiles 244 cold libraries vs 219 monolithic PSOs (+11.4%). Compile time break-even point requires $T_{\text{cold\_lib}} \le 0.90 \times T_{\text{cold\_monolithic}}$. Strict failure rule with zero loopholes if $T_{\text{lib}} \ge 0.5 \cdot T_{\text{mono}}$ OR link $\ge 1\text{ ms}$ OR GPU overhead $\ge 5\%$. |
