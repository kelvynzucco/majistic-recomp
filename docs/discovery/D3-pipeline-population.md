# D3 — Pipeline Population & Growth Curve (`pipeline-population`)

- **Data:** 2026-11-10
- **Status:** Concluído (com ressalva de cobertura parcial do jogo)
- **Script reproduzível:** [`scripts/discovery/d3-pipeline-population.mjs`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/scripts/discovery/d3-pipeline-population.mjs)
- **Questão do plano ([`docs/SHADER_PRECOMPILATION_PLAN.md`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/docs/SHADER_PRECOMPILATION_PLAN.md#L106)):**
  > *"Quantos pipelines/shaders distintos o jogo utiliza, e essa contagem atinge um platô (plateau)?"*

---

## 1. Resumo Executivo

1. **População total observada nos caches binários em disco (`7` arquivos `.xsh` e `8` arquivos `.xpso`):**
   - **Shaders convidados únicos (`union` global de todos os `.xsh` e de todos os `203` logs):** **`242` shaders Xenos únicos** (**`100` vertex shaders** + **`142` pixel shaders**), somando apenas **`~266 KB`** de microcódigo Xenos bruto (média de **`1.102,2 bytes` / `275,6 dwords`** por shader; mínimo `36 bytes`, máximo `7.008 bytes`).
   - **Nenhum registro duplicado em `.xsh`:** todos os `7` arquivos `.xsh` têm `0` registros duplicados. Todos os `80` hashes de VS e `105` hashes de PS já emitidos em linhas de log históricas (`Creating graphics pipeline state`) já estão presentes nos `.xsh` em disco (`100%` de retenção dos shaders).
   - **Descrições de PSO únicas (`.xpso`) nos caches principais acumulados:**
     - **Vulkan FBO ([`4E4D0816.fbo.vk.xpso`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/vulkan-vulkan/release/shaders/shareable/4E4D0816.fbo.vk.xpso)):** **`342` PSOs únicos** (`23.276 bytes`), referenciando **`230` shaders únicos** (`97` VS + `133` PS), **`14` pipelines depth-only (`PS = 0`)**, **`183` pares `(VS, PS)` únicos**, **`195` tuplas `(VS_mod, PS_mod)` únicas** e **`94` blocos de estado fixo/render-pass únicos** (**média de `1,87 PSOs` por par `(VS, PS)`**, máximo `20`).
     - **D3D12 RTV ([`4E4D0816.rtv.d3d12.xpso`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/d3d12-default/release/shaders/shareable/4E4D0816.rtv.d3d12.xpso)):** **`295` PSOs únicos** (`21.252 bytes`), referenciando **`220` shaders únicos** (`94` VS + `126` PS), **`12` pipelines depth-only (`PS = 0`)**, **`177` pares `(VS, PS)` únicos**, **`190` tuplas `(VS_mod, PS_mod)` únicas** e **`68` blocos de estado fixo únicos** (**média de `1,67 PSOs` por par `(VS, PS)`**, máximo `12`).
     - **Vulkan FSI ([`4E4D0816.fsi.vk.xpso`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/vulkan-vulkan/release/shaders/shareable/4E4D0816.fsi.vk.xpso)) e D3D12 ROV ([`4E4D0816.rov.d3d12.xpso`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/d3d12-rov/release/shaders/shareable/4E4D0816.rov.d3d12.xpso)):** **`168` PSOs** (`136` pares `(VS, PS)`, apenas `15` blocos de estado fixo, **`1,24 PSOs/par`**) e **`143` PSOs** (`121` pares `(VS, PS)`, apenas `13` blocos de estado fixo, **`1,18 PSOs/par`**). Nos caminhos FSI/ROV (onde o blending e o depth/stencil EDRAM são resolvidos no pixel shader via interlock/ROV em vez de estado fixo de pipeline), a multiplicação combinatória de estado por par `(VS, PS)` cai de `1,67–1,87x` para **`1,18–1,24x`**.
2. **Alta sobreposição entre backends (`Vulkan FBO` vs. `D3D12 RTV`):**
   - Dos `230` shaders usados no Vulkan FBO e `220` no D3D12 RTV, **`208` shaders (`86,0%` da união de `242` shaders) são idênticos** entre ambos os backends.
   - Dos `183` pares `(VS, PS)` no Vulkan FBO e `177` no D3D12 RTV, **`163` pares (`82,7%` da união de `197` pares em disco) são idênticos**.
   - Além disso, o cache `d3d12-rtv` (`157` PSOs) é um **subconjunto exato de `100%` (`157/157`)** dos hashes `XXH3_64bits` de `PipelineDescription` presentes em `d3d12-default` (`295` PSOs), confirmando que `PipelineDescription` é determinístico entre execuções na mesma rota.
3. **Dinâmica de crescimento e platô (dentro de zonas vs. entre zonas):**
   - **Platôs locais nítidos dentro de cada zona explorada:** Na maior sessão contínua ([`runtime-20261108-161438.log`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-161438.log), `15,9 min`), o crescimento ocorre em **degraus concentrados nas transições de zona/cutscene**, intercalados por **platôs de vários minutos**:
     - Minutos 1–2 (cutscene inicial + entrada na Zona 1): `165 → 229` PSOs (`+64` PSOs, `+19` shaders).
     - **Platô 1 (Minutos 3–4, explorando a Zona 1):** `229 → 236` PSOs (**`+7` PSOs em 2 minutos e `+0` novos shaders** — cravado em `202` shaders).
     - Minuto 5 (transição de subárea/combate): `236 → 267` PSOs (`+31` PSOs, `+2` shaders).
     - **Platô 2 (Minutos 6–9, 4 minutos contínuos de gameplay):** `267 → 275` PSOs (**apenas `+8` PSOs e `+5` shaders em 4 minutos = `2,0 PSOs/min`**).
     - Minutos 10–12 (nova área principal + encontro com o Majin / Conquista `#2`): `275 → 327` PSOs (`+52` PSOs, `+22` shaders).
     - **Platô 3 (Minutos 13–15, 3 minutos contínuos + 2ª cutscene):** `327 → 332` PSOs (**apenas `+5` PSOs e `+4` shaders em 3 minutos = `1,7 PSOs/min`**).
   - **Estabilização nas rotas já visitadas entre sessões subsequentes:** Nas 6 sessões Vulkan seguintes ao longo de 2 dias (`2026-11-08 16:31` a `2026-11-09 18:37`), o cache cresceu apenas de `332 → 334 → 335 → 339 → 342` PSOs (**`+10` PSOs no total**), com sessões inteiras de `63–89 s` registrando **`0` novos PSOs e `0,0 ms` de espera**. No D3D12, as últimas sessões de `2026-11-08` cresceram de `291 → 293 → 295` PSOs com **`+0` novos shaders** (estacionado em `220` shaders).
   - **Sublinearidade de shaders em relação a PSOs:** Conforme o cache cresce de `100` para `342` PSOs no Vulkan FBO, a razão `PSOs / Shader` sobe de **`0,97`** (`100` PSOs para `103` shaders) para **`1,49`** (`342` PSOs para `230` shaders), e a razão `PSOs / Par (VS, PS)` sobe de **`1,32`** para **`1,87`**. Isso mostra que novos PSOs em áreas posteriores reutilizam cada vez mais shaders já conhecidos sob novas combinações de estado fixo/render pass.
4. **Ressalva crítica — Platô local vs. Cobertura do jogo inteiro:**
   - Os caches atuais cobrem o início do jogo (menu, cutscenes iniciais e as primeiras `~3–4` áreas jogáveis até o encontro com o Majin / Conquista `#2`). Portanto, os números **`242` shaders** e **`342` PSOs (Vulkan FBO) / `295` PSOs (D3D12 RTV)** demonstram um **platô local nas rotas testadas**, e **não** o teto de `100%` da campanha.
   - Ainda assim, a escala do motor de *Majin and the Forsaken Kingdom* é modesta (centenas de shaders e algumas centenas a ~1.000 PSOs na campanha inteira estimada, não dezenas de milhares), tornando a pré-compilação local em disco viável se obtida a lista de pipelines. No D3D12 com o patch multithread leva `~2–3 s` para `300` PSOs a quente; no Vulkan sem o overhead de `.vkpc` leva `< 100 ms` com driver aquecido, ou `~6 s` em paralelo com driver completamente frio (medido empiricamente).

---

## 2. Formato Binário dos Caches Analisados (Código-Fonte do SDK)

A leitura direta em [`scripts/discovery/d3-pipeline-population.mjs`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/scripts/discovery/d3-pipeline-population.mjs) segue exatamente as estruturas C++ do SDK:

1. **Arquivo de Shaders Convidados (`4E4D0816.xsh`)** — definido em [`pipeline_store_file.h:35-69`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/include/rex/graphics/pipeline_store_file.h#L35-L69):
   - Cabeçalho de `8 bytes` (`ShaderStorageFileHeader`): `magic = 0x48534558` (`'XESH'`), `version_swapped = 0x19122020` (`SwapBytes(0x20201219)`).
   - Sequência de registros variáveis: `ShaderStoredHeader` de `12 bytes` (`uint64_t ucode_data_hash`, `uint32_t ucode_dword_count : 31`, `xenos::ShaderType type : 1`, onde `0 = kVertex` e `1 = kPixel` conforme [`xenos.h:33-36`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/include/rex/graphics/xenos.h#L33-L36)) seguido por `ucode_dword_count * 4` bytes de microcódigo Xenos.
2. **Arquivo de Descrições de Pipeline Vulkan (`4E4D0816.<fbo|fsi>.vk.xpso`)** — definido em [`vulkan/pipeline_cache.h:141-272`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/include/rex/graphics/vulkan/pipeline_cache.h#L141-L272):
   - Cabeçalho de `20 bytes` (`Format 2`, com `version_2_marker = 0xFFFFFFFF`, `description_version_swapped = SwapBytes(0x20260228)`, `modification_version_swapped = SwapBytes(0x20261018)`) nos arquivos `.fbo.vk.xpso`, ou cabeçalho legado de `12 bytes` nos arquivos `.fsi.vk.xpso`.
   - Registros fixos de **`68 bytes`** (`PipelineStoredDescription`): `8 bytes` de `description_hash` (`XXH3_64bits`) + `60 bytes` de `PipelineDescription`:
     - `vertex_shader_hash` (`8B`), `vertex_shader_modification` (`8B`), `pixel_shader_hash` (`8B`), `pixel_shader_modification` (`8B`);
     - `render_pass_key` (`4B`, [`vulkan/render_target_cache.h:36-78`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/include/rex/graphics/vulkan/render_target_cache.h#L36-L78));
     - `8B` de registradores de rasterização/depth/stencil (`depth_bias`, `depth_bias_slope_scaled`, bitfields de topologia, cull mode, depth/stencil, etc.) + `16B` de `PipelineRenderTarget[4]` (blend/color write mask).
3. **Arquivo de Descrições de Pipeline D3D12 (`4E4D0816.<rtv|rov>.d3d12.xpso`)** — definido em [`d3d12/pipeline_cache.h:101-228`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/include/rex/graphics/d3d12/pipeline_cache.h#L101-L228):
   - Cabeçalho de `12 bytes` (`magic = 'XEPS'`, `magic_api = 'DXRT'` ou `'DXRO'`, `version_swapped = SwapBytes(0x20260226)`).
   - Registros fixos de **`72 bytes`** (`PipelineStoredDescription`): `8 bytes` de `description_hash` (`XXH3_64bits`) + `64 bytes` de `PipelineDescription` (`vertex_shader_hash`, `vertex_shader_modification`, `pixel_shader_hash`, `pixel_shader_modification`, `depth_bias`, `depth_bias_slope_scaled`, `8B` de bitfields de estado fixo e `16B` de `PipelineRenderTarget[4]`).

---

## 3. População Completa nos Caches em Disco (`game-data/runtime/cache/`)

### 3.1. Arquivos `.xsh` (Shaders Convidados Xenos)

| Diretório de Cache | Arquivo `.xsh` | Tamanho (Bytes) | Registros no Arquivo | Duplicatas | Shaders Únicos | Vertex Shaders (`VS`) | Pixel Shaders (`PS`) | Tamanho Médio do Microcódigo | Min / Max Microcódigo |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `vulkan-vulkan` | [`4E4D0816.xsh`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/vulkan-vulkan/release/shaders/shareable/4E4D0816.xsh) | `266.312` | `239` | `0` | **`239`** | `100` | `139` | `1.102,2 B` | `36 B` / `7.008 B` |
| `d3d12-default` | [`4E4D0816.xsh`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/d3d12-default/release/shaders/shareable/4E4D0816.xsh) | `251.864` | `220` | `0` | **`220`** | `94` | `126` | `1.132,8 B` | `36 B` / `6.816 B` |
| `d3d12-rov` | [`4E4D0816.xsh`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/d3d12-rov/release/shaders/shareable/4E4D0816.xsh) | `157.568` | `160` | `0` | **`160`** | `69` | `91` | `972,8 B` | `36 B` / `6.420 B` |
| `vulkan-vulkan_fsi` | [`4E4D0816.xsh`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/vulkan-vulkan_fsi/release/shaders/shareable/4E4D0816.xsh) | `132.620` | `147` | `0` | **`147`** | `61` | `86` | `890,1 B` | `36 B` / `6.420 B` |
| `d3d12-rtv` | [`4E4D0816.xsh`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/d3d12-rtv/release/shaders/shareable/4E4D0816.xsh) | `129.548` | `143` | `0` | **`143`** | `61` | `82` | `893,9 B` | `36 B` / `6.312 B` |
| `vulkan-fbo` | [`4E4D0816.xsh`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/vulkan-fbo/release/shaders/shareable/4E4D0816.xsh) | `944` | `9` | `0` | **`9`** | `5` | `4` | `92,0 B` | `36 B` / `156 B` |
| `vulkan-fsi` | [`4E4D0816.xsh`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/vulkan-fsi/release/shaders/shareable/4E4D0816.xsh) | `656` | `7` | `0` | **`7`** | `4` | `3` | `80,6 B` | `36 B` / `132 B` |
| **União Global (`.xsh`)** | **Todos os `7` arquivos** | — | — | `0` | **`242`** | **`100`** | **`142`** | **`~1.102 B`** | **`36 B` / `7.008 B`** |

> **Observação sobre retenção de `.xsh`:** O arquivo [`vulkan-vulkan/release/shaders/shareable/4E4D0816.xsh`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/vulkan-vulkan/release/shaders/shareable/4E4D0816.xsh) sozinho contém **`239` dos `242` shaders únicos** já vistos em toda a história do projeto (`100/100` VS e `139/142` PS). Os outros `3` pixel shaders estão em `d3d12-default`.

### 3.2. Arquivos `.xpso` (Descrições de Pipeline State Object)

| Cache / Arquivo `.xpso` | Backend / Modo | Formato Header | Bytes | PSOs Únicos (`records`) | Shaders Ref. (`VS` + `PS`) | PSOs Depth-Only (`PS=0`) | Modificações (`VS_mod` / `PS_mod`) | Pares `(VS, PS)` Únicos | Tuplas `(VS_mod, PS_mod)` | Blocos de Estado Fixo Únicos | Média `PSOs / Par (VS,PS)` | Máx `PSOs` p/ 1 Par |
| :--- | :--- | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| [`vulkan-vulkan` (`fbo.vk.xpso`)](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/vulkan-vulkan/release/shaders/shareable/4E4D0816.fbo.vk.xpso) | Vulkan `fbo` | Format 2 (`20B`) | `23.276` | **`342`** | **`230`** (`97` + `133`) | `14` | `99` / `144` | **`183`** | `195` | **`94`** | **`1,87`** | `20` |
| [`d3d12-default` (`rtv.d3d12.xpso`)](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/d3d12-default/release/shaders/shareable/4E4D0816.rtv.d3d12.xpso) | D3D12 `rtv` | Legacy (`12B`) | `21.252` | **`295`** | **`220`** (`94` + `126`) | `12` | `96` / `138` | **`177`** | `190` | **`68`** | **`1,67`** | `12` |
| [`vulkan-vulkan` (`fsi.vk.xpso`)](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/vulkan-vulkan/release/shaders/shareable/4E4D0816.fsi.vk.xpso) | Vulkan `fsi` | Legacy (`12B`) | `11.436` | **`168`** | **`179`** (`77` + `102`) | `9` | `79` / `103` | **`136`** | `137` | **`15`** | **`1,24`** | `4` |
| [`d3d12-rtv` (`rtv.d3d12.xpso`)](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/d3d12-rtv/release/shaders/shareable/4E4D0816.rtv.d3d12.xpso) | D3D12 `rtv` | Legacy (`12B`) | `11.316` | **`157`** | **`143`** (`61` + `82`) | `10` | `63` / `90` | **`104`** | `112` | **`54`** | **`1,51`** | `8` |
| [`d3d12-rov` (`rov.d3d12.xpso`)](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/d3d12-rov/release/shaders/shareable/4E4D0816.rov.d3d12.xpso) | D3D12 `rov` | Legacy (`12B`) | `10.308` | **`143`** | **`160`** (`69` + `91`) | `8` | `71` / `92` | **`121`** | `122` | **`13`** | **`1,18`** | `3` |
| [`vulkan-fbo` (`fbo.vk.xpso`)](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/vulkan-fbo/release/shaders/shareable/4E4D0816.fbo.vk.xpso) | Vulkan `fbo` | Format 2 (`20B`) | `972` | **`14`** | **`9`** (`5` + `4`) | `9` | `6` / `4` | **`6`** | `6` | **`12`** | **`2,33`** | `5` |
| [`vulkan-fsi` (`fsi.vk.xpso`)](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/vulkan-fsi/release/shaders/shareable/4E4D0816.fsi.vk.xpso) | Vulkan `fsi` | Legacy (`12B`) | `488` | **`7`** | **`7`** (`4` + `3`) | `4` | `5` / `3` | **`5`** | `5` | **`5`** | **`1,40`** | `2` |
| [`vulkan-vulkan_fsi` (`fsi.vk.xpso`)](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/vulkan-vulkan_fsi/release/shaders/shareable/4E4D0816.fsi.vk.xpso) | Vulkan `fsi` | Legacy (`12B`) | `12` | **`0`** | **`0`** | `0` | `0` / `0` | **`0`** | `0` | **`0`** | — | `0` |

### 3.3. Sobreposição Cruzada entre Backends e com os Logs Históricos

| Comparação | Métrica | Valor |
| :--- | :--- | :--- |
| **Vulkan FBO (`342` PSOs) vs. D3D12 RTV (`295` PSOs)** | Shaders compartilhados / União de shaders | **`208 / 242` (`86,0%`)** |
| **Vulkan FBO (`342` PSOs) vs. D3D12 RTV (`295` PSOs)** | Pares `(VS, PS)` compartilhados / União em disco | **`163 / 197` (`82,7%`)** |
| **`d3d12-rtv` (`157` PSOs) vs. `d3d12-default` (`295` PSOs)** | Hashes `XXH3_64bits` de `PipelineDescription` compartilhados | **`157 / 157` (`100,0%` subconjunto exato)** |
| **Logs históricos (`203` arquivos) vs. `.xsh` em disco** | Hashes de `VS` e `PS` nos logs presentes nos `.xsh` atuais | **`80/80` VS (`100%`) e `105/105` PS (`100%`)** |
| **Logs históricos (`203` arquivos) vs. `.xpso` em disco** | Pares `(VS, PS)` nos logs presentes nos `.xpso` atuais | **`142 / 217`** (`75` pares ocorreram em sessões anteriores ao reset de `.xpso` em `2026-11-08 15:56`, embora **todos** os seus shaders individuais sigam nos `.xsh`) |

> **Implicação arquitetural importante:** Como `100%` dos shaders vistos em logs antigos continuam no `.xsh` mesmo quando o `.xpso` foi recriado (`75` pares `(VS, PS)` de sessões pré-reset ainda não reapareceram após o reset de `15:56`), a união histórica real de pares `(VS, PS)` já observados chega a **`272` pares `(VS, PS)`** sobre os mesmos **`242` shaders**.

---

## 4. Curva de Crescimento por Ordem de Descoberta (Dentro do `.xpso`)

Como o `PipelineStorage` grava cada novo `PipelineStoredDescription` sequencialmente em modo `ab` (append binary) na ordem exata em que o jogo descobre cada pipeline inédito, a ordem dos registros dentro de [`4E4D0816.fbo.vk.xpso`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/vulkan-vulkan/release/shaders/shareable/4E4D0816.fbo.vk.xpso) (`342` PSOs) e [`4E4D0816.rtv.d3d12.xpso`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/cache/d3d12-default/release/shaders/shareable/4E4D0816.rtv.d3d12.xpso) (`295` PSOs) revela como os shaders e blocos de estado escalam à medida que a população de PSOs cresce:

### 4.1. Evolução de Shaders e Estados por Índice de PSO (`Vulkan FBO`, `1 → 342`)

| PSOs Acumulados (`psoIndex`) | Shaders Únicos (`VS` + `PS`) | `VS` Únicos | `PS` Únicos | Pares `(VS, PS)` Únicos | Blocos de Estado Fixo Únicos | Razão `PSOs / Par (VS, PS)` | Razão `PSOs / Shader` |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `25` | `22` | `11` | `11` | `14` | `19` | `1,79` | `1,14` |
| `50` | `50` | `25` | `25` | `33` | `34` | `1,52` | `1,00` |
| `75` | `77` | `34` | `43` | `55` | `43` | `1,36` | `0,97` |
| `100` | `103` | `41` | `62` | `76` | `48` | `1,32` | `0,97` |
| `125` | `112` | `45` | `67` | `84` | `53` | `1,49` | `1,12` |
| `150` | `134` | `55` | `79` | `104` | `53` | `1,44` | `1,12` |
| `175` | `146` | `61` | `85` | `115` | `57` | `1,52` | `1,20` |
| `200` | `165` | `70` | `95` | `127` | `62` | `1,57` | `1,21` |
| `225` | `172` | `75` | `97` | `134` | `71` | `1,68` | `1,31` |
| `250` | `176` | `78` | `98` | `139` | `73` | `1,80` | `1,42` |
| `275` | `182` | `80` | `102` | `145` | `82` | `1,90` | `1,51` |
| `300` | `196` | `84` | `112` | `158` | `84` | `1,90` | `1,53` |
| `325` | `213` | `91` | `122` | `171` | `91` | `1,90` | `1,53` |
| **`342`** | **`230`** | **`97`** | **`133`** | **`183`** | **`94`** | **`1,87`** | **`1,49`** |

### 4.2. Evolução de Shaders e Estados por Índice de PSO (`D3D12 RTV`, `1 → 295`)

| PSOs Acumulados (`psoIndex`) | Shaders Únicos (`VS` + `PS`) | `VS` Únicos | `PS` Únicos | Pares `(VS, PS)` Únicos | Blocos de Estado Fixo Únicos | Razão `PSOs / Par (VS, PS)` | Razão `PSOs / Shader` |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `25` | `25` | `12` | `13` | `16` | `19` | `1,56` | `1,00` |
| `50` | `53` | `27` | `26` | `35` | `32` | `1,43` | `0,94` |
| `100` | `103` | `41` | `62` | `76` | `46` | `1,32` | `0,97` |
| `150` | `129` | `53` | `76` | `99` | `53` | `1,52` | `1,16` |
| `200` | `173` | `71` | `102` | `136` | `53` | `1,47` | `1,16` |
| `250` | `197` | `83` | `114` | `157` | `59` | `1,59` | `1,27` |
| **`295`** | **`220`** | **`94`** | **`126`** | **`177`** | **`68`** | **`1,67`** | **`1,34`** |

**Leitura técnica das curvas:**
- Nos primeiros `100` PSOs (boot, menus e início da cutscene), quase todo PSO traz um shader inédito (`100` PSOs para `103` shaders; `0,97 PSO/shader`).
- Entre o PSO `175` e o PSO `275` no Vulkan FBO (`+100` novos PSOs criados em gameplay), o jogo introduz apenas **`+36` novos shaders** (`146 → 182`) e **`+30` novos pares `(VS, PS)`** (`115 → 145`). Os outros `70` PSOs são **recombinações de shaders já carregados** com diferentes máscaras de blend, depth/stencil ou `RenderPassKey`.

---

## 5. Curva de Crescimento vs. Tempo de Jogo e Transições de Área

### 5.1. Crescimento Minuto a Minuto na Maior Sessão Contínua (`runtime-20261108-161438.log`, `15,9 min`)

A sessão [`runtime-20261108-161438.log`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-161438.log) inicia com `165` PSOs (`183` shaders) pré-carregados das sessões anteriores de menu/introdução (`155626`, `155904`, `160133`) e avança por `15,9 minutos` (`954,4 s`) através da cutscene de abertura, duas áreas jogáveis, encontro com o Majin (`Achievement unlocked: #2`) e segunda cutscene:

| Minuto | Shaders Acumulados | PSOs Acumulados | `Δ Shaders` no Minuto | `Δ PSOs` no Minuto | Texturas (`MB`) | FPS Médio | Fase / Evento na Sessão |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | :--- |
| `Boot (0)` | `183` | `165` | — | — | — | — | Preload no startup (`165` PSOs em `2,19 s`) |
| `Min 1` | `186` | `184` | `+3` | `+19` | `340` (`76 MB`) | `29,4` | Cutscene inicial (`[Majin Cutscene]`) + carregamento de nível |
| `Min 2` | `202` | `229` | `+16` | `+45` | `431` (`88 MB`) | `28,6` | Entrada na 1ª zona jogável (burst de novos materiais) |
| **`Min 3`** | **`202`** | **`234`** | **`+0`** | **`+5`** | `449` (`88 MB`) | `30,0` | **Platô local 1:** exploração da 1ª zona |
| **`Min 4`** | **`202`** | **`236`** | **`+0`** | **`+2`** | `456` (`88 MB`) | `29,8` | **Platô local 1 (cont.):** `0` novos shaders por 2 min seguidos |
| `Min 5` | `204` | `267` | `+2` | `+31` | `507` (`89 MB`) | `29,8` | Transição de subárea / combate (`+31` PSOs reutilizando shaders) |
| **`Min 6`** | **`204`** | **`268`** | **`+0`** | **`+1`** | `535` (`90 MB`) | `30,0` | **Platô local 2:** gameplay contínuo |
| **`Min 7`** | **`208`** | **`273`** | **`+4`** | **`+5`** | `563` (`101 MB`) | `29,8` | **Platô local 2:** gameplay contínuo |
| **`Min 8`** | **`208`** | **`274`** | **`+0`** | **`+1`** | `567` (`101 MB`) | `30,0` | **Platô local 2:** gameplay contínuo |
| **`Min 9`** | **`209`** | **`275`** | **`+1`** | **`+1`** | `572` (`101 MB`) | `30,0` | **Platô local 2:** apenas `+8` PSOs em `4 min` (`2,0 PSOs/min`) |
| `Min 10` | `217` | `290` | `+8` | `+15` | `586` (`101 MB`) | `29,3` | Carregamento da área do Majin |
| `Min 11` | `221` | `309` | `+4` | `+19` | `671` (`138 MB`) | `29,5` | Nova área + efeitos do Majin |
| `Min 12` | `231` | `327` | `+10` | `+18` | `712` (`145 MB`) | `29,5` | Encontro com o Majin (`Achievement unlocked: #2`) |
| **`Min 13`** | **`231`** | **`329`** | **`+0`** | **`+2`** | `714` (`145 MB`) | `29,9` | **Platô local 3:** área estabilizada |
| **`Min 14`** | **`231`** | **`329`** | **`+0`** | **`+0`** | `714` (`145 MB`) | `30,0` | **Platô local 3:** `0` novos PSOs e `0` novos shaders |
| **`Min 15`** | **`235`** | **`332`** | **`+4`** | **`+3`** | `763` (`146 MB`) | `29,9` | **Platô local 3:** cutscene seguinte (`+3` PSOs em `3 min`) |

### 5.2. Progressão Acumulada entre Sessões (`Cross-Session Milestones`)

#### Caminho Principal Vulkan (`4E4D0816.fbo.vk.xpso`, após reset em `2026-11-08 15:56`)
1. [`runtime-20261108-155626.log`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-155626.log) (`17,6 s`): `0 → 29` PSOs (`+29` no boot/logos).
2. [`runtime-20261108-155904.log`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-155904.log) (`64,5 s`): `29 → 161` PSOs (`+132` no menu e abertura; `183` shaders).
3. [`runtime-20261108-160133.log`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-160133.log) (`130,1 s`): `161 → 165` PSOs (`+4` PSOs, `0` novos shaders — permanece em `183` shaders por `2,1 min`).
4. [`runtime-20261108-161438.log`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-161438.log) (`954,4 s`): `165 → 332` PSOs (`+167` PSOs avançando pelas primeiras zonas até o Majin; `235` shaders).
5. [`runtime-20261108-163142.log`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-163142.log) (`63,0 s`): `332 → 334` PSOs (`+2` PSOs).
6. [`runtime-20261108-170059.log`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-170059.log) (`74,5 s`) e [`172515.log`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261108-172515.log) (`88,8 s`): `334 → 334` PSOs (**`+0` novos PSOs**).
7. [`runtime-20261109-152130.log`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261109-152130.log) (`63,7 s`): `334 → 335` PSOs (`+1` PSO).
8. [`runtime-20261109-153434.log`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261109-153434.log) (`55,8 s`): `335 → 339` PSOs (`+4` PSOs; `237` shaders).
9. [`runtime-20261109-161051.log`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261109-161051.log) (`63,5 s`) e [`183049.log`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261109-183049.log) (`82,6 s`): `339 → 339` PSOs (**`+0` novos PSOs**).
10. [`runtime-20261109-183708.log`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/runtime-20261109-183708.log) (`193,5 s`): `339 → 342` PSOs (`+3` PSOs no Minuto 2, permanecendo em `342` PSOs / `240` shaders até o fim do Minuto 3).

#### Caminho Principal D3D12 (`4E4D0816.rtv.d3d12.xpso`)
1. `2026-11-06`: cresce progressivamente de `0 → 151` (`131` shaders) → `177` (`152` shaders) → `180` (`154` shaders) → `215` (`182` shaders) → `223` (`185` shaders).
2. `2026-11-07`: cresce de `227` (`188` shaders) → `237` (`192` shaders) → `268` (`203` shaders, em uma sessão de `256,1 s` / `4,3 min`) → `276` (`209` shaders).
3. `2026-11-08`: atinge `291` PSOs (`220` shaders) → `293` PSOs (`220` shaders, sessão de `99,7 s`) → **`295` PSOs (`220` shaders)**, estabilizando com **`+0` novos shaders** nas últimas três sessões.

---

## 6. Ressalvas Metodológicas e Implicações para o Plano (D4–D7)

1. **Platô local comprovado vs. Cobertura parcial da campanha:**
   - Os dados comprovam que a população de pipelines **atinge platôs rápidos dentro de cada zona** (taxas de `0` a `2 PSOs/min` após os primeiros `30–60 s` de entrada numa área) e **reutiliza fortemente o mesmo conjunto de shaders** (`242` shaders únicos em toda a história de testes).
   - Contudo, todas as sessões registradas percorrem o início do jogo (até o encontro com o Majin / Conquista `#2`). Áreas intermediárias e finais do reino, novos poderes elementais do Majin (vento, raio, fogo, purificação) e chefes posteriores certamente adicionarão novos shaders e PSOs.
   - **Projeção estimada para o jogo completo:** Para um jogo Xbox 360 single-player de escopo médio como *Majin and the Forsaken Kingdom*, tendo `~242` shaders e `~342` PSOs cobrindo menus + cutscenes + as primeiras áreas + mecânicas base do protagonista e do Majin, a campanha inteira é projetada na faixa de **`~350–600` shaders únicos** e **`~600–1.200` PSOs** (estimativa baseada em extrapolação da taxa de descoberta inicial, não uma medição final de 100% da campanha).
2. **Impacto direto sobre a viabilidade de pré-compilação local (brute-force warm-up):**
   - Na escala de `342` PSOs atuais (ou mesmo `~1.000` PSOs no jogo completo):
     - Os arquivos `.xsh` (`~266 KB`) e `.xpso` (`~23 KB`) são minúsculos e já carregam instantaneamente.
     - No **Vulkan**, com o driver NVIDIA (`GLCache`) aquecido e sem `.vkpc`, criar `342` PSOs custa apenas **`0,05–0,20 ms/PSO` de relógio** (`17–68 ms` para `342` PSOs; verificado a `274 ms` total de inicialização em [`game-data/runtime/logs/d4-warm-warmup-pass2.log`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/d4-warm-warmup-pass2.log)). **Para um jogador novo com driver frio** (medido com `%LOCALAPPDATA%\NVIDIA\GLCache` limpo em [`game-data/runtime/logs/d4-cold-warmup-pass1.log`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/game-data/runtime/logs/d4-cold-warmup-pass1.log)), a compilação SASS paralela em 6 threads leva **`29,91 ms / PSO` de relógio** (`10,23 s` para os `342` PSOs, total de inicialização **`10,66 s`**, e `~30 s` extrapolados para `1.000` PSOs). Portanto, o pré-aquecimento a frio no primeiro boot é viável, mas leva de `10 a 30 segundos`, tornando indispensável o feedback visual via barra de progresso no launcher.
     - Além disso, o limite atual de `512 MiB` / `~284` entradas do arquivo `.vkpc` customizado ([`vulkan/pipeline_cache.cpp:58`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L58)) **já foi ultrapassado nas primeiras áreas do jogo** (`342 > 284`), e seria excedido por `3x a 4x` no jogo completo — reforçando que a arquitetura de `VkPipelineCache` isolado por PSO com blob de `2,11 MB` precisa ser substituída.
     - No **D3D12**, com o patch [`d3d12-blocking-pso-precompile.patch`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/patches/rexglue-0.10.0/d3d12-blocking-pso-precompile.patch) ativo (N threads paralelas), pré-compilar `300` PSOs leva **`~2–3,5 s`** (e `1.000` PSOs levaria `~7–11 s` a frio, ou `< 1 s` se combinado com `ID3D12PipelineLibrary` / driver cache).
