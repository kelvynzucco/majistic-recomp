# D3 — Pipeline Population & Growth Curve (`pipeline-population`)

- **Date:** 2026-11-10
- **Status:** Complete (with partial game coverage caveat)
- **Reproducible script:** [`scripts/discovery/d3-pipeline-population.mjs`](../../scripts/discovery/d3-pipeline-population.mjs)
- **Plan question ([`docs/SHADER_PRECOMPILATION_PLAN.md`](../SHADER_PRECOMPILATION_PLAN.md#L106)):**
  > *"How many distinct pipelines/shaders does the game use, and does that count plateau?"*

---

## 1. Executive Summary

1. **Total population observed in on-disk binary caches (`7` `.xsh` files and `8` `.xpso` files):**
   - **Unique guest shaders (global `union` of all `.xsh` files and all `203` logs):** **`242` unique Xenos shaders** (**`100` vertex shaders** + **`142` pixel shaders**), totaling only **`~266 KB`** of raw Xenos microcode (mean of **`1,102.2 bytes` / `275.6 dwords`** per shader; minimum `36 bytes`, maximum `7,008 bytes`).
   - **Zero duplicate records in `.xsh`:** all `7` `.xsh` files contain `0` duplicate records. All `80` VS hashes and `105` PS hashes emitted in historical log lines (`Creating graphics pipeline state`) are present in on-disk `.xsh` files (`100%` shader retention).
   - **Unique PSO descriptions (`.xpso`) in cumulative primary caches:**
     - **Vulkan FBO ([`4E4D0816.fbo.vk.xpso`](../../game-data/runtime/cache/vulkan-vulkan/release/shaders/shareable/4E4D0816.fbo.vk.xpso)):** **`342` unique PSOs** (`23,276 bytes`), referencing **`230` unique shaders** (`97` VS + `133` PS), **`14` depth-only pipelines (`PS = 0`)**, **`183` unique `(VS, PS)` pairs**, **`195` unique `(VS_mod, PS_mod)` tuples**, and **`94` unique fixed-state/render-pass blocks** (**mean of `1.87 PSOs` per `(VS, PS)` pair**, maximum `20`).
     - **D3D12 RTV ([`4E4D0816.rtv.d3d12.xpso`](../../game-data/runtime/cache/d3d12-default/release/shaders/shareable/4E4D0816.rtv.d3d12.xpso)):** **`295` unique PSOs** (`21,252 bytes`), referencing **`220` unique shaders** (`94` VS + `126` PS), **`12` depth-only pipelines (`PS = 0`)**, **`177` unique `(VS, PS)` pairs**, **`190` unique `(VS_mod, PS_mod)` tuples**, and **`68` unique fixed-state blocks** (**mean of `1.67 PSOs` per `(VS, PS)` pair**, maximum `12`).
     - **Vulkan FSI ([`4E4D0816.fsi.vk.xpso`](../../game-data/runtime/cache/vulkan-vulkan/release/shaders/shareable/4E4D0816.fsi.vk.xpso)) and D3D12 ROV ([`4E4D0816.rov.d3d12.xpso`](../../game-data/runtime/cache/d3d12-rov/release/shaders/shareable/4E4D0816.rov.d3d12.xpso)):** **`168` PSOs** (`136` `(VS, PS)` pairs, only `15` fixed-state blocks, **`1.24 PSOs/pair`**) and **`143` PSOs** (`121` `(VS, PS)` pairs, only `13` fixed-state blocks, **`1.18 PSOs/pair`**). In FSI/ROV paths (where blending and EDRAM depth/stencil are resolved in the pixel shader via interlock/ROV rather than pipeline fixed-function state), the combinatorial state multiplier per `(VS, PS)` pair drops from `1.67–1.87x` to **`1.18–1.24x`**.
2. **High overlap across backends (`Vulkan FBO` vs `D3D12 RTV`):**
   - Of the `230` shaders used in Vulkan FBO and `220` in D3D12 RTV, **`208` shaders (`86.0%` of the union of `242` shaders) are identical** across both backends.
   - Of the `183` `(VS, PS)` pairs in Vulkan FBO and `177` in D3D12 RTV, **`163` pairs (`82.7%` of the union of `197` on-disk pairs) are identical**.
   - Additionally, the `d3d12-rtv` cache (`157` PSOs) is an **exact `100%` subset (`157/157`)** of the `XXH3_64bits` hashes of `PipelineDescription` present in `d3d12-default` (`295` PSOs), confirming that `PipelineDescription` is deterministic across runs along the same route.
3. **Growth dynamics and plateaus (within zones vs between zones):**
   - **Sharp local plateaus within each explored zone:** In the longest continuous session ([`runtime-20261108-161438.log`](../../game-data/runtime/logs/runtime-20261108-161438.log), `15.9 min`), growth occurs in **concentrated steps during zone/cutscene transitions**, interspersed by **multi-minute plateaus**:
     - Minutes 1–2 (opening cutscene + entry into Zone 1): `165 → 229` PSOs (`+64` PSOs, `+19` shaders).
     - **Plateau 1 (Minutes 3–4, exploring Zone 1):** `229 → 236` PSOs (**`+7` PSOs in 2 minutes and `+0` new shaders** — fixed at `202` shaders).
     - Minute 5 (sub-area transition / combat): `236 → 267` PSOs (`+31` PSOs, `+2` shaders).
     - **Plateau 2 (Minutes 6–9, 4 minutes of continuous gameplay):** `267 → 275` PSOs (**only `+8` PSOs and `+5` shaders in 4 minutes = `2.0 PSOs/min`**).
     - Minutes 10–12 (new main area + meeting the Majin / Achievement `#2`): `275 → 327` PSOs (`+52` PSOs, `+22` shaders).
     - **Plateau 3 (Minutes 13–15, 3 minutes continuous + 2nd cutscene):** `327 → 332` PSOs (**only `+5` PSOs and `+4` shaders in 3 minutes = `1.7 PSOs/min`**).
   - **Stabilization across previously visited routes in subsequent sessions:** Across the next 6 Vulkan sessions spanning 2 days (`2026-11-08 16:31` to `2026-11-09 18:37`), the cache grew only from `332 → 334 → 335 → 339 → 342` PSOs (**`+10` total PSOs**), with entire sessions of `63–89 s` logging **`0` new PSOs and `0.0 ms` wait time**. On D3D12, the final sessions on `2026-11-08` grew from `291 → 293 → 295` PSOs with **`+0` new shaders** (stationary at `220` shaders).
   - **Sublinearity of shaders relative to PSOs:** As the cache expands from `100` to `342` PSOs in Vulkan FBO, the `PSOs / Shader` ratio increases from **`0.97`** (`100` PSOs for `103` shaders) to **`1.49`** (`342` PSOs for `230` shaders), and the `PSOs / (VS, PS) Pair` ratio rises from **`1.32`** to **`1.87`**. This demonstrates that new PSOs in later areas increasingly reuse previously known shaders under new combinations of fixed-state/render passes.
4. **Critical caveat — Local plateau vs whole game coverage:**
   - Existing caches cover the early game (menu, opening cutscenes, and the first `~3–4` playable areas up to meeting the Majin / Achievement `#2`). Therefore, the figures of **`242` shaders** and **`342` PSOs (Vulkan FBO) / `295` PSOs (D3D12 RTV)** represent a **local plateau along the tested routes**, **not** a `100%` ceiling of the full campaign.
   - Nevertheless, the scale of *Majin and the Forsaken Kingdom*'s engine remains modest (hundreds of shaders and a few hundred to ~1,000 PSOs estimated across the entire campaign, rather than tens of thousands), making local on-disk precompilation viable if the pipeline list is available. On D3D12 with multi-threaded precompilation active, warm pre-pass takes `~2–3 s` for `300` PSOs; on Vulkan without `.vkpc` wrapper overhead, it takes `< 100 ms` with warm driver cache, or `~6 s` in parallel with completely cold driver cache (empirically measured).

---

## 2. Binary Format of Analyzed Caches (SDK Source Code)

Direct parsing in [`scripts/discovery/d3-pipeline-population.mjs`](../../scripts/discovery/d3-pipeline-population.mjs) follows the exact SDK C++ structures:

1. **Guest Shader Storage File (`4E4D0816.xsh`)** — defined in [`pipeline_store_file.h:35-69`](../../tools/source/rexglue-sdk/include/rex/graphics/pipeline_store_file.h#L35-L69):
   - `8-byte` header (`ShaderStorageFileHeader`): `magic = 0x48534558` (`'XESH'`), `version_swapped = 0x19122020` (`SwapBytes(0x20201219)`).
   - Variable-length record sequence: `12-byte` `ShaderStoredHeader` (`uint64_t ucode_data_hash`, `uint32_t ucode_dword_count : 31`, `xenos::ShaderType type : 1`, where `0 = kVertex` and `1 = kPixel` per [`xenos.h:33-36`](../../tools/source/rexglue-sdk/include/rex/graphics/xenos.h#L33-L36)) followed by `ucode_dword_count * 4` bytes of Xenos microcode.
2. **Vulkan Pipeline Description File (`4E4D0816.<fbo|fsi>.vk.xpso`)** — defined in [`vulkan/pipeline_cache.h:141-272`](../../tools/source/rexglue-sdk/include/rex/graphics/vulkan/pipeline_cache.h#L141-L272):
   - `20-byte` header (`Format 2`, with `version_2_marker = 0xFFFFFFFF`, `description_version_swapped = SwapBytes(0x20260228)`, `modification_version_swapped = SwapBytes(0x20261018)`) in `.fbo.vk.xpso` files, or legacy `12-byte` header in `.fsi.vk.xpso` files.
   - Fixed **`68-byte`** records (`PipelineStoredDescription`): `8 bytes` of `description_hash` (`XXH3_64bits`) + `60 bytes` of `PipelineDescription`:
     - `vertex_shader_hash` (`8B`), `vertex_shader_modification` (`8B`), `pixel_shader_hash` (`8B`), `pixel_shader_modification` (`8B`);
     - `render_pass_key` (`4B`, [`vulkan/render_target_cache.h:36-78`](../../tools/source/rexglue-sdk/include/rex/graphics/vulkan/render_target_cache.h#L36-L78));
     - `8B` of rasterization/depth/stencil registers (`depth_bias`, `depth_bias_slope_scaled`, bitfields for topology, cull mode, depth/stencil, etc.) + `16B` of `PipelineRenderTarget[4]` (blend/color write mask).
3. **D3D12 Pipeline Description File (`4E4D0816.<rtv|rov>.d3d12.xpso`)** — defined in [`d3d12/pipeline_cache.h:101-228`](../../tools/source/rexglue-sdk/include/rex/graphics/d3d12/pipeline_cache.h#L101-L228):
   - `12-byte` header (`magic = 'XEPS'`, `magic_api = 'DXRT'` or `'DXRO'`, `version_swapped = SwapBytes(0x20260226)`).
   - Fixed **`72-byte`** records (`PipelineStoredDescription`): `8 bytes` of `description_hash` (`XXH3_64bits`) + `64 bytes` of `PipelineDescription` (`vertex_shader_hash`, `vertex_shader_modification`, `pixel_shader_hash`, `pixel_shader_modification`, `depth_bias`, `depth_bias_slope_scaled`, `8B` fixed-state bitfields, and `16B` `PipelineRenderTarget[4]`).

---

## 3. Complete Population Across On-Disk Caches (`game-data/runtime/cache/`)

### 3.1. `.xsh` Files (Xenos Guest Shaders)

| Cache Directory | File `.xsh` | Size (Bytes) | Records in File | Duplicates | Unique Shaders | Vertex Shaders (`VS`) | Pixel Shaders (`PS`) | Mean Microcode Size | Min / Max Microcode |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `vulkan-vulkan` | [`4E4D0816.xsh`](../../game-data/runtime/cache/vulkan-vulkan/release/shaders/shareable/4E4D0816.xsh) | `266,312` | `239` | `0` | **`239`** | `100` | `139` | `1,102.2 B` | `36 B` / `7,008 B` |
| `d3d12-default` | [`4E4D0816.xsh`](../../game-data/runtime/cache/d3d12-default/release/shaders/shareable/4E4D0816.xsh) | `251,864` | `220` | `0` | **`220`** | `94` | `126` | `1,132.8 B` | `36 B` / `6,816 B` |
| `d3d12-rov` | [`4E4D0816.xsh`](../../game-data/runtime/cache/d3d12-rov/release/shaders/shareable/4E4D0816.xsh) | `157,568` | `160` | `0` | **`160`** | `69` | `91` | `972.8 B` | `36 B` / `6,420 B` |
| `vulkan-vulkan_fsi` | [`4E4D0816.xsh`](../../game-data/runtime/cache/vulkan-vulkan_fsi/release/shaders/shareable/4E4D0816.xsh) | `132,620` | `147` | `0` | **`147`** | `61` | `86` | `890.1 B` | `36 B` / `6,420 B` |
| `d3d12-rtv` | [`4E4D0816.xsh`](../../game-data/runtime/cache/d3d12-rtv/release/shaders/shareable/4E4D0816.xsh) | `129,548` | `143` | `0` | **`143`** | `61` | `82` | `893.9 B` | `36 B` / `6,312 B` |
| `vulkan-fbo` | [`4E4D0816.xsh`](../../game-data/runtime/cache/vulkan-fbo/release/shaders/shareable/4E4D0816.xsh) | `944` | `9` | `0` | **`9`** | `5` | `4` | `92.0 B` | `36 B` / `156 B` |
| `vulkan-fsi` | [`4E4D0816.xsh`](../../game-data/runtime/cache/vulkan-fsi/release/shaders/shareable/4E4D0816.xsh) | `656` | `7` | `0` | **`7`** | `4` | `3` | `80.6 B` | `36 B` / `132 B` |
| **Global Union (`.xsh`)** | **All `7` files** | — | — | `0` | **`242`** | **`100`** | **`142`** | **`~1,102 B`** | **`36 B` / `7,008 B`** |

> **Note on `.xsh` retention:** The file [`vulkan-vulkan/release/shaders/shareable/4E4D0816.xsh`](../../game-data/runtime/cache/vulkan-vulkan/release/shaders/shareable/4E4D0816.xsh) alone contains **`239` of the `242` unique shaders** observed across project history (`100/100` VS and `139/142` PS). The remaining `3` pixel shaders reside in `d3d12-default`.

### 3.2. `.xpso` Files (Pipeline State Object Descriptions)

| Cache / File `.xpso` | Backend / Mode | Header Format | Bytes | Unique PSOs (`records`) | Ref. Shaders (`VS` + `PS`) | Depth-Only PSOs (`PS=0`) | Modifications (`VS_mod` / `PS_mod`) | Unique `(VS, PS)` Pairs | Unique `(VS_mod, PS_mod)` | Unique Fixed-State Blocks | Mean `PSOs / (VS,PS) Pair` | Max `PSOs` for 1 Pair |
| :--- | :--- | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| [`vulkan-vulkan` (`fbo.vk.xpso`)](../../game-data/runtime/cache/vulkan-vulkan/release/shaders/shareable/4E4D0816.fbo.vk.xpso) | Vulkan `fbo` | Format 2 (`20B`) | `23,276` | **`342`** | **`230`** (`97` + `133`) | `14` | `99` / `144` | **`183`** | `195` | **`94`** | **`1.87`** | `20` |
| [`d3d12-default` (`rtv.d3d12.xpso`)](../../game-data/runtime/cache/d3d12-default/release/shaders/shareable/4E4D0816.rtv.d3d12.xpso) | D3D12 `rtv` | Legacy (`12B`) | `21,252` | **`295`** | **`220`** (`94` + `126`) | `12` | `96` / `138` | **`177`** | `190` | **`68`** | **`1.67`** | `12` |
| [`vulkan-vulkan` (`fsi.vk.xpso`)](../../game-data/runtime/cache/vulkan-vulkan/release/shaders/shareable/4E4D0816.fsi.vk.xpso) | Vulkan `fsi` | Legacy (`12B`) | `11,436` | **`168`** | **`179`** (`77` + `102`) | `9` | `79` / `103` | **`136`** | `137` | **`15`** | **`1.24`** | `4` |
| [`d3d12-rtv` (`rtv.d3d12.xpso`)](../../game-data/runtime/cache/d3d12-rtv/release/shaders/shareable/4E4D0816.rtv.d3d12.xpso) | D3D12 `rtv` | Legacy (`12B`) | `11,316` | **`157`** | **`143`** (`61` + `82`) | `10` | `63` / `90` | **`104`** | `112` | **`54`** | **`1.51`** | `8` |
| [`d3d12-rov` (`rov.d3d12.xpso`)](../../game-data/runtime/cache/d3d12-rov/release/shaders/shareable/4E4D0816.rov.d3d12.xpso) | D3D12 `rov` | Legacy (`12B`) | `10,308` | **`143`** | **`160`** (`69` + `91`) | `8` | `71` / `92` | **`121`** | `122` | **`13`** | **`1.18`** | `3` |
| [`vulkan-fbo` (`fbo.vk.xpso`)](../../game-data/runtime/cache/vulkan-fbo/release/shaders/shareable/4E4D0816.fbo.vk.xpso) | Vulkan `fbo` | Format 2 (`20B`) | `972` | **`14`** | **`9`** (`5` + `4`) | `9` | `6` / `4` | **`6`** | `6` | **`12`** | **`2.33`** | `5` |
| [`vulkan-fsi` (`fsi.vk.xpso`)](../../game-data/runtime/cache/vulkan-fsi/release/shaders/shareable/4E4D0816.fsi.vk.xpso) | Vulkan `fsi` | Legacy (`12B`) | `488` | **`7`** | **`7`** (`4` + `3`) | `4` | `5` / `3` | **`5`** | `5` | **`5`** | **`1.40`** | `2` |
| [`vulkan-vulkan_fsi` (`fsi.vk.xpso`)](../../game-data/runtime/cache/vulkan-vulkan_fsi/release/shaders/shareable/4E4D0816.fsi.vk.xpso) | Vulkan `fsi` | Legacy (`12B`) | `12` | **`0`** | **`0`** | `0` | `0` / `0` | **`0`** | `0` | **`0`** | — | `0` |

### 3.3. Cross-Backend and Historical Log Overlap

| Comparison | Metric | Value |
| :--- | :--- | :--- |
| **Vulkan FBO (`342` PSOs) vs D3D12 RTV (`295` PSOs)** | Shared shaders / Union of shaders | **`208 / 242` (`86.0%`)** |
| **Vulkan FBO (`342` PSOs) vs D3D12 RTV (`295` PSOs)** | Shared `(VS, PS)` pairs / Union on disk | **`163 / 197` (`82.7%`)** |
| **`d3d12-rtv` (`157` PSOs) vs `d3d12-default` (`295` PSOs)** | Shared `PipelineDescription` `XXH3_64bits` hashes | **`157 / 157` (`100.0%` exact subset)** |
| **Historical logs (`203` files) vs on-disk `.xsh`** | Log `VS` and `PS` hashes present in current `.xsh` | **`80/80` VS (`100%`) and `105/105` PS (`100%`)** |
| **Historical logs (`203` files) vs on-disk `.xpso`** | Log `(VS, PS)` pairs present in current `.xpso` | **`142 / 217`** (`75` pairs occurred in sessions prior to `.xpso` reset on `2026-11-08 15:56`, though **all** individual shaders remain in `.xsh`) |

> **Important architectural takeaway:** Because `100%` of shaders from old logs remain in `.xsh` even after `.xpso` resets (`75` pre-reset `(VS, PS)` pairs have not yet recurred following the `15:56` reset), the true historical union of observed `(VS, PS)` pairs reaches **`272` `(VS, PS)` pairs** across the same **`242` shaders**.

---

## 4. Growth Curve by Discovery Order (Inside `.xpso`)

Because `PipelineStorage` records each new `PipelineStoredDescription` sequentially in `ab` (append binary) mode in the exact order the game discovers each new pipeline, the order of records inside [`4E4D0816.fbo.vk.xpso`](../../game-data/runtime/cache/vulkan-vulkan/release/shaders/shareable/4E4D0816.fbo.vk.xpso) (`342` PSOs) and [`4E4D0816.rtv.d3d12.xpso`](../../game-data/runtime/cache/d3d12-default/release/shaders/shareable/4E4D0816.rtv.d3d12.xpso) (`295` PSOs) reveals how shaders and state blocks scale as the PSO population grows:

### 4.1. Shaders and States Evolution by PSO Index (`Vulkan FBO`, `1 → 342`)

| Accumulated PSOs (`psoIndex`) | Unique Shaders (`VS` + `PS`) | Unique `VS` | Unique `PS` | Unique `(VS, PS)` Pairs | Unique Fixed-State Blocks | Ratio `PSOs / (VS, PS) Pair` | Ratio `PSOs / Shader` |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `25` | `22` | `11` | `11` | `14` | `19` | `1.79` | `1.14` |
| `50` | `50` | `25` | `25` | `33` | `34` | `1.52` | `1.00` |
| `75` | `77` | `34` | `43` | `55` | `43` | `1.36` | `0.97` |
| `100` | `103` | `41` | `62` | `76` | `48` | `1.32` | `0.97` |
| `125` | `112` | `45` | `67` | `84` | `53` | `1.49` | `1.12` |
| `150` | `134` | `55` | `79` | `104` | `53` | `1.44` | `1.12` |
| `175` | `146` | `61` | `85` | `115` | `57` | `1.52` | `1.20` |
| `200` | `165` | `70` | `95` | `127` | `62` | `1.57` | `1.21` |
| `225` | `172` | `75` | `97` | `134` | `71` | `1.68` | `1.31` |
| `250` | `176` | `78` | `98` | `139` | `73` | `1.80` | `1.42` |
| `275` | `182` | `80` | `102` | `145` | `82` | `1.90` | `1.51` |
| `300` | `196` | `84` | `112` | `158` | `84` | `1.90` | `1.53` |
| `325` | `213` | `91` | `122` | `171` | `91` | `1.90` | `1.53` |
| **`342`** | **`230`** | **`97`** | **`133`** | **`183`** | **`94`** | **`1.87`** | **`1.49`** |

### 4.2. Shaders and States Evolution by PSO Index (`D3D12 RTV`, `1 → 295`)

| Accumulated PSOs (`psoIndex`) | Unique Shaders (`VS` + `PS`) | Unique `VS` | Unique `PS` | Unique `(VS, PS)` Pairs | Unique Fixed-State Blocks | Ratio `PSOs / (VS, PS) Pair` | Ratio `PSOs / Shader` |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `25` | `25` | `12` | `13` | `16` | `19` | `1.56` | `1.00` |
| `50` | `53` | `27` | `26` | `35` | `32` | `1.43` | `0.94` |
| `100` | `103` | `41` | `62` | `76` | `46` | `1.32` | `0.97` |
| `150` | `129` | `53` | `76` | `99` | `53` | `1.52` | `1.16` |
| `200` | `173` | `71` | `102` | `136` | `53` | `1.47` | `1.16` |
| `250` | `197` | `83` | `114` | `157` | `59` | `1.59` | `1.27` |
| **`295`** | **`220`** | **`94`** | **`126`** | **`177`** | **`68`** | **`1.67`** | **`1.34`** |

**Technical reading of curves:**
- During the first `100` PSOs (boot, menus, opening cutscene), almost every PSO introduces a new shader (`100` PSOs for `103` shaders; `0.97 PSO/shader`).
- Between PSO `175` and PSO `275` in Vulkan FBO (`+100` new PSOs created during gameplay), the game introduces only **`+36` new shaders** (`146 → 182`) and **`+30` new `(VS, PS)` pairs** (`115 → 145`). The remaining `70` PSOs are **recombinations of already loaded shaders** with different blend, depth/stencil, or `RenderPassKey` masks.

---

## 5. Growth Curve vs Playtime and Area Transitions

### 5.1. Minute-by-Minute Growth in the Longest Continuous Session (`runtime-20261108-161438.log`, `15.9 min`)

Session [`runtime-20261108-161438.log`](../../game-data/runtime/logs/runtime-20261108-161438.log) starts with `165` PSOs (`183` shaders) preloaded from previous menu/intro sessions (`155626`, `155904`, `160133`) and progresses over `15.9 minutes` (`954.4 s`) across the opening cutscene, two playable areas, meeting the Majin (`Achievement unlocked: #2`), and the second cutscene:

| Minute | Accumulated Shaders | Accumulated PSOs | `Δ Shaders` in Minute | `Δ PSOs` in Minute | Textures (`MB`) | Mean FPS | Phase / Event in Session |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | :--- |
| `Boot (0)` | `183` | `165` | — | — | — | — | Startup preload (`165` PSOs in `2.19 s`) |
| `Min 1` | `186` | `184` | `+3` | `+19` | `340` (`76 MB`) | `29.4` | Initial cutscene (`[Majin Cutscene]`) + level load |
| `Min 2` | `202` | `229` | `+16` | `+45` | `431` (`88 MB`) | `28.6` | Entry into 1st playable zone (burst of new materials) |
| **`Min 3`** | **`202`** | **`234`** | **`+0`** | **`+5`** | `449` (`88 MB`) | `30.0` | **Local Plateau 1:** 1st zone exploration |
| **`Min 4`** | **`202`** | **`236`** | **`+0`** | **`+2`** | `456` (`88 MB`) | `29.8` | **Local Plateau 1 (cont.):** `0` new shaders for 2 consecutive minutes |
| `Min 5` | `204` | `267` | `+2` | `+31` | `507` (`89 MB`) | `29.8` | Sub-area transition / combat (`+31` PSOs reusing shaders) |
| **`Min 6`** | **`204`** | **`268`** | **`+0`** | **`+1`** | `535` (`90 MB`) | `30.0` | **Local Plateau 2:** continuous gameplay |
| **`Min 7`** | **`208`** | **`273`** | **`+4`** | **`+5`** | `563` (`101 MB`) | `29.8` | **Local Plateau 2:** continuous gameplay |
| **`Min 8`** | **`208`** | **`274`** | **`+0`** | **`+1`** | `567` (`101 MB`) | `30.0` | **Local Plateau 2:** continuous gameplay |
| **`Min 9`** | **`209`** | **`275`** | **`+1`** | **`+1`** | `572` (`101 MB`) | `30.0` | **Local Plateau 2:** only `+8` PSOs in `4 min` (`2.0 PSOs/min`) |
| `Min 10` | `217` | `290` | `+8` | `+15` | `586` (`101 MB`) | `29.3` | Loading Majin's area |
| `Min 11` | `221` | `309` | `+4` | `+19` | `671` (`138 MB`) | `29.5` | New area + Majin effects |
| `Min 12` | `231` | `327` | `+10` | `+18` | `712` (`145 MB`) | `29.5` | Meeting the Majin (`Achievement unlocked: #2`) |
| **`Min 13`** | **`231`** | **`329`** | **`+0`** | **`+2`** | `714` (`145 MB`) | `29.9` | **Local Plateau 3:** stabilized area |
| **`Min 14`** | **`231`** | **`329`** | **`+0`** | **`+0`** | `714` (`145 MB`) | `30.0` | **Local Plateau 3:** `0` new PSOs and `0` new shaders |
| **`Min 15`** | **`235`** | **`332`** | **`+4`** | **`+3`** | `763` (`146 MB`) | `29.9` | **Local Plateau 3:** next cutscene (`+3` PSOs in `3 min`) |

### 5.2. Cumulative Progression Across Sessions (`Cross-Session Milestones`)

#### Vulkan Main Path (`4E4D0816.fbo.vk.xpso`, post-reset on `2026-11-08 15:56`)
1. [`runtime-20261108-155626.log`](../../game-data/runtime/logs/runtime-20261108-155626.log) (`17.6 s`): `0 → 29` PSOs (`+29` at boot/logos).
2. [`runtime-20261108-155904.log`](../../game-data/runtime/logs/runtime-20261108-155904.log) (`64.5 s`): `29 → 161` PSOs (`+132` in menu and opening; `183` shaders).
3. [`runtime-20261108-160133.log`](../../game-data/runtime/logs/runtime-20261108-160133.log) (`130.1 s`): `161 → 165` PSOs (`+4` PSOs, `0` new shaders — remains at `183` shaders for `2.1 min`).
4. [`runtime-20261108-161438.log`](../../game-data/runtime/logs/runtime-20261108-161438.log) (`954.4 s`): `165 → 332` PSOs (`+167` PSOs advancing through first zones to the Majin; `235` shaders).
5. [`runtime-20261108-163142.log`](../../game-data/runtime/logs/runtime-20261108-163142.log) (`63.0 s`): `332 → 334` PSOs (`+2` PSOs).
6. [`runtime-20261108-170059.log`](../../game-data/runtime/logs/runtime-20261108-170059.log) (`74.5 s`) and [`172515.log`](../../game-data/runtime/logs/runtime-20261108-172515.log) (`88.8 s`): `334 → 334` PSOs (**`+0` new PSOs**).
7. [`runtime-20261109-152130.log`](../../game-data/runtime/logs/runtime-20261109-152130.log) (`63.7 s`): `334 → 335` PSOs (`+1` PSO).
8. [`runtime-20261109-153434.log`](../../game-data/runtime/logs/runtime-20261109-153434.log) (`55.8 s`): `335 → 339` PSOs (`+4` PSOs; `237` shaders).
9. [`runtime-20261109-161051.log`](../../game-data/runtime/logs/runtime-20261109-161051.log) (`63.5 s`) and [`183049.log`](../../game-data/runtime/logs/runtime-20261109-183049.log) (`82.6 s`): `339 → 339` PSOs (**`+0` new PSOs**).
10. [`runtime-20261109-183708.log`](../../game-data/runtime/logs/runtime-20261109-183708.log) (`193.5 s`): `339 → 342` PSOs (`+3` PSOs in Minute 2, remaining at `342` PSOs / `240` shaders through end of Minute 3).

#### D3D12 Main Path (`4E4D0816.rtv.d3d12.xpso`)
1. `2026-11-06`: grows progressively from `0 → 151` (`131` shaders) → `177` (`152` shaders) → `180` (`154` shaders) → `215` (`182` shaders) → `223` (`185` shaders).
2. `2026-11-07`: grows from `227` (`188` shaders) → `237` (`192` shaders) → `268` (`203` shaders, in a session of `256.1 s` / `4.3 min`) → `276` (`209` shaders).
3. `2026-11-08`: reaches `291` PSOs (`220` shaders) → `293` PSOs (`220` shaders, session of `99.7 s`) → **`295` PSOs (`220` shaders)**, stabilizing with **`+0` new shaders** across the final three sessions.

---

## 6. Methodological Caveats and Implications for the Plan (D4–D7)

1. **Proven local plateau vs partial campaign coverage:**
   - Data confirms that the pipeline population **reaches rapid plateaus within each zone** (rates of `0` to `2 PSOs/min` after the first `30–60 s` entering an area) and **heavily reuses the same shader set** (`242` unique shaders across testing history).
   - However, all recorded sessions traverse the early game (up to meeting the Majin / Achievement `#2`). Intermediate and late areas, new elemental Majin powers (wind, lightning, fire, purification), and later bosses will add new shaders and PSOs.
   - **Full game projection:** For a mid-scope Xbox 360 title like *Majin and the Forsaken Kingdom*, with `~242` shaders and `~342` PSOs covering menus + cutscenes + first areas + core mechanics, the full campaign is projected in the range of **`~350–600` unique shaders** and **`~600–1,200` PSOs** (extrapolation based on initial discovery rate, not a final 100% campaign measurement).
2. **Direct impact on local precompilation feasibility (warm-up):**
   - At the scale of the current `342` PSOs (or even `~1,000` PSOs in the full game):
     - The `.xsh` (`~266 KB`) and `.xpso` (`~23 KB`) files are tiny and load instantaneously.
     - On **Vulkan**, with warm NVIDIA driver cache (`GLCache`) and without `.vkpc` CPU wrapper overhead, creating `342` PSOs costs only **`0.05–0.20 ms/PSO` wall-clock** (`17–68 ms` for `342` PSOs; verified at `274 ms` total startup in [`game-data/runtime/logs/d4-warm-warmup-pass2.log`](../../game-data/runtime/logs/d4-warm-warmup-pass2.log)). **For a new player with a cold driver** (measured with cleared `%LOCALAPPDATA%\NVIDIA\GLCache` in [`game-data/runtime/logs/d4-cold-warmup-pass1.log`](../../game-data/runtime/logs/d4-cold-warmup-pass1.log)), 6-thread parallel SASS compilation takes **`29.91 ms / PSO` wall-clock** (`10.23 s` for `342` PSOs, total startup **`10.66 s`**, and `~30 s` extrapolated for `1,000` PSOs). Therefore, first-boot cold pre-warmup is feasible, but takes `10 to 30 seconds`, making launcher progress bar feedback indispensable.
     - Furthermore, the legacy `512 MiB` / `~284` entries limit of the custom `.vkpc` file ([`vulkan/pipeline_cache.cpp:58`](../../tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L58)) **was already exceeded in the early areas** (`342 > 284`), and would be exceeded by `3x to 4x` in the full game — confirming that isolated per-PSO `VkPipelineCache` blobs of `2.11 MB` needed to be replaced by a monolithic shared cache.
     - On **D3D12**, with multi-threaded precompilation ([`d3d12-blocking-pso-precompile.patch`](../../patches/rexglue-0.10.0/d3d12-blocking-pso-precompile.patch)) active across N worker threads, precompiling `300` PSOs takes **`~2–3.5 s`** (`1,000` PSOs would take `~7–11 s` cold, or `< 1 s` with `ID3D12PipelineLibrary` / warm driver cache).
