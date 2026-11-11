# Shader Stutter Mitigation: Execution Tracker & Step-by-Step Progress Log

> **Target Title**: *Majin and the Forsaken Kingdom* (Title ID `4E4D0816`)  
> **Master Architecture Document**: [`docs/SHADER_IMPLEMENTATION_PLAN.md`](SHADER_IMPLEMENTATION_PLAN.md)  
> **Created**: 2026-11-11  
> **Status**: **READY TO EXECUTE PHASE A (MILESTONE M0)**  

---

## 1. Executive Status Dashboard

| Metric / Dimension | Current State | Notes |
| :--- | :--- | :--- |
| **Current Active Phase** | **Fase A (Preparação & Ganhos Rápidos)** | Pronto para iniciar Milestone M0. |
| **Current Active Milestone** | **`M0: Telemetry Integrity & CI Gate`** | Telemetria no SDK + Script de automação CI. |
| **Current Working Branch** | `main` | Próxima branch de trabalho: `feat/telemetry-and-gate` |
| **Overall Execution Progress** | **7% Completed (1 / 15 Subtasks)** | `analyze-performance-csv.mjs` pronto e validado. |
| **GPL Spike Gate Status (M2a)** | ⏳ **Pending Phase A completion** | Spike clean-room aguarda conclusão de M0 e M1. |
| **Clean-Room Compliance** | 🟢 **100% Verified** | Zero assets/manifests distribuídos; harness externo mantido clean-room em `tools/`. |

---

## 2. Milestone Execution Roadmap & Checklist

### 🏁 Fase A: Telemetria & Ganhos Imediatos (Estimativa: 1–2 dias)
- [ ] **M0: Telemetria & CI Gate (`feat/telemetry-and-gate`)**
  - [ ] **M0.1**: Corrigir bug de atribuição em `fh1_frame_cost.h` (`MainCause` culpando o jogo em vez de `Fh1WaitForAhead`).
  - [ ] **M0.1b**: Separar diagnósticos no CSV: `pipeline_compile_ms` (SASS síncrono) vs `pipeline_wait_ms` (bloqueio de thread em `Fh1WaitForAhead`).
  - [ ] **M0.2**: Habilitar `REXGLUE_ENABLE_PERF_COUNTERS` na configuração Release-Diagnostic do CMake.
  - [x] **M0.3a**: Entregar CLI autônomo `scripts/analyze-performance-csv.mjs` (suporte a slicing de gameplay e agregação multi-run).
  - [ ] **M0.3b**: Entregar suite de automação `scripts/verify-shader-mitigation.ps1` com exit codes estritos de CI.
  - [ ] **M0.4**: Recapturar baseline oficial sob ambiente controlado com contadores ativados (salvar JSON de referência).
- [ ] **M1: Monolithic Shared VkPipelineCache & D3D12 Multi-Thread (`feat/monolithic-vkpc-and-d3d12-parallel`)**
  - [ ] **M1.1a (Diagnóstico)**: Validar baseline imediato com `--vulkan_store_pipeline_cache=false` (8.02 $\to$ 0.20 ms/PSO).
  - [ ] **M1.1b (Persistência)**: Implementar `VkPipelineCache` monolítico compartilhado (carregado no boot, salvo em checkpoints periódicos e no `Shutdown()`).
  - [ ] **M1.2**: Reaplicar patch de compilação paralela D3D12 (`d3d12-blocking-pso-precompile.patch`).
  - [ ] **M1.3**: Executar verificação automatizada (`verify-shader-mitigation.ps1`) e registrar métricas.

---

### 🏛️ Fase B: Prioridade Imediata — Extended Dynamic State (Estimativa: 2–3 dias)
- [ ] **M2.1: Extended Dynamic State (EDS 1, 2, 3) (`feat/vulkan-eds-and-gpl`)**
  - [ ] **M2.1a**: Implementar EDS 1 & 2 no `IssueDraw` (culling, depth test/write, stencil) $\to$ Colapso para 28 estados (-26.9% misses).
  - [ ] **M2.1b**: Implementar EDS 3 via probing de feature flags específicas (`extendedDynamicState3ColorBlendEnable`, `ColorBlendEquation`, `ColorWriteMask`) $\to$ Colapso para 9 estados (-36.0% misses / -123 PSOs). Em drivers com suporte parcial, degradar graciosamente para 18 ou 28 estados.
  - [ ] **M2.1c**: Incrementar versão do esquema de cache em `pipeline_cache.h` (`PipelineDescription::kVersion`) para invalidar caches `.xpso` legados pré-EDS com segurança.
  - [ ] **M2.1d**: Validar em jogo que 123 PSOs são eliminados sem necessidade de GPL e sem risco ao tradutor SPIR-V.
  - [ ] **M2.1e**: Reduzir o universo de PSOs de 342 para 219, preparando o cenário real pós-EDS para o spike M2a.

---

### ⚡ Fase B+ (Opcional): Otimizações de CPU no CP (Estimativa: 0.5–1 dia)
- [ ] **M1.5: Patches da CrownParkComputing (`feat/cp-cpu-optimizations`)**
  - [ ] **M1.5a**: Cache MRU de 64 entradas em `GetCurrentStateDescription`.
  - [ ] **M1.5b**: Fast path de 16 entradas em `UpdateBindings`.
  - [ ] **M1.5c**: Cópia em bloco via `memcpy` para comandos PM4 sequenciais.
  - [ ] **M1.5d**: Validar que não há regressão no baseline quente D8.

---

### 🔬 Fase C1: Spike de Validação Clean-Room Pós-EDS (Estimativa: 1–2 dias | Gate Bloqueante)
- [ ] **M2a: Spike de Validação GPL Clean-Room (`spike/gpl-cleanroom-validation`)**
  - [ ] **M2a.0**: Construir harness CLI headless clean-room em `tools/gpl-spike/` (sem código GPL-3 no SDK MIT).
  - [ ] **M2a.1**: Alimentar as 219 tuplas reais pós-EDS da cache `4E4D0816.fbo.vk.xpso` para o harness.
  - [ ] **M2a.2 (Gate Central - Custo Frio)**: Medir latência de compilação de biblioteca fria no driver NVIDIA com GLCache purgado.
  - [ ] **M2a.3 (Regra Estrita de Gate & Break-Even)**: Reprovar se $T_{\text{cold\_lib}} \ge 0.5 \times T_{\text{cold\_monolithic}}$ OU $P_{95}(\text{fast-link}) \ge 1.0\text{ ms}$ OU Overhead GPU $\ge 5.0\%$ (ponto de empate: 244 libs vs 219 PSOs $\approx 0.90\times$). Se reprovado $\to$ Abandonar GPL formalmente (Contingência R1) e avançar direto para M3 Async (economia de 3–5 dias).
  - [ ] **M2a.4 (Layout de Locations)**: Avaliar viabilidade de `Location i = i` 1:1 vs pareamento fixo `(vsHash, vsMod)`.
  - [ ] **M2a.5**: Medir latência de fast-link no driver RTX 3060 Ti (Threshold: $\ge 95\%$ em $< 1.0\text{ ms}$).
  - [ ] **M2a.6**: Medir overhead de execução na GPU (Draw time de pipeline linkado vs monolítico $< 2\%$).
  - [ ] **M2a.7**: Validar 0 erros/avisos nas camadas de validação Vulkan.
  - [ ] **GATE DECISION**: Se aprovado $\to$ Avançar para Fase C2 (GPL). Se reprovado $\to$ Avançar direto para Fase D (M3 Async).

---

### 🏛️ Fase C2: Núcleo Vulkan GPL & Fallback (Estimativa: 3–5 dias | Condicionado ao Gate M2a)
- [ ] **M2.2/M2.3: Arquitetura de 4 Bibliotecas & Fallback (6 Entradas: 5 Tiers + T2b) (`feat/vulkan-eds-and-gpl`)**
  - [ ] **M2.2**: Negociação de extensões no `vulkan_device.cpp` e implementação da Matriz de Fallback (5 Tiers + sub-tier T2b).
  - [ ] **M2.3**: Implementação das 4 bibliotecas (`Vertex Input`, `Pre-Raster`, `Fragment Shader`, `Fragment Output`).
  - [ ] **M2.4**: Verificação de fast-link instantâneo no draw-call para novas variações de estado de shaders já compilados.

---

### 🛡️ Fase D: Async Restrito em 2 Camadas (Estimativa: 2–3 dias)
- [ ] **M3: Mascaramento Seguro de Shaders Não Compilados (`feat/restricted-async`)**
  - [ ] **M3.1**: Auditoria de símbolos `fxb0` da Categoria 6B (isolar água, neblina e vidro de partículas puras).
  - [ ] **M3.2**: Implementação da Camada 1 (Allowlist positiva estrita por PSO: Categoria 6A).
  - [ ] **M3.3**: Implementação da Camada 2 (Trava no `IssueDraw` para $\le 6$ vértices protegendo quads de tela).
  - [ ] **M3.4**: Configuração do Presentation Gate (não travar swapchain aguardando efeitos cosméticos).

---

### 📦 Fase E: Cache Local Offline & Polish (Backlog / Opcional)
- [ ] **M4: Ferramentas Locais da Máquina do Jogador (`feat/local-session-cache`)**
  - [ ] **M4.1**: Exportação/importação de manifest local de sessões anteriores (`.xpl` local do jogador).
  - [ ] **M4.2**: Pré-tradução offline de shaders locais na instalação.
  - [ ] **M4.3**: Barra de progresso informativa no launcher para hardware em Tier T5.

---

## 3. Matriz Evolutiva de Métricas de Performance (v5)

| Métrica | Baseline Frio (D8) | Baseline Quente (D8 Ref) | Meta T1 (1º Boot Frio) | Meta T1 (Sessões Repetidas) | Pós-M0 | Pós-M1 | Pós-M2.1 (EDS) | Pós-M2a | Pós-M2 (GPL) | Pós-M3 (Final) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **P99.9 (ms)** | `443.77` | `30.42` | **`< 50.0`** | **`< 33.33` (agg)** | - | - | - | - | - | - |
| **Stalls > 100 ms** | `22.3` | `0.0` | **`<= 5`** | **`0.0`** | - | - | - | - | - | - |
| **Stalls > 33.33 ms** | `34.7` | `7.0` | **`<= 12`** | **`<= 8` (sessão) / `<= 3` (gp)** | - | - | - | - | - | - |
| **Tempo Perdido (ms/1k)**| `1,477.8` | `14.9` | **`< 250.0`** | **`< 50.0`** | - | - | - | - | - | - |
| **Boot D3D12 (s)** | `~24.0` | `~3.5` | **`<= 3.5`** | **`<= 3.5`** | - | - | - | - | - | - |
| **Visual Integrity** | 100% | 100% | **0 artefatos** | **0 artefatos** | - | - | - | - | - | - |

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
- **Status Atual**: Pronto para criar a branch `feat/telemetry-and-gate` e iniciar o **Milestone M0**.
- **Próximo Passo Imediato**:
  - Criar branch `feat/telemetry-and-gate`.
  - Aplicar correções de telemetria em `tools/source/rexglue-sdk/src/graphics/fh1_frame_cost.h` (M0.1 e M0.1b).

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
