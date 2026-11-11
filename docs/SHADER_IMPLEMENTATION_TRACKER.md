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
| **Current Active Milestone** | **`M0: Telemetry Integrity & CI Gate`** | Telemetria no SDK + Verificador automatizado. |
| **Current Working Branch** | `main` | Próxima branch de trabalho: `feat/telemetry-and-gate` |
| **Overall Execution Progress** | **0% Completed (0 / 12 Subtasks)** | Plano consolidado (Rev 4), pronto para código. |
| **GPL Spike Gate Status (M2a)** | ⏳ **Pending Phase A completion** | Aguardando M0 e M1 para início do spike. |
| **Clean-Room Compliance** | 🟢 **100% Verified** | Zero assets, microcódigo, manifests ou hashes distribuídos. |

---

## 2. Milestone Execution Roadmap & Checklist

### 🏁 Fase A: Telemetria & Ganhos Imediatos (Estimativa: 1–2 dias)
- [ ] **M0: Telemetria & CI Gate (`feat/telemetry-and-gate`)**
  - [ ] **M0.1**: Corrigir bug de atribuição em `fh1_frame_cost.h` (`MainCause` culpando o jogo em vez de `Fh1WaitForAhead`).
  - [ ] **M0.1b**: Separar diagnósticos: `pipeline_compile_ms` (SASS síncrono) vs `pipeline_wait_ms` (bloqueio de thread).
  - [ ] **M0.2**: Habilitar `REXGLUE_ENABLE_PERF_COUNTERS` na configuração Release-Diagnostic do CMake.
  - [ ] **M0.3**: Formalizar suite de automação `scripts/verify-shader-mitigation.ps1` com slicing de gameplay.
  - [ ] **M0.4**: Recapturar baseline oficial sob ambiente controlado (salvar JSON de referência).
- [ ] **M1: Bypass .vkpc & D3D12 Multi-Thread (`feat/vkpc-bypass-d3d12-parallel`)**
  - [ ] **M1.1**: Adicionar flag `--vulkan_store_pipeline_cache=false` como padrão no Vulkan (elimina +7.82 ms/PSO).
  - [ ] **M1.2**: Reaplicar patch de compilação paralela D3D12 (`d3d12-blocking-pso-precompile.patch`).
  - [ ] **M1.3**: Executar verificação automatizada (`verify-shader-mitigation.ps1`) e registrar métricas.

---

### 🔬 Fase B: Spike de Validação GPL (Estimativa: 1–2 dias | Gate Bloqueante)
- [ ] **M2a: Spike de Validação Rápido (`feat/gpl-validation-spike`)**
  - [ ] **M2a.0**: Portar arquitetura de harness CLI headless do *TheSimpsonsGameRecomp* (`YesterMester`).
  - [ ] **M2a.1**: Extrair as 342 tuplas reais da cache `4E4D0816.fbo.vk.xpso` para o harness de teste.
  - [ ] **M2a.2**: Auditar suporte do driver à feature `graphicsPipelineLibraryIndependentInterpolationDecoration`.
  - [ ] **M2a.3**: Medir latência de fast-link no driver RTX 3060 Ti (Threshold: $\ge 95\%$ em $< 1.0\text{ ms}$).
  - [ ] **M2a.4**: Medir overhead de execução na GPU (Draw time de pipeline linkado vs monolítico $< 2\%$).
  - [ ] **M2a.5**: Validar 0 erros/avisos nas camadas de validação Vulkan.
  - [ ] **GATE DECISION**: Se aprovado $\rightarrow$ Fase C. Se reprovado $\rightarrow$ Ativar Contingência R1 (EDS + Async).

---

### ⚡ Fase B+ (Opcional): Otimizações de CPU no CP (Estimativa: 0.5–1 dia)
- [ ] **M1.5: Patches da CrownParkComputing (`feat/cp-cpu-optimizations`)**
  - [ ] **M1.5a**: Cache MRU de 64 entradas em `GetCurrentStateDescription`.
  - [ ] **M1.5b**: Fast path de 16 entradas em `UpdateBindings`.
  - [ ] **M1.5c**: Cópia em bloco via `memcpy` para comandos PM4 sequenciais.
  - [ ] **M1.5d**: Validar que não há regressão no baseline quente D8.

---

### 🏛️ Fase C: Núcleo Vulkan GPL & Extended Dynamic State (Estimativa: 5–8 dias)
- [ ] **M2: Arquitetura de 4 Bibliotecas & EDS (`feat/vulkan-gpl-core`)**
  - [ ] **M2.0**: Mapeamento detalhado dos padrões de chamada Vulkan do DXVK (`dxvk_graphics.cpp`).
  - [ ] **M2.1**: Implementação da detecção de recursos Vulkan e Matriz de Fallback (Tiers T1 a T5).
  - [ ] **M2.2**: Implementação das 4 bibliotecas (`Vertex Input`, `Pre-Raster`, `Fragment Shader`, `Fragment Output`).
  - [ ] **M2.3**: Implementação de Extended Dynamic State 1 & 2 (Depth test, depth write, cull mode, stencil).
  - [ ] **M2.4**: Implementação de Extended Dynamic State 3 (Color blend enable, equation, write mask).
  - [ ] **M2.5**: Verificação de linking instantâneo em tempo de draw sem travar thread de renderização.

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

## 3. Matriz Evolutiva de Métricas de Performance

| Métrica | Baseline Frio (D8) | Baseline Quente (D8 Ref) | Meta Tier T1 (GPL+EDS) | Pós-M0 | Pós-M1 | Pós-M2a | Pós-M2 | Pós-M3 (Final) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **P99.9 (ms)** | `443.77` | `30.42` | **`< 33.33` (agg)** | - | - | - | - | - |
| **Stalls > 100 ms** | `22.3` | `0.0` | **`0.0`** | - | - | - | - | - |
| **Stalls > 33.33 ms** | `34.7` | `7.0` | **`<= 8` (sessão) / `<= 3` (gameplay)** | - | - | - | - | - |
| **Tempo Perdido (ms/1k)**| `1,477.8` | `14.9` | **`< 50.0`** | - | - | - | - | - |
| **Boot D3D12 (s)** | `~24.0` | `~3.5` | **`<= 3.5`** | - | - | - | - | - |
| **Visual Integrity** | 100% | 100% | **0 artefatos** | - | - | - | - | - |

---

## 4. Diário de Execução Passo a Passo (Execution Journal)

*Cada sessão de trabalho registrará aqui exatamente onde parou, o que foi feito, commits gerados e o próximo passo imediato.*

### 📝 Entrada 000 — 2026-11-11: Inicialização do Plano de Execução
- **Fase / Tarefa**: Planejamento e Estruturação do Tracker.
- **Branch**: `main`
- **Ações Realizadas**:
  - Concluídas as etapas de descoberta D1 a D8 e refinado o plano arquitetural até a Revisão 4 (`docs/SHADER_IMPLEMENTATION_PLAN.md`).
  - Estabelecida a conformidade estrita de Clean-Room (zero assets/manifests distribuídos).
  - Integradas as 4 referências externas: *TheSimpsonsGameRecomp* (harness M2a.0), *DXVK* (4 bibliotecas M2), Khronos (`IndependentInterpolationDecoration` M2a.2) e *CrownParkComputing* (patches de CPU M1.5).
  - Criado este documento de rastreamento contínuo (`docs/SHADER_IMPLEMENTATION_TRACKER.md`).
- **Status Atual**: Pronto para criar a branch `feat/telemetry-and-gate` e iniciar o **Milestone M0**.
- **Próximo Passo Imediato**:
  - Criar branch `feat/telemetry-and-gate`.
  - Implementar correções de telemetria em `tools/source/rexglue-sdk/src/graphics/fh1_frame_cost.h` (separação de `pipeline_compile_ms` e `pipeline_wait_ms`).

---

## 5. Log de Decisões Técnicas & Desvios

| ID | Data | Contexto / Decisão | Justificativa Técnica |
| :---: | :---: | :--- | :--- |
| **DEC-01** | 2026-11-11 | **Zero Distribuição de `.xpl` / Manifestos** | Preservar estrita segurança legal e clean-room; foco total na eliminação de stutter em runtime via GPL + EDS. |
| **DEC-02** | 2026-11-11 | **Spike M2a Precedendo M2** | De-risking obrigatório de 1 dia antes de alocar 5-8 dias reescrevendo o pipeline cache em 4 bibliotecas. |
| **DEC-03** | 2026-11-11 | **Port do Harness Simpsons (M2a.0)** | Redução de 40–50% do esforço do spike reutilizando infraestrutura de teste CLI já pronta da comunidade. |
| **DEC-04** | 2026-11-11 | **Adoção de IndependentInterpolationDecoration** | Elimina risco de explosão combinatória de pré-rasterização (VS não precisa ser duplicado por variante de PS). |
