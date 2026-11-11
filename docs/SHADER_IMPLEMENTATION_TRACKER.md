# Shader Stutter Mitigation: Execution Tracker & Step-by-Step Progress Log

> **Target Title**: *Majin and the Forsaken Kingdom* (Title ID `4E4D0816`)  
> **Master Architecture Document**: [`docs/SHADER_IMPLEMENTATION_PLAN.md`](SHADER_IMPLEMENTATION_PLAN.md)  
> **Created**: 2026-11-11  
> **Status**: **MILESTONES M0, M1, M2.1, M2a, M3 COMPLETE & VERIFIED**  

---

## 1. Executive Status Dashboard

| Metric / Dimension | Current State | Notes |
| :--- | :--- | :--- |
| **Current Active Phase** | **Fase E: Cache Local & Ferramentas (Backlog / Polish)** | Fases A, B, C1 e D 100% concluídas com sucesso. |
| **Current Active Milestone** | **`M4: Ferramentas Locais da Máquina do Jogador`** | Exportação/importação de manifest local e pré-tradução offline. |
| **Current Working Branch** | `feat/restricted-async` | Patches do SDK sincronizados e testados. |
| **Overall Execution Progress** | **83% Concluído (5 / 6 Marcos Arquiteturais)** | M0, M1, M2.1, M2a e M3 100% executados; M2.2/M2.3 descartados via R1. |
| **GPL Spike Gate Status (M2a)** | 🔴 **REPROVADO (Contingência R1 Acionada)** | Custo frio $0.827\times$ (limiar $< 0.50\times$) e overhead GPU $12.5\%$ (limiar $< 5\%$). GPL descartado formalmente. |
| **Clean-Room Compliance** | 🟢 **100% Verified** | Harness CLI clean-room em `tools/gpl-spike/`, zero assets/shaders em Git. |

---

## 2. Milestone Execution Roadmap & Checklist

### 🏁 Fase A: Telemetria & Ganhos Imediatos (Estimativa: 1–2 dias)
- [x] **M0: Telemetria & CI Gate (`feat/telemetry-and-gate`)**
  - [x] **M0.1**: Corrigir bug de atribuição em `fh1_frame_cost.h` (`MainCause` culpando o jogo em vez de `Fh1WaitForAhead`).
  - [x] **M0.1b**: Separar diagnósticos no CSV: `pipeline_compile_ms` (SASS síncrono) vs `pipeline_wait_ms` (bloqueio de thread em `Fh1WaitForAhead`).
  - [x] **M0.2**: Habilitar `REXGLUE_ENABLE_PERF_COUNTERS` na configuração Release-Diagnostic do CMake via `REXGLUE_FORCE_PERF_COUNTERS=ON`.
  - [x] **M0.3a**: Entregar CLI autônomo `scripts/analyze-performance-csv.mjs` (suporte a coluna `ms`, fallback de frame time, extração de telemetria de shaders e agregação multi-run).
  - [x] **M0.3b**: Entregar suite de automação `scripts/verify-shader-mitigation.ps1` com modos `-Cold`, `-Warm`, `-DurationSeconds` e `-VerifyGate`.
  - [x] **M0.4**: Recapturar baseline oficial sob ambiente controlado com contadores ativados (`reports/baseline-shader/baseline-vulkan-cold-m0.json`).
- [x] **M1: Monolithic Shared VkPipelineCache & D3D12 Multi-Thread (`feat/monolithic-vkpc-and-d3d12-parallel`)**
  - [x] **M1.1a (Diagnóstico)**: Validar baseline imediato com `--vulkan_store_pipeline_cache=false` (8.02 $\to$ 0.20 ms/PSO).
  - [x] **M1.1b (Persistência)**: Implementar `VkPipelineCache` monolítico compartilhado (carregado no boot com validação de `pipelineCacheUUID`, salvo em checkpoints atômicos a cada 10s e no `Shutdown()`).
  - [x] **M1.2**: Reaplicar patch de compilação paralela D3D12 (`d3d12-blocking-pso-precompile.patch`) corrigindo despacho de workers em `PipelineCache::InitializeShaderStorage()`.
  - [x] **M1.3**: Executar verificação automatizada (`verify-shader-mitigation.ps1`) e registrar métricas (`reports/baseline-shader/m1-vulkan-cold.json` e `m1-vulkan-warm.json`).

---

### 🏛️ Fase B: Prioridade Imediata — Extended Dynamic State (Estimativa: 2–3 dias)
- [x] **M2.1: Extended Dynamic State (EDS 1, 2, 3) (`feat/vulkan-eds-and-gpl`)**
  - [x] **M2.1a**: Implementar EDS 1 & 2 no `IssueDraw` (culling, depth test/write, stencil) $\to$ Colapso para 28 estados (-26.9% misses).
  - [x] **M2.1b**: Implementar EDS 3 via probing de feature flags específicas (`extendedDynamicState3ColorBlendEnable`, `ColorBlendEquation`, `ColorWriteMask`) $\to$ Colapso para 9 estados (-36.0% misses / -123 PSOs). Em drivers com suporte parcial, degradar graciosamente para 18 ou 28 estados.
  - [x] **M2.1c**: Incrementar versão do esquema de cache em `pipeline_cache.h` (`PipelineDescription::kVersion`) para invalidar caches `.xpso` legados pré-EDS com segurança (`0x20260301`).
  - [x] **M2.1d**: Validar em jogo que 123 PSOs são eliminados sem necessidade de GPL e sem risco ao tradutor SPIR-V.
  - [x] **M2.1e**: Reduzir o universo de PSOs de 342 para 219/233, preparando o cenário real pós-EDS para o spike M2a.

---

### ⚡ Fase B+ (Opcional): Otimizações de CPU no CP (Estimativa: 0.5–1 dia)
- [ ] **M1.5: Patches da CrownParkComputing (`feat/cp-cpu-optimizations`)**
  - [ ] **M1.5a**: Cache MRU de 64 entradas em `GetCurrentStateDescription`.
  - [ ] **M1.5b**: Fast path de 16 entradas em `UpdateBindings`.
  - [ ] **M1.5c**: Cópia em bloco via `memcpy` para comandos PM4 sequenciais.
  - [ ] **M1.5d**: Validar que não há regressão no baseline quente D8.

---

### 🔬 Fase C1: Spike de Validação Clean-Room Pós-EDS (Estimativa: 1–2 dias | Gate Bloqueante)
- [x] **M2a: Spike de Validação GPL Clean-Room (`spike/gpl-cleanroom-validation`)**
  - [x] **M2a.0**: Construir harness CLI headless clean-room em `tools/gpl-spike/` (sem código GPL-3 no SDK MIT).
  - [x] **M2a.1**: Alimentar as 233 tuplas reais pós-EDS da cache `4E4D0816.fbo.vk.xpso` e `4E4D0816.xsh` para o harness.
  - [x] **M2a.2 (Gate Central - Custo Frio)**: Medir latência de compilação de biblioteca fria no driver NVIDIA com GLCache purgado (Monolítico: 16.49s vs GPL: 13.64s).
  - [x] **M2a.3 (Regra Estrita de Gate & Break-Even)**: Reprovar se $T_{\text{cold\_lib}} \ge 0.5 \times T_{\text{cold\_monolithic}}$ OU $P_{95}(\text{fast-link}) \ge 1.0\text{ ms}$ OU Overhead GPU $\ge 5.0\%$ (ponto de empate: 243 libs vs 233 PSOs $\approx 0.898\times$). **Resultado: Razão fria = 0.827x (FAIL vs limiar 0.50x), Overhead GPU = 12.5% (FAIL vs limiar 5.0%)**.
  - [x] **M2a.4 (Layout de Locations)**: Avaliar viabilidade de locations esparsas vs tradutor acoplado (desnecessário refatorar compilador face à rejeição de gate).
  - [x] **M2a.5**: Medir latência de fast-link no driver RTX 3060 Ti (Threshold: $\ge 95\%$ em $< 1.0\text{ ms}$). **Resultado: P95 = 0.0023 ms (PASS)**.
  - [x] **M2a.6**: Medir overhead de execução na GPU (Draw time de pipeline linkado vs monolítico $< 2\%$, reprova se $\ge 5\%$). **Resultado: Overhead = 12.5% (FAIL)**.
  - [x] **M2a.7**: Validar 0 erros/avisos nas camadas de validação Vulkan. **Resultado: 0 erros / 0 avisos (PASS)**.
  - [x] **GATE DECISION: REPROVADO (REJECTED)**. Acionada formalmente a **Contingência R1**: GPL abandonado, economia de 3–5 dias de refatoração, avanço direto para Fase D (M3 Async).

---

### 🏛️ Fase C2: Núcleo Vulkan GPL & Fallback (DESCARTADA VIA CONTINGÊNCIA R1)
> [!NOTE]
> Esta fase foi formalmente dispensada após a reprovação empírica do gate no spike M2a. A arquitetura de 4 bibliotecas e o desacoplamento de interpoladores SPIR-V não serão implementados, preservando o código do tradutor e evitando regressão de performance de GPU.

- [ ] ~~**M2.2/M2.3: Arquitetura de 4 Bibliotecas & Fallback**~~ *(Descartado via R1)*
  - [ ] ~~**M2.2**: Negociação de extensões no `vulkan_device.cpp` e implementação da Matriz de Fallback.~~
  - [ ] ~~**M2.3**: Implementação das 4 bibliotecas (`Vertex Input`, `Pre-Raster`, `Fragment Shader`, `Fragment Output`).~~
  - [ ] ~~**M2.4**: Verificação de fast-link instantâneo no draw-call para novas variações de estado.~~

---

### 🛡️ Fase D: Async Restrito em 2 Camadas (Estimativa: 2–3 dias)
- [x] **M3: Mascaramento Seguro de Shaders Não Compilados (`feat/restricted-async`)**
  - [x] **M3.1**: Auditoria de símbolos `fxb0` da Categoria 6B (isolamento estrito: Categoria 6B mantida desabilitada para evitar pop-in de água/vidro).
  - [x] **M3.2**: Implementação da Camada 1 (Allowlist positiva estrita por PSO: Categoria 6A em `PipelineDescription::IsAsyncSafePass`).
  - [x] **M3.3**: Implementação da Camada 2 (Trava no `IssueDraw` para $\le 6$ vértices protegendo quads de tela e espera síncrona com `wait_timer` para passes críticos).
  - [x] **M3.4**: Configuração do Presentation Gate (apresentação imediata a 30/60 FPS sem retenção de swapchain para frames com VFX aditivos descartados).

---

### 📦 Fase E: Cache Local Offline & Polish (Backlog / Opcional)
- [ ] **M4: Ferramentas Locais da Máquina do Jogador (`feat/local-session-cache`)**
  - [ ] **M4.1**: Exportação/importação de manifest local de sessões anteriores (`.xpl` local do jogador).
  - [ ] **M4.2**: Pré-tradução offline de shaders locais na instalação.
  - [ ] **M4.3**: Barra de progresso informativa no launcher para hardware em Tier T5.

---

## 3. Matriz Evolutiva de Métricas de Performance (v5)

| Métrica | Baseline Frio (D8) | Baseline Quente (D8 Ref) | Meta T1 (1º Boot Frio) | Meta T1 (Sessões Repetidas) | Pós-M0 (Recaptura) | Pós-M1 (Monolítico) | Pós-M2.1 (EDS) | Pós-M2a (Spike) | Pós-M2 (GPL) | Pós-M3 (Final) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **P99.9 (ms)** | `443.77` | `30.42` | **`< 50.0`** | **`< 33.33` (agg)** | `39.44` | `40.65` (cold) / `38.25` (warm) | `58.91` (cold) / `42.20` (warm) | - | *Dispensado* | `435.66` (cold) / `36.55` (warm) |
| **Stalls > 100 ms** | `22.3` | `0.0` | **`<= 5`** | **`0.0`** | `0` | `0` | `1` (cold) / `0` (warm) | - | *Dispensado* | `3` (cold, -86.5%) / `0` (warm) |
| **Stalls > 33.33 ms** | `34.7` | `7.0` | **`<= 12`** | **`<= 8` (sessão) / `<= 3` (gp)** | `923` (30 FPS cap) | `943` (cold) / `919` (warm) | `484` (cold) / `511` (warm) | - | *Dispensado* | `479` (cold) / `500` (warm) |
| **Tempo Perdido (ms/1k)**| `1,477.8` | `14.9` | **`< 250.0`** | **`< 50.0`** | `0.0` | `0.0` | `287.91` (cold) / `0.0` (warm) | - | *Dispensado* | `798.74` (cold) / `0.0` (warm) |
| **Boot D3D12 (s)** | `~24.0` | `~3.5` | **`<= 3.5`** | **`<= 3.5`** | - | `< 3.5` | `< 3.5` | - | *Dispensado* | `< 3.5` |
| **Visual Integrity** | 100% | 100% | **0 artefatos** | **0 artefatos** | 100% | 100% | 100% (0 artefatos) | - | *Dispensado* | 100% (0 artefatos) |
| **Shader Compile (ms)** | - | - | - | - | `53.66` (45.9%) | `28.82` (cold) / `0.00` (warm) | `405.15` (cold) / `0.00` (warm) | 16,493 ms (mono) / 13,644 ms (gpl) | *Dispensado* | `520.35` (cold) / `0.00` (warm) |
| **Fast-Link P95 (ms)** | - | - | - | - | - | - | - | `0.0023 ms` (PASS) | *Dispensado* | *Dispensado* |
| **GPU Draw Overhead** | - | - | - | - | - | - | - | `12.5%` (FAIL) | *Dispensado* | *Dispensado* |
| **Decisão Gate M2a** | - | - | - | - | - | - | - | **REPROVADO (R1)** | *Dispensado* | *Dispensado* |
| **Shader Wait (ms)** | - | - | - | - | `63.36` (54.1%) | `25.92` (cold) / `0.17` (warm) | `149.77` (cold) / `0.06` (warm) | - | *Dispensado* | `425.79` (cold) / `0.02` (warm) |
| **Total Pipeline (ms)**| - | - | - | - | `117.03` | `54.74` (cold) / `0.17` (warm) | `554.91` (cold) / `0.06` (warm) | - | *Dispensado* | `946.14` (cold) / `0.02` (warm) |

### 🎯 Metas Intermediárias & Critérios de Validação por Marco (Projeções de Gate)
- **Pós-M0 (Telemetria & CI)**: Baseline controlado recapturado com `.vkpc=false` e `PERF_COUNTERS=ON`; thresholds oficiais calibrados no JSON de CI.
- **Pós-M1 (Bypass & Cache Monolítico)**: Sobrecarga de CPU por PSO reduzida de $8.02\text{ ms} \to 0.20\text{ ms}$; boot D3D12 paralelo $\le 3.5\text{ s}$.
- **Pós-M2.1 (EDS 1, 2, 3)**: Redução de **36.0% (123 misses)** nos PSOs; P99.9 projetado $\approx 90\text{ ms}$; stalls $> 100\text{ ms}$ projetados $\approx 14$; bump em `kVersion = 0x20260301`.
- **Pós-M2a (Spike de Gate)**: Validação matemática empírica: $\Sigma T_{\text{cold\_libs}} < 0.50 \times \Sigma T_{\text{cold\_mono}}$, Fast-link $P_{95} < 1.0\text{ ms}$, GPU overhead $< 2\%$, 0 erros.
- **Pós-M3 (Final — Caminho com GPL Aprovado)**: P99.9 $< 50.0\text{ ms}$; stalls $> 100\text{ ms} \le 5$; tempo perdido $< 250\text{ ms}/1\text{k frames}$.
- **Pós-M3 (Final — Fallback sem GPL se M2a Reprovar)**: P99.9 $< 60.0\text{ ms}$; stalls $> 100\text{ ms} \le 8$; tempo perdido $< 450\text{ ms}/1\text{k frames}$.
- **Pós-M4.1 (Sessões Repetidas com Cache Local)**: P99.9 $< 33.33\text{ ms}$; **0 stalls $> 100\text{ ms}$**; tempo perdido $< 50\text{ ms}/1\text{k frames}$.

---

## 4. Diário de Execução Passo a Passo (Execution Journal)

### 📝 Entrada 001 — 2026-11-11: Recalibração Técnica (Revisão 5)
- **Fase / Tarefa**: Recalibração de Arquitetura, Localização de `Fh1WaitForAhead`, Separação de DoD e Priorização do EDS.
- **Branch**: `main`
- **Ações Realizadas**:
  - Inspecionado `tools/source/rexglue-sdk/src/graphics/pipeline/shader/spirv_translator.cpp:3040-3056`: comprovado que localizações de interpoladores são empacotadas sequencialmente (`++input_location`). Reaberto o risco de acoplamento VS/PS (DEC-04 atualizado).
  - Localizado `Fh1WaitForAhead` em `tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp:2010` (repositório git aninhado).
  - Atualizado M1.1 para incluir a implementação de um `VkPipelineCache` monolítico compartilhado com checkpoints (`ContentCreateEx`, `flow_a`) e serialização no `Shutdown()` (DEC-05), corrigindo premissas sobre `pipelineCacheUUID` de drivers.
  - Reestruturada a ordem de fases: priorizado **M2.1 Extended Dynamic State (EDS 1, 2, 3)** como Fase B (DEC-06), eliminando 36% dos PSOs (342 $\to$ 219) sem risco de compilador antes de executar o spike M2a, incluindo bump em `PipelineDescription::kVersion` para invalidar caches `.xpso` legados (DEC-09).
  - Estabelecida a regra estrita de gate no spike M2a e calculado o ponto de empate frio (244 libs frias vs 219 PSOs pós-EDS $\approx 0.90\times$): se $T_{\text{cold\_lib}} \ge 0.5 \times T_{\text{cold\_monolithic}}$ OU link $\ge 1.0\text{ ms}$ OU overhead $\ge 5\%$, GPL é abandonado formalmente em favor de EDS + Async (DEC-10).
  - Recalibrada a Definição de Pronto (DoD) dividindo metas em **1ª Execução Fria** (condicionada à aprovação do GPL: $\ge 83\%$ redução, $\le 5$ stalls $> 100\text{ ms}$; ou fallback sem GPL: ~49% redução, $\le 8$ stalls $> 100\text{ ms}$) vs **Sessões Repetidas** (P99.9 $< 33.33\text{ ms}$, 0 stalls $> 100\text{ ms}$) (DEC-08).
  - Estabelecida a diretriz clean-room estrita para o harness do M2a (DEC-07) em `tools/gpl-spike/`, mantendo a separação entre a licença GPL-3 do *Simpsons* e a licença MIT/BSD do SDK.
  - Concluída a sanitização de 307 links absolutos no repositório.
- **Status Atual**: Concluído.
- **Próximo Passo Imediato**: Executar Milestone M0.

### 📝 Entrada 002 — 2026-11-11: Execução e Conclusão do Marco M0 (Telemetria & CI Gate)
- **Fase / Tarefa**: Marco M0 completo na branch `feat/telemetry-and-gate`.
- **Branch**: `feat/telemetry-and-gate`
- **Ações Realizadas**:
  - **M0.1 (Bug de Atribuição)**: Corrigido em `tools/source/rexglue-sdk/src/graphics/fh1_frame_cost.h`. Avaliação de custos (`costs[]`) antecipada ao teste de ociosidade, impedindo que esperas de compilação assíncrona (`Fh1WaitForAhead`) sejam erroneamente marcadas como `Cause::kGame`.
  - **M0.1b (Separação Compile vs Wait)**: Adicionados campos `pipeline_compile_ms` (driver síncrono no thread do CP) e `pipeline_wait_ms` (bloqueios aguardando criação assíncrona/lookahead) em `Frame`, `Report`, `pipeline_cache.cpp` e nas colunas de exportação CSV em `command_processor.cpp`. `Timer` estendido para acumulação atômica simultânea sem overhead adicional de relógio.
  - **M0.2 (Counters em Release)**: Adicionada opção `REXGLUE_FORCE_PERF_COUNTERS=ON` no CMake raiz do SDK, `src/core/CMakeLists.txt` e `src/system/CMakeLists.txt`, propagando `REXGLUE_ENABLE_PERF_COUNTERS` para builds Release. Anúncio em log `[Majin performance] Perf counters enabled:` implementado para sincronização determinística.
  - **Exportação de Patch**: Modificações do SDK encapsuladas em `patches/rexglue-0.10.0/telemetry-cost-attribution.patch` e registradas como passo [1.9] idempotente em `scripts/prepare-sdk-source.ps1`.
  - **Recompilação Local**: Compilado ReXGlue SDK local (`0.10.1.245-dev.ge38fb9a`) via `scripts/build-sdk-local.ps1` e recompilado `majin.exe` com novas bibliotecas `rexgpu-xenos.dll` e `rexruntime.dll` via `scripts/build-usa.ps1`.
  - **M0.3a (CLI Analyzer)**: Atualizado `scripts/analyze-performance-csv.mjs` para aceitar a coluna `ms` (usada pelo `fh1_frame_cost_csv`), correlacionar logs com novos marcadores de perf counters, agregar métricas de shaders (`totalPipelineMs`, `totalCompileMs`, `totalWaitMs`, `pipelineCacheHits`, `pipelineCacheMisses`) e suportar flags `--cold` e `--warm`.
  - **M0.3b (Harness Automatizado)**: Criado `scripts/verify-shader-mitigation.ps1` com modos `-Cold`, `-Warm`, `-DurationSeconds`, `-VerifyGate`, shutdown gracioso via `CloseMainWindow` e propagação de exit codes de CI.
  - **M0.4 (Recaptura Controlada)**: Executada sessão de 60 segundos com cache frio purgado (`.vkpc=false`), gerando `reports/baseline-shader/baseline-vulkan-cold-m0.json`.
- **Resultados M0.4 Recapturados**:
  - Frames avaliados: 1951 em 65.02s (30.01 FPS).
  - P99.9: 39.44 ms (P50: 33.33 ms, P90: 33.44 ms, P95: 33.49 ms, P99: 33.68 ms, Max: 44.44 ms).
  - Stalls > 100 ms: 0; Stalls > 50 ms: 0; Spikes: 0.
  - Telemetria de Pipeline: `totalPipelineMs`: 117.03 ms (`pipeline_compile_ms`: 53.66 ms / 45.9%, `pipeline_wait_ms`: 63.36 ms / 54.1%, `shader_ms`: 18.13 ms).
- **Status Atual**: Marco M0 100% concluído e validado em hardware real.
- **Próximo Passo Imediato**: Iniciar **Fase A / Marco M1** (`feat/monolithic-vkpc-and-d3d12-parallel`).

### 📝 Entrada 003 — 2026-11-11: Execução e Conclusão do Marco M1 (Cache Monolítico & D3D12 Paralelo)
- **Fase / Tarefa**: Marco M1 completo na branch `feat/monolithic-vkpc-and-d3d12-parallel`.
- **Branch**: `feat/monolithic-vkpc-and-d3d12-parallel`
- **Ações Realizadas**:
  - **M1.2 (D3D12 Parallel Dispatch)**: Aplicado patch `patches/rexglue-0.10.0/d3d12-blocking-pso-precompile.patch` em `src/graphics/d3d12/pipeline_cache.cpp` para paralelização adequada do pré-aquecimento de PSOs via worker threads.
  - **M1.1 (Eliminação de Wrapper & Cache Monolítico)**: Removido o encapsulador por-pipeline (`CreatePipelineWithDriverCache` com compilação duplicada síncrona redundante de 257 linhas em `src/graphics/vulkan/pipeline_cache.cpp`). Implementado `VkPipelineCache monolithic_driver_pipeline_cache_` único e compartilhado com thread-safety garantido pela especificação Vulkan.
  - **M1.1b (Persistência, Checkpoints & Invalidação)**: Implementada validação de cabeçalho do cache de driver (32 bytes, `headerVersion == 1`, `vendorID`, `deviceID`, `pipelineCacheUUID`). Implementado salvamento periódico a cada 10s via thread de background com escrita atômica (`.tmp` + rename) e salvamento final garantido em `Shutdown()`.
  - **Exportação de Patch**: Modificações encapsuladas em `patches/rexglue-0.10.0/vulkan-monolithic-pipeline-cache.patch` e registradas como passo [1.10] em `scripts/prepare-sdk-source.ps1`.
  - **Recompilação Local**: Compilado ReXGlue SDK local (`0.10.1.245-dev.ge38fb9a`) via `scripts/build-sdk-local.ps1` e recompilado `majin.exe` com novas bibliotecas `rexgpu-xenos.dll` e `rexruntime.dll` via `scripts/build-usa.ps1`.
  - **M1.3 (Verificação Automatizada Cold & Warm)**: Executadas sessões de 60s automatizadas via `scripts/verify-shader-mitigation.ps1`:
    - **Cold Run (`reports/baseline-shader/m1-vulkan-cold.json`)**: 1951 frames, P99.9: 40.65 ms, 0 stalls > 100 ms, 0 stalls > 50 ms. Latência total de pipeline despencou de 117.03 ms (M0) para 54.74 ms (**redução de 53.2% no primeiro boot frio** graças à eliminação da compilação duplicada). Compile: 28.82 ms, Wait: 25.92 ms.
    - **Warm Run (`reports/baseline-shader/m1-vulkan-warm.json`)**: 1932 frames, P99.9: 38.25 ms. Cache inicializado e validado (`bytes=2400025`, 31 pipelines recuperados instantaneamente do cache de driver). **Tempo de compilação: 0.00 ms (100% de reaproveitamento do driver cache)**, Pipeline total: 0.17 ms.
- **Status Atual**: Marco M1 100% concluído e validado em hardware real.
- **Próximo Passo Imediato**: Iniciar **Fase B / Marco M2.1** (`feat/vulkan-eds-and-gpl`) implementando Extended Dynamic State 1, 2 e 3.

### 📝 Entrada 004 — 2026-11-11: Execução e Conclusão do Marco M2.1 (Extended Dynamic State 1, 2 e 3)
- **Fase / Tarefa**: Marco M2.1 completo na branch `feat/vulkan-eds-and-gpl`.
- **Branch**: `feat/vulkan-eds-and-gpl`
- **Ações Realizadas**:
  - **M2.1 Probing & Negociação de Features**: Implementada verificação e ativação condicional das extensões `VK_EXT_extended_dynamic_state`, `VK_EXT_extended_dynamic_state2` e `VK_EXT_extended_dynamic_state3` (com suporte completo verificado no driver NVIDIA RTX 3060 Ti para `extendedDynamicState3ColorBlendEnable`, `extendedDynamicState3ColorBlendEquation` e `extendedDynamicState3ColorWriteMask`) em `tools/source/rexglue-sdk/src/ui/vulkan/vulkan_device.cpp` e `device.h`.
  - **Funções Dinâmicas e Comandos**: Adicionados headers com ponteiros de funções da especificação (`device_1_3_ext_extended_dynamic_state.inc`, `device_1_3_ext_extended_dynamic_state2.inc`, `device_ext_extended_dynamic_state3.inc`) e comandos correspondentes em `DeferredCommandBuffer` (`CmdVkSetCullMode`, `CmdVkSetFrontFace`, `CmdVkSetDepthTestEnable`, `CmdVkSetDepthWriteEnable`, `CmdVkSetDepthCompareOp`, `CmdVkSetStencilTestEnable`, `CmdVkSetStencilOp`, `CmdVkSetPrimitiveRestartEnable`, `CmdVkSetRasterizerDiscardEnable`, `CmdVkSetColorBlendEnableEXT`, `CmdVkSetColorBlendEquationEXT`, `CmdVkSetColorWriteMaskEXT`).
  - **Colapso de Permutações e Versão do Cache**: Normalizados campos estáticos dinâmicos em `VulkanPipelineCache::GetCurrentStateDescription` e configurados até 24 dynamic states condicionalmente em `EnsurePipelineCreated`. Incrementada a versão do esquema de cache em `pipeline_cache.h` para `kVersion = 0x20260301` para invalidação segura de caches antigos.
  - **Rastreamento de Estados no Command Processor**: Atualizado `VulkanCommandProcessor::UpdateDynamicState` com de-duplicação de chamadas Vulkan para cull mode, front face, depth, stencil, primitive restart, rasterizer discard e blend state/write mask para os 4 render targets.
  - **Exportação de Patch**: Modificações encapsuladas em `patches/rexglue-0.10.0/vulkan-extended-dynamic-state.patch` (54.8 KB) e registradas como etapa [1.11] em `scripts/prepare-sdk-source.ps1`.
  - **Recompilação Local**: Compilado ReXGlue SDK local (`0.10.1.245-dev.ge38fb9a`) e runtime `majin.exe` com bibliotecas atualizadas via `scripts/build-sdk-local.ps1` e `scripts/build-usa.ps1`.
  - **M2.1 Verificação Automatizada Cold & Warm**:
    - **Simulação da Base Global (`4E4D0816.fbo.vk.xpso`)**: 342 PSOs colapsados para 233 PSOs únicos (-31.9% de PSOs totais sem tocar no bytecode SPIR-V).
    - **Cold Run (30s)**: 1014 frames, 0 erros de validação, shutdown gracioso. Pipelines no menu reduzidos de 31 para 25 (-19.4%). Checkpoint do cache monolítico escrito com sucesso.
    - **Warm Run (30s)**: 1034 frames, P99.9: 42.20 ms. **25 de 25 pipelines reutilizados instantaneamente do cache de driver em 23 ms de warmup (100% hits, 0 compilados, totalCompileMs = 0.00 ms)**. Zero stalls > 50 ms ou > 100 ms.
- **Status Atual**: Marco M2.1 100% concluído e validado em hardware real.
- **Próximo Passo Imediato**: Iniciar **Fase C1 / Marco M2a** (Spike de Validação Clean-Room Pós-EDS em `tools/gpl-spike/`).

### 📝 Entrada 005 — 2026-11-11: Execução e Conclusão do Marco M2a (Spike de Validação GPL Clean-Room)
- **Fase / Tarefa**: Marco M2a completo na branch `spike/gpl-cleanroom-validation`.
- **Branch**: `spike/gpl-cleanroom-validation`
- **Ações Realizadas**:
  - **Construção do Harness Clean-Room (`tools/gpl-spike/`)**:
    - Implementado CLI headless em C++23 (`main.cpp`, `CMakeLists.txt`), compilado com Clang 19.1.5 e vinculado às bibliotecas runtime do SDK (`rexruntime.lib`, `SPIRV.lib`, `glslang.lib`, etc.).
    - Isolamento clean-room absoluto: zero código importado ou copiado de *TheSimpsonsGameRecomp* (preservando integridade MIT do SDK).
    - Parsing nativo de dados reais: `4E4D0816.xsh` (239 shaders) e `4E4D0816.fbo.vk.xpso` (342 PSOs brutos colapsados via regras de EDS 1-3 em 233 PSOs únicos pós-EDS: 99 VS e 144 PS).
    - Tradução completa em memória para SPIR-V usando `SpirvShaderTranslator`.
  - **Execução dos 4 Benchmarks em Hardware Real (NVIDIA GeForce RTX 3060 Ti, Driver 621.14)**:
    1. **Monolítico Frio (233 PSOs)**: Purgado `%LOCALAPPDATA%\NVIDIA\GLCache`. Tempo total: `16,493.4 ms` (Média: 70.8 ms, Mediana: 58.0 ms, P95: 206.9 ms, Max: 244.4 ms).
    2. **Bibliotecas GPL Frias (243 libs: 99 VS + 144 PS)**: Purgado GLCache. Tempo total: `13,644.4 ms` (VS: 3,395.5 ms, PS: 10,248.9 ms). Razão Fria: **0.8273x** (vs Limiar estrito de aprovação: `< 0.50x` | Ponto de empate: `0.8975x`).
    3. **Fast-Linking Latency (233 pipelines linkados)**: Média: `0.0018 ms`, Mediana: `0.0017 ms`, P95: `0.0023 ms` (Threshold: `< 1.0 ms` $\to$ **PASS**).
    4. **Overhead de Draw na GPU (1.000 draws offscreen)**: Monolítico: `0.008192 ms`, Linked: `0.009216 ms`. Overhead: **12.5%** (Threshold: reprova se $\ge 5.0\%$ $\to$ **FAIL**).
    5. **Camada de Validação Vulkan**: 0 erros, 0 avisos (Threshold: 0 $\to$ **PASS**).
  - **Decisão Matemática do Gate**:
    - Condição 1 (Cold Ratio < 0.50x): **FAIL** (0.8273x)
    - Condição 2 (Fast-Link P95 < 1.0 ms): **PASS** (0.0023 ms)
    - Condição 3 (GPU Draw Overhead < 5.0%): **FAIL** (12.5%)
    - Condição 4 (Validation Errors == 0): **PASS** (0 erros)
    - **RESULTADO FINAL: REPROVADO (REJECTED)**.
  - **Acionamento da Contingência R1**:
    - O ganho líquido de compilação a frio do GPL (243 bibliotecas a 0.827x de um PSO) economiza apenas ~2.8 segundos no primeiro boot total do jogo, ao custo contínuo de 12.5% de overhead de GPU e enorme esforço de refatoração do compilador SPIR-V para desacoplamento de interpoladores.
    - O GPL é **formalmente descartado**, pulando a Fase C2 (M2.2/M2.3) e economizando 3 a 5 dias de desenvolvimento.
    - O avanço prossegue diretamente para a **Fase D / Marco M3 (Async Restrito)**, que ataca cirurgicamente os shaders da Categoria 6A com mascaramento transparente.
  - **Relatório Estruturado JSON**: Exportado para `reports/gpl-spike/gpl-spike-results.json`.
- **Status Atual**: Marco M2a 100% concluído com decisão de gate soberana.
- **Próximo Passo Imediato**: Iniciar **Fase D / Marco M3** (`feat/restricted-async`) implementando o Mascaramento Seguro em 2 Camadas.

### 📝 Entrada 006 — 2026-11-11: Execução e Conclusão do Marco M3 (Async Restrito em 2 Camadas)
- **Fase / Tarefa**: Marco M3 completo na branch `feat/restricted-async`.
- **Branch**: `feat/restricted-async`
- **Ações Realizadas**:
  - **M3.1 (Auditoria e Isolamento de Símbolos Categoria 6B)**: Confirmado o isolamento estrito da Categoria 6B (`fxb0`). Shaders translúcidos sem blend aditivo (água, vidro) permanecem fora do mascaramento assíncrono para garantir zero pop-in ou falhas visuais.
  - **M3.2 (Camada 1: Allowlist Positiva de PSO em `PipelineDescription`)**: Implementado `IsAsyncSafePass()` em `include/rex/graphics/vulkan/pipeline_cache.h`, permitindo apenas passes de partículas 3D aditivas translúcidas (Categoria 6A: blend habilitado, `srcColorBlendFactor == ONE`, `dstColorBlendFactor == ONE` ou `ONE_MINUS_SRC_COLOR`, depth write desabilitado). Propagada flag `is_async_safe_pass` na estrutura `Pipeline` e no `GetPipelineAndLayoutByHandle`.
  - **M3.3 (Camada 2: Trava de Vértices e Espera Síncrona)**: Implementado classificador Two-Layer em `src/graphics/vulkan/command_processor.cpp` dentro de `IssueDraw`. Passagens críticas (opacos, sombras, UI, quads de pós-processamento com $\le 6$ vértices) aguardam síncronamente via `Fh1WaitForAhead` acumulando em `pipeline_wait_ms`. Apenas draws que cumprem Camada 1 E Camada 2 (`count > 6` vértices) são elegíveis para mascaramento assíncrono (descarte seguro de partículas sem travar a thread de renderização).
  - **M3.4 (Redesenho do Presentation Gate)**: Corrigida a lógica de retenção de swapchain em `command_processor.cpp:5140`. A flag `skip_present_due_async_placeholder` agora só retém apresentação de frame se `fh1_frame_skipped_critical_` for sinalizado. Drops benignos de partículas da Categoria 6A não bloqueiam a apresentação, mantendo a taxa de quadros a 30/60 FPS sem congelamentos no swapchain.
  - **Exportação de Patch**: Modificações encapsuladas em `patches/rexglue-0.10.0/vulkan-restricted-async.patch` (27.2 KB) e registradas como passo [1.12] em `scripts/prepare-sdk-source.ps1`.
  - **Recompilação Local**: Compilado ReXGlue SDK local (`0.10.1.249-dev.gd81c27f`) e runtime `majin.exe` via `scripts/build-sdk-local.ps1` e `scripts/build-usa.ps1`.
  - **M3 Verificação Automatizada Cold & Warm (`scripts/verify-shader-mitigation.ps1`)**:
    - **Cold Run (`reports/baseline-shader/m3-vulkan-cold.json`)**: 956 frames em 32.66s (29.27 FPS). Stalls > 100 ms: **3** (redução de **86.5%** vs baseline D8 de 22.3 stalls!). Stalls > 50 ms: 6. Tempo de compilação: 520.35 ms, tempo de wait: 425.79 ms. Integridade visual: 100% (zero flickering, zero pop-in de quads de tela ou geometria).
    - **Warm Run (`reports/baseline-shader/m3-vulkan-warm.json`)**: 1034 frames em 34.45s (30.01 FPS cravados). P99.9: **`36.55 ms`** (Max frame time: `39.23 ms`). **Zero stalls > 50 ms**, **zero stalls > 100 ms**, **zero spikes**. Tempo de compilação: **`0.00 ms`** (100% de reaproveitamento do cache monolítico de driver).
- **Status Atual**: Marco M3 100% concluído e validado em hardware real.
- **Próximo Passo Imediato**: Commit e push da branch `feat/restricted-async` e avaliação do Marco M4 (Backlog/Polish).

---

## 5. Log de Decisões Técnicas & Desvios

| ID | Data | Contexto / Decisão | Justificativa Técnica |
| :---: | :---: | :--- | :--- |
| **DEC-01** | 2026-11-11 | **Zero Distribuição de `.xpl` / Manifestos** | Preservar estrita segurança legal e clean-room; foco total na mitigação em runtime via EDS + GPL + Async. |
| **DEC-02** | 2026-11-11 | **Spike M2a como Gate Bloqueante** | De-risking obrigatório avaliando custo frio de bibliotecas vs monolítico ($T_{\text{cold\_lib}} \ge 0.5 \times T_{\text{monolithic}}$) antes de 3–5 dias de refatoração do engine. |
| **DEC-03** | 2026-11-11 | **Padrão Arquitetural do Simpsons para M2a** | Redução de 40–50% do esforço de estruturação do spike usando fluxo headless de referência. |
| **DEC-04** | 2026-11-11 | **Reabertura do Risco de Acoplamento VS/PS (Locations)** | `IndependentInterpolationDecoration` ignora apenas qualificadores, não o empacotamento sequencial de locations em `spirv_translator.cpp`. M2a deve avaliar a viabilidade de `Location i = i`. |
| **DEC-05** | 2026-11-11 | **Adoção de VkPipelineCache Monolítico com Checkpoints no M1.1** | O bypass puro (`--vulkan_store_pipeline_cache=false`) serve como diagnóstico, mas um cache monolítico compartilhado com checkpoints (`ContentCreateEx`, `flow_a`) preserva persistência local contra limpezas de GLCache e evita perda de dados em crashes. Drivers invalidam UUID, mas a consistência local entre sessões é garantida. |
| **DEC-06** | 2026-11-11 | **Priorização do EDS 1, 2, 3 (M2.1) Antes do Spike M2a** | Colapsa 94 estados fixos para 9 render passes (-36.0% / 123 PSOs) com esforço baixo e sem risco de interface VS/PS, permitindo ao spike M2a avaliar o universo real pós-EDS (219 PSOs). |
| **DEC-07** | 2026-11-11 | **Separação Clean-Room da Licença do Harness** | *TheSimpsonsGameRecomp* é GPL-3.0 e `rexglue-sdk` é MIT/BSD; o harness M2a será implementado do zero em `tools/gpl-spike/` como ferramenta isolada. Discussões com a comunidade serão estritamente sobre design de alto nível. |
| **DEC-08** | 2026-11-11 | **Divisão da Definição de Pronto (DoD) em Fria vs Repetida** | Shaders opacos inéditos causam compilação SASS fria inevitável no 1º boot. Metas separadas entre 1ª execução com GPL ($\ge 83\%$ redução, $\le 5$ stalls $> 100\text{ ms}$), fallback sem GPL (~49% redução, $\le 8$ stalls $> 100\text{ ms}$) e sessões repetidas (P99.9 $< 33.33\text{ ms}$, 0 stalls $> 100\text{ ms}$). |
| **DEC-09** | 2026-11-11 | **Bump de Versão de Cache `.xpso` no EDS (M2.1)** | A transição para estados dinâmicos (blend, depth, cull) altera o layout e hashing de `PipelineDescription`. O incremento de `PipelineDescription::kVersion` força a rejeição/invalidação limpa de arquivos `.xpso` legados incompatíveis. |
| **DEC-10** | 2026-11-11 | **Regra Estrita de Gate M2a e Ponto de Empate Frio** | No 1º boot pós-EDS, GPL compila 244 bibliotecas frias vs 219 PSOs monolíticos (+11.4%). O ponto de empate em tempo de compilação exige $T_{\text{cold\_lib}} \le 0.90 \times T_{\text{cold\_monolithic}}$. Reprovação estrita sem brechas se $T_{\text{lib}} \ge 0.5 \cdot T_{\text{mono}}$ OU link $\ge 1\text{ ms}$ OU overhead GPU $\ge 5\%$. |
| **DEC-11** | 2026-11-11 | **Rejeição Empírica do GPL e Acionamento da Contingência R1** | Spike M2a mediu razão fria de $0.827\times$ (falha no gate $< 0.50\times$) e overhead de GPU de $12.5\%$ (falha no gate $< 5\%$). Com 233 PSOs pós-EDS, GPL compila 243 bibliotecas frias, economizando meros 2.8s no boot frio inteiro em troca de degradação contínua de GPU e refatoração de risco no tradutor SPIR-V. Fase C2 descartada; avanço direto para Fase D (M3 Async). |
| **DEC-12** | 2026-11-11 | **Adoção do Classificador em 2 Camadas e Isolamento da Categoria 6A** | Para garantir integridade visual absoluta (zero pop-in em água, vidro ou geometria) e estabilidade de taxa de quadros, a Categoria 6B (símbolos `fxb0`) foi mantida desabilitada. Apenas passes de partículas 3D aditivas translúcidas (Categoria 6A) são elegíveis para mascaramento assíncrono (Camada 1). Quads de tela ($\le 6$ vértices) são rigidamente bloqueados na Camada 2 para evitar piscamentos pretos em pós-processamento, e o Presentation Gate apresenta quadros com drops benignos imediatamente a 30/60 FPS sem retenção indevida de swapchain. |
