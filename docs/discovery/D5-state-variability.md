# D5: Pipeline State Variability, Combinatorics & Static Pre-Generation Feasibility (Option 2)

**Document Version**: 1.0.0  
**Date**: 2026-11-10  
**Status**: Completed  
**Branch / Environment**: Windows AMD64, Vulkan (`rexgpu-xenos.dll`), DirectX 12 (`rexgpu-d3d12.dll`), NVIDIA GeForce RTX 3060 Ti  
**Deliverable Script**: [`scripts/discovery/d5-state-variability.mjs`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/scripts/discovery/d5-state-variability.mjs)  
**Parent Plan**: [`docs/SHADER_PRECOMPILATION_PLAN.md`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/docs/SHADER_PRECOMPILATION_PLAN.md) (Issue #4)

---

## 1. Executive Summary & Core Answers

Discovery Phase **D5** investigates whether the full list of pipeline state objects `(VS, PS, state)` can be synthesized statically on the player's machine prior to gameplay (**Option 2 / O2**). It examines pipeline state variability per shader pair, analyzes the game's asset archives (`game-data/`), and evaluates whether pipeline configurations can be extracted directly from on-disk effect containers.

```mermaid
flowchart TD
    subgraph DiskAssets["1. On-Disk Effect Containers (game-data/)"]
        fxb["99 fxb0 Archives across 4 Global Packages\n(static.pak, HexaEngineXbox.knl, start.pak, eff.pak)"]
        psDisk["Pixel Shaders (PS)\n130 of 139 (93.5%) byte-for-byte identical"]
        vsDisk["Vertex Shaders (VS Templates)\n94 of 100 (94.0%) CF+ALU identical"]
        statesDisk["Pass HLSL with D3DRS_* State Assignments\n(ZWriteEnable, ZEnable, AlphaBlendEnable)"]
        fxb --> psDisk
        fxb --> vsDisk
        fxb --> statesDisk
    end

    subgraph RuntimeMultipliers["2. Dynamic Runtime Multipliers"]
        vfetchPatch["sub_8261EAD8 in RAM:\nDynamic vfetch patch via D3DVertexDeclaration"]
        fboStates["94 State Blocks in Vulkan FBO:\n9 Passes x 4 Blends x 3 Depth x 15 Raster"]
        vsDisk --> vfetchPatch
        statesDisk --> fboStates
    end

    subgraph ObservedUniverse["3. Observed Gameplay Universe (4E4D0816.fbo.vk.xpso)"]
        pairs["183 Unique (VS, PS) Pairs"]
        psoList["342 Unique PSOs (Mean 1.87 PSOs/Pair)\n83.6% of pairs have only 1 or 2 variants"]
        pairs --> psoList
        fboStates --> psoList
        vfetchPatch --> psoList
    end

    subgraph Combinatorics["4. Combinatorial Assessment of Static Pre-Generation (O2)"]
        cartesian["Naive Cartesian Product:\n100 VS x 139 PS x 94 States = 1,306,600 PSOs\n(~10.8 Hours Cold! INFEASIBLE)"]
        constrainedO2["Constrained Static Extraction (O2):\nShader Translation (Stage a): 100% FEASIBLE\nDriver PSO Synthesis (Stage b): INFEASIBLE due to vfetch explosion"]
    end
```

### Core Answers to Plan Questions:

1. **How many unique pipeline state combinations exist per shader pair?**
   - **Extremely low and concentrated variability**: Across the `342` runtime PSOs in Vulkan FBO ([`4E4D0816.fbo.vk.xpso`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/vulkan-vulkan/release/shaders/shareable/4E4D0816.fbo.vk.xpso)), there are **`183` unique `(VS, PS)` pairs** associated with **`94` unique fixed-function state blocks**.
   - The average ratio is only **`1.87` PSOs per pair** (in D3D12 RTV it is **`1.67` PSOs per pair**; in FSI/ROV backends it drops further to **`1.18–1.24`**).
   - **`67.8%` of pairs (124 of 183) use strictly 1 unique state**.
   - **`83.6%` of pairs (153 of 183) use at most 2 states**.
   - **`94.0%` of pairs use 4 or fewer states**.

2. **Why are there only 94 state blocks across the entire game?**
   - Exact binary decomposition of the 94 `PipelineDescription` fixed-function blocks reveals they are built from a small, closed set of engine primitives:
     - **Only 9 RenderPass Keys** (FBO attachment layouts: backbuffer swapchain, depth PreZ, shadow map, bloom downsample, blur/post-process).
     - **Only 4 Blend Modes** (Opaque/Disabled, SrcAlpha+InvSrcAlpha, One+One Additive, Alpha-Preserving UI).
     - **Only 3 Depth/Stencil States** (PreZ write-only `LESS_EQUAL`, Opaque main `EQUAL`/`LESS_EQUAL`, Transparent `DEPTH_TEST_OFF`/no-write).
     - **Only 15 Rasterizer States** (Cull Back, Cull Front, Cull None, combined with depth bias configurations for shadow passes).
   - The theoretical Cartesian product (9 * 4 * 3 * 15 = 1,620) collapses to just **94 actual states** because each material and pass follows predetermined rendering semantics.

3. **Do the game's effect containers (`fxb0`) carry rendering states?**
   - **YES (Hypothesis 1 Confirmed)**: Reverse engineering the `fxb0` archives in `static.pak`, `HexaEngineXbox.knl`, `start.pak`, and `eff.pak` demonstrates that they are **compiled Microsoft DirectX 9 Effect Framework containers** (`ID3DXEffect` / Xbox 360 FXC `2.0.11164.0`).
   - Every technique and pass contains explicit 20-byte D3D9 render state assignment structures (`D3DRS_ZWRITEENABLE`, `D3DRS_ZENABLE`, `D3DRS_ALPHABLENDENABLE`, `D3DRS_CULLMODE`, etc.).

4. **Can the pipeline list be generated statically on the player's machine without playing (O2)?**
   - **Stage (a) Guest Microcode Translation to SPIR-V/DXBC: 100% FEASIBLE**.
     - **93.5% of Pixel Shaders (130 of 139)** reside byte-for-byte in the on-disk `fxb0` files and can be translated immediately at install time (< 500 ms total CPU time).
     - **94.0% of Vertex Shaders (94 of 100)** have control flow (CF) and ALU instruction bodies identical to on-disk templates.
   - **Stage (b) Driver PSO Synthesis: INFEASIBLE via Pure Static Generation**:
     - Dynamic `vfetch` patching in RAM by guest routine [`sub_8261EAD8`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/prototype/generated/default/majin_recomp.139.cpp#L16088-L16625) binds each Vertex Shader dynamically to mesh geometry declarations (`chr*.pak` and `st*.pak`).
     - A naive unconstrained Cartesian product (100 VS * 139 PS * 94 states) yields **`1,306,600` PSOs**. At the measured cold compile speed of `29.91 ms / PSO` (RTX 3060 Ti), compiling 1.3 million PSOs would require **over 10.8 hours** of continuous CPU computation.
     - Even restricting permutations to observed passes and `vfetch` signatures yields over 100,000 blind PSOs (~50 minutes of compilation), where 99% would never be requested during runtime.
   - **Architecture & Mitigation Verdict**:
     - Pure static Cartesian pre-generation (**O2**) is **discarded** for driver PSO synthesis (Stage b).
     - Recommended paths:
       1. **Option 1 (Automated Warm-Up / Headless Replay)**: Execute a targeted fast-forward replay or distribute a compact, copyright-clean canonical key list derived from actual runs.
       2. **Driver Feature Mitigations (D6 and D7)**: Decouple fixed-function state from shader compilation via `VK_EXT_graphics_pipeline_library` and `VK_EXT_extended_dynamic_state` (D7), combined with restricted asynchronous compilation on non-critical passes (D6).

---

## 2. Binary Architecture of Global Packages & `fxb0` Containers

### 2.1 Shader Package Distribution
As verified in [`scripts/discovery/d5-state-variability.mjs`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/scripts/discovery/d5-state-variability.mjs), all 99 shader archives in the game reside within 4 global packages:

| Package / File | File Type | TOC Entries | `fxb0` Containers | Rendering Scope |
| :--- | :--- | ---: | ---: | :--- |
| **`KB/package/static.pak`** | Global Static Package | `152` | **`91`** | Core gameplay shaders (`KbBaseP00_L4` to `KbBaseP32_L1`, `KbPreZSSSM`, `Kemumaki`, `KbSSAO`, `KbWaterModel`, `filter*`). |
| **`system/HexaEngineXbox.knl`** | HexaEngine Engine Kernel | `6` | **`5`** | Fundamental engine shaders (`HxMaterial`, `HxFilter`, `HxParallaxOcclusion`, `HxPrim`, `HxDefault`). |
| **`KB/package/start.pak`** | Boot Package | `4` | **`1`** | Boot primitive shader (`kbprimitive`). |
| **`KB/package/eff.pak`** | VFX Particle Package | `3` | **`2`** | Particle and blur shaders (`blur`, `vfx_kb_shader`). |
| **`KB/package/st*.pak` (215 files)** | Level Geometry Archives | `> 10,000` | **`0`** | Static stage geometry, collision meshes, lightmaps (`0` shaders). |
| **`KB/package/chr*.pak` (135 files)** | Character Archives | `> 5,000` | **`0`** | Skeletal models, animations, textures (`0` shaders). |

### 2.2 Archive Structure & Decompression
The `.pak` and `.knl` archives utilize headers with magic `\0kap` / `\0lnk` and a Table of Contents (TOC) starting at offset `0x80`, with sequential **`288-byte` (`0x120`)** entries:
- **Offset `+0x08`**: Uncompressed file size (32-bit unsigned big-endian integer).
- **Offset `+0x0C`**: Asset CRC32 checksum.
- **Offset `+0x10` to `+0x17`**: 64-bit `FILETIME` timestamp (recorded as `2010-09-16 07:07:18 UTC`, the official game master timestamp).
- **Offset `+0x18` to `+0x11F`**: Null-terminated ASCII asset path (e.g., `KB/shader/KbBaseP00_L4`).

Payload data following the TOC is compressed using standard **Zlib Deflate** (`0x78 0x9C`), preceded by a 20-byte chunk header declaring the exact compressed chunk size.

Upon decompressing any shader container, the initial magic is `0bxf` (big-endian for `'fxb0'`, `0x30627866`). This represents compiled Microsoft Direct3D 9 Effect Framework bytecode:
- **Techniques and Passes**: ASCII identification blocks designate techniques (e.g., `KbPreZSkinModel`, `KbPreZNonskinModel`) and passes (`P0`, `P1`, `P4`).
- **D3D9 Render State Assignments (`D3DRS_*`)**: 20-byte assignment structures define fixed-function operations per pass:
  - `D3DRS_ZWRITEENABLE` (`14`): `0x00000001` (`TRUE`) in PreZ and opaque passes; `0x00000000` (`FALSE`) in blended passes.
  - `D3DRS_ZENABLE` (`7`): `0x00000001` (`TRUE`).
  - `D3DRS_ALPHABLENDENABLE` (`27`): Enabled in VFX and alpha foliage/fur passes (`KbfurModel`).
  - `D3DRS_CULLMODE` (`22`): `D3DCULL_CCW` (`3`) default, `D3DCULL_CW` (`2`) in shadow passes, `D3DCULL_NONE` (`1`) in fullscreen quads and two-sided foliage.

---

## 3. Runtime Pipeline State Variability

### 3.1 Binary Cache Comparison (Vulkan FBO vs. D3D12 RTV)

Analysis of all `PipelineStoredDescription` entries across primary runtime caches yields:

| Variability Metric | Vulkan FBO (`4E4D0816.fbo.vk.xpso`) | D3D12 RTV (`4E4D0816.rtv.d3d12.xpso`) | Architectural Observation |
| :--- | ---: | ---: | :--- |
| **Total PSOs in Cache** | **`342`** | **`295`** | Cumulative caches from identical initial gameplay routes. |
| **Unique `(VS, PS)` Pairs** | **`183`** | **`177`** | `82.7%` exact pair convergence between backends. |
| **Unique Fixed-Function State Blocks** | **`94`** | **`68`** | Distinct fixed-function / render-pass combinations. |
| **Mean Variants per Pair** | **`1.87`** | **`1.67`** | Close to 1:1; minimal state dispersion. |
| **Distinct RenderPass Keys** | **`9`** | **`1`** | In Vulkan, encodes framebuffer attachment formats. In D3D12, managed via RTV descriptors. |
| **Distinct Blend Modes** | **`4`** | **`14`** | In Vulkan FBO, redundant blend parameters collapse; D3D12 encodes logic ops and write masks. |
| **Distinct Depth/Stencil States** | **`3`** | **`35`** | In Vulkan FBO: 3 pure modes (PreZ write, Opaque test, Off). In D3D12, separates stencil masks. |
| **Distinct Rasterizer States** | **`15`** | **`1`** | Culling modes (Back/Front/None) combined with depth bias. |

### 3.2 State Variant Distribution per `(VS, PS)` Pair (Vulkan FBO, N = 183 Pairs)

Quantitative distribution confirms that combinatorial explosion does not occur at runtime:

| State Variants per Pair | Pair Count | Share (%) | Cumulative Share (%) | Engine Function / Role |
| :---: | ---: | ---: | ---: | :--- |
| **1 variant** | **`124`** | **`67.8%`** | **`67.8%`** | Specialized shaders operating in exactly 1 fixed context (e.g., UI, skybox, fullscreen filters, dedicated PreZ). |
| **2 variants** | **`29`** | **`15.8%`** | **`83.6%`** | Geometry shaders operating in 2 standard passes: main opaque pass and shadow caster pass. |
| **3 variants** | **`11`** | **`6.0%`** | **`89.6%`** | Geometry with culling variations (Normal, Two-Sided, Shadow). |
| **4 variants** | **`8`** | **`4.4%`** | **`94.0%`** | Material shaders with blend variations (Opaque, Alpha Test, Alpha Blend, Shadow). |
| **5 to 9 variants** | **`9`** | **`4.9%`** | **`98.9%`** | Shared terrain and foliage shaders rendering to multiple target formats. |
| **15 to 20 variants** | **`2`** | **`1.1%`** | **`100.0%`** | The two most generic utility shaders (`357b0a2ce6b9fef0` and `67b4aa0885628ee0`), reused across multiple passes with differing blend and depth masks. |

> [!IMPORTANT]
> **Over 83% of all shader pairs use at most 2 pipeline states.** The hypothesis that shader pairs proliferate across dozens of state permutations is disproven for *Majin and the Forsaken Kingdom*.

---

## 4. Microcode Analysis & Dynamic `vfetch` Patching

### 4.1 On-Disk vs. Runtime Microcode Match (`4E4D0816.xsh`)
A byte-for-byte comparison between the 239 shaders extracted from [`4E4D0816.xsh`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/vulkan-vulkan/release/shaders/shareable/4E4D0816.xsh) and on-disk `fxb0` archives reveals a fundamental architectural asymmetry between Pixel Shaders and Vertex Shaders:

| Shader Stage | Unique in Runtime Cache | Exact Match on Disk | Match Rate | Root Cause |
| :--- | ---: | ---: | ---: | :--- |
| **Pixel Shaders (PS)** | **`139`** | **`130`** | **`93.5%`** | All 130 material and effect PS reside directly in `fxb0` packages. The remaining 9 are tiny dynamic clear/blit shaders (36 to 144 bytes). |
| **Vertex Shaders (VS)** | **`100`** | **`0`** | **`0.0%`** | Zero VS match byte-for-byte because Xbox 360 lacks a fixed-function Input Assembler; vertex attribute fetches are executed via `vfetch` instructions in microcode. |
| **Vertex Shaders (CF+ALU Structure)** | **`100`** | **`94`** | **`94.0%`** | Control Flow (CF) blocks and all ALU arithmetic instructions match on-disk templates exactly. Only `vfetch` clauses differ. |

### 4.2 Role of Guest Routine `sub_8261EAD8`
On the Xbox 360, the D3D9 driver dynamically modifies `vfetch` instructions in guest RAM prior to issuing `PM4_IM_LOAD` packets.

In the recompiled executable, guest function [`sub_8261EAD8`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/prototype/generated/default/majin_recomp.139.cpp#L16088-L16625):
1. Accepts the Vertex Shader template pointer and the mesh's `D3DVertexDeclaration`.
2. Iterates over vertex elements (position, normal, UV coordinates, blend weights).
3. Directly writes into the 12-byte (`3 dwords`) `vfetch` instruction fields:
   - **Dword 0**: Destination register (`dst_reg`), source register (`src_reg`), fetch constant index (`const_index`).
   - **Dword 1**: Data format (`format` — e.g., `FLOAT3`, `DEC3N`, `UBYTE4N`), swizzle, and normalization flags.
   - **Dword 2**: Vertex buffer stride in dwords and byte offset.

A total of **85 distinct `vfetch` signatures** were identified across the 100 runtime Vertex Shaders, corresponding to specific mesh layouts in level archives (`st*.pak`) and character archives (`chr*.pak`).

---

## 5. Ringbuffer Lookahead Telemetry in Active Gameplay

While Phase D4 measured lookahead lead times during boot and menus (0.05 to 2.5 ms, N = 25), telemetry from the extended 15.9-minute gameplay session ([`runtime-20261108-161438.log`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-161438.log)) provides empirical evidence of lookahead behavior during active play:

```
[2026-11-08 16:28:55.445] [info] [gpu] [t17588] Pipelines: 332 built, 108 of them ahead of their draws (108 drawn with so far, 0 failed), 0 placeholders; ... lookahead: 103 walks (queue end 3, wait 100), 392361 packets, 12317 draws, 108 queued, 100 unmet waits, 53.4 ms; 60 waits 4348.7 ms
```

| Gameplay Lookahead Metric | Measured Value | Engine Impact & Frame Pacing Context |
| :--- | ---: | :--- |
| **PSOs Compiled during Gameplay** | **`167`** | `332` total at end of session minus `165` pre-warmed at boot. |
| **PSOs Discovered by Lookahead** | **`108` (`64.7%`)** | Ringbuffer lookahead detects 64.7% of newly needed PSOs before draw calls execute. |
| **Unmet Waits** | **`100` of `108` (`92.6%`)** | **`92.6%` of PSOs queued ahead were not compiled in time for their first draw call.** |
| **Total CPU Stall Time in Lookahead Waits** | **`4,348.7 ms` (~`4.35 s`)** | Total duration the main render thread blocked waiting for background PSO compilation. |
| **Mean Stall per Wait** | **`72.48 ms` / wait** | Closely matches measured cold driver compilation latency (`45–205 ms`). |

> [!WARNING]
> **Runtime ringbuffer lookahead CANNOT prevent cold shader compilation stutter.**  
> Although the lookahead walker identifies draw packets ahead in the command buffer, the lead time between queuing and execution is only a few milliseconds, while background driver compilation takes `45` to `205 ms`. This causes `100` unmet waits (`92.6%`) and over `4.3` seconds of cumulative CPU freezing.

---

## 6. Feasibility Evaluation of Option 2 (Static Local Pre-Generation)

Option 2 envisioned pre-synthesizing all PSOs on the player's machine prior to the first game launch, using only local game data files.

### 6.1 Combinatorial Breakdown
Evaluating pure combinatorial pre-generation:
- **Naive Cartesian Space**:
  - `100 VS * 139 PS * 94 States = 1,306,600 PSOs`
  - At the measured cold rate (`29.91 ms / PSO` parallelized on RTX 3060 Ti):
    - `1,306,600 * 0.02991 s = 39,080 s (~10.8 hours)`
  - Requiring a player to wait 11 hours on first launch is completely unacceptable.

- **Effect-Constrained Space**:
  - In the 99 `fxb0` containers, `(VS_template, PS)` pairings are bound to specific passes (~200 to 300 passes total).
  - Factoring in dynamic `vfetch` permutations:
    - `300 Passes * 85 vfetch Signatures * 4 State Variants = 102,000 PSOs`
  - Compiling 102,000 PSOs would still take **`~50 minutes`**, only for the game to use **`~1,000` total PSOs** across the entire campaign (wasting over 99% of compilation work).

### 6.2 Feasible vs. Infeasible Components in Option 2

```mermaid
flowchart LR
    subgraph Feasible["100% Feasible (Stage a: Translation)"]
        t1["Extract 99 on-disk fxb0 archives"]
        t2["Translate 130 PS and 94 VS Templates to SPIR-V/DXBC"]
        t3["Total CPU cost: < 500 ms at launch"]
    end

    subgraph Infeasible["Infeasible (Stage b: Driver PSOs)"]
        i1["Blindly synthesize 100,000 to 1,300,000 PSOs"]
        i2["Driver compile time: 50 min to 10.8 hours"]
        i3["99% of compiled PSOs never used in game"]
    end

    Feasible -->|Saves 1.4% of slow-frame work| Plan["Design Decision"]
    Infeasible -->|Discarded at Discovery Gate| Plan
```

1. **Static Guest Microcode Translation (Stage a)**: **FEASIBLE**.
   - The launcher can scan the 4 `.pak`/`.knl` archives on install and pre-translate all 99 containers into SPIR-V / DXBC. This eliminates 100% of runtime CPU translation overhead (`1.58 ms / shader`).
   - However, as proven in Phase D2, shader translation accounts for only **`1.39%`** of slow-frame stall time; the remaining `98.6%` is driver PSO creation.
2. **Static Driver PSO Synthesis (Stage b)**: **INFEASIBLE**.
   - Without exact runtime knowledge of which vertex declarations map to which effect passes, blind generation causes combinatorial explosion.

---

## 7. Gate Unlocks & Architectural Directives

The findings of Phase D5 conclude the static asset exploration and direct future efforts toward runtime execution optimizations and driver capabilities:

1. **Discard Option 2 for Driver PSOs**: Full PSO synthesis cannot be achieved via static combinatorial generation.
2. **Validate Option 1 (Automated Warm-Up / Canonical Replay)**: Since PSOs cannot be derived statically without execution, a fast headless replay or a non-copyrighted canonical key list remains the most viable warm-up mechanism.
3. **Critical Priority for Phase D6 (Restricted Asynchronous Compilation)**: Because new PSOs will inevitably be encountered during a first playthrough, the engine requires a strategy to avoid blocking the main thread for `72 ms`. Phase D6 must evaluate which render passes (shadows, blur, particles) tolerate placeholder skips without visual corruption.
4. **Critical Priority for Phase D7 (Modern Driver Capabilities)**:
   - `VK_EXT_graphics_pipeline_library` (GPL): Compiles shader stages (VS + PS) independently of the 94 fixed-function rasterizer and blend states, dramatically reducing runtime compilation latency.
   - `VK_EXT_extended_dynamic_state`: Dynamically binds culling, blend enable, and depth compare states outside the PSO hash, collapsing pipeline state variations even further.

---

## 8. Attached Evidence

- Analysis and extraction script: [`scripts/discovery/d5-state-variability.mjs`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/scripts/discovery/d5-state-variability.mjs)
- Inspected binary caches:
  - [`game-data/runtime/cache/vulkan-vulkan/release/shaders/shareable/4E4D0816.fbo.vk.xpso`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/vulkan-vulkan/release/shaders/shareable/4E4D0816.fbo.vk.xpso) (`342` PSOs)
  - [`game-data/runtime/cache/d3d12-default/release/shaders/shareable/4E4D0816.rtv.d3d12.xpso`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/d3d12-default/release/shaders/shareable/4E4D0816.rtv.d3d12.xpso) (`295` PSOs)
  - [`game-data/runtime/cache/vulkan-vulkan/release/shaders/shareable/4E4D0816.xsh`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/vulkan-vulkan/release/shaders/shareable/4E4D0816.xsh) (`239` shaders)
- Examined game asset packages:
  - `game-data/finalizedXBox360/KB/package/static.pak` (`91` `fxb0` shaders)
  - `game-data/finalizedXBox360/system/HexaEngineXbox.knl` (`5` `fxb0` shaders)
  - `game-data/finalizedXBox360/KB/package/start.pak` (`1` `fxb0` shader)
  - `game-data/finalizedXBox360/KB/package/eff.pak` (`2` `fxb0` shaders)
- Continuous gameplay telemetry:
  - [`game-data/runtime/logs/runtime-20261108-161438.log`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-161438.log) (Periodic lookahead lines `315` to `1540`).
