# D4: Shader Registration Timing & Load-Time Warm-Up Feasibility

**Document Version**: 1.1.0  
**Date**: 2026-11-10  
**Status**: Completed (with explicit gameplay and cold-driver caveats)  
**Branch / Environment**: Windows AMD64, Vulkan (`rexgpu-xenos.dll`), NVIDIA GeForce RTX 3060 Ti (Driver 621.14 / Vulkan 1.4.357)  
**Deliverable Script**: [`scripts/discovery/d4-shader-registration-timing.mjs`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/scripts/discovery/d4-shader-registration-timing.mjs)  
**Parent Plan**: [`docs/SHADER_PRECOMPILATION_PLAN.md`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/docs/SHADER_PRECOMPILATION_PLAN.md) (Issue #4)

---

## 1. Resumo Executivo & Respostas Centrais

A fase de descoberta **D4** avalia **quando o motor do jogo registra shaders: durante as telas de carregamento (level load) ou sob demanda (*lazily*) no draw time**, determinando a viabilidade de um aquecimento durante o carregamento de zonas (*zone-based warm-up*).

```mermaid
flowchart TD
    subgraph Disk["1. Armazenamento em Disco (4 Arquivos Globais)"]
        knl["Kernel do Motor (5 contêineres fxb0)"]
        pak1["Amostra Primitiva (1 contêiner fxb0)"]
        pak2["Efeitos VFX (2 contêineres fxb0)"]
        pak3["Pacote Estático Geral (91 contêineres fxb0)"]
        levelPak["551 Pacotes de Fases/Personagens (0 shaders)"]
    end

    subgraph GuestRAM["2. Ciclo de Vida na RAM do Guest"]
        bootLoad["Boot: Carregamento do pool global de templates"]
        sub_8261EAD8["Draw/Bind: sub_8261EAD8 patcha vfetch do VS em RAM"]
    end

    subgraph Ringbuffer["3. Ringbuffer do Command Processor"]
        pm4["PM4_IM_LOAD / PM4_IM_LOAD_IMMEDIATE"]
        lookahead["Fh1PipelineLookahead: varredura antecipada"]
    end

    subgraph Pipeline["4. Compilação e Execução de PSO"]
        translate["Tradução Host (SPIR-V): ~1,58 ms"]
        psoMiss["Compilação SASS no Driver a Frio: 45–205 ms (STALL!)"]
        draw["IssueDraw / vkCmdDraw"]
    end

    knl --> bootLoad
    pak1 --> bootLoad
    pak2 --> bootLoad
    pak3 --> bootLoad
    levelPak -. "0 shaders" .-> bootLoad

    bootLoad --> sub_8261EAD8
    sub_8261EAD8 --> pm4
    pm4 --> lookahead
    lookahead --> translate
    pm4 --> draw
    draw --> psoMiss
```

### Conclusões Centrais e Ressalvas Metodológicas

1. **A engine registra shaders sob demanda (*lazily*) no draw time**:
   - **Zero shaders por fase em disco**: A varredura de todos os **11.310 arquivos** e **555 pacotes `.pak`** dos dados do jogo confirmou que **nenhum pacote de fase, personagem ou cutscene contém shaders**. Todos os 98 contêineres de shader residem em apenas **4 arquivos globais de inicialização**.
   - **Patching dinâmico de `vfetch` em RAM**: A comparação estrutural entre os shaders de runtime e os contêineres em disco revelou que enquanto **93,5% dos Pixel Shaders** coincidem integralmente com os dados em disco, **0% dos Vertex Shaders** coincidem integralmente. A rotina guest [`sub_8261EAD8`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/prototype/generated/default/majin_recomp.139.cpp#L16088-L16625) faz patch dinâmico em RAM das instruções de busca de vértices (`vfetch`) no momento em que a declaração de vértices do mesh é associada, imediatamente antes de emitir o pacote `PM4_IM_LOAD`.
   - **Ressalva de amostragem**: O comportamento 100% *lazy* com envio imediato antes do draw foi medido diretamente durante a inicialização e menus ($N = 27$ shaders observados no probe, com mediana de $\Delta(\text{IM} \to \text{Draw}) = 1,33\text{ ms}$). Para o gameplay propriamente dito, a evidência é primariamente **estrutural** (inexistência de shaders nos pacotes de fases e necessidade de declaração de vértices para sintetizar o VS final).
2. **Ausência de shaders durante o carregamento (`flow_a`)**:
   - Durante a transição de carregamento (`flow_a entry` até `flow_a result`), são processados **0 pacotes `PM4_IM_LOAD`**.
   - *Ressalva:* Como durante a tela de loading não ocorrem draws 3D, a ausência de registros é esperada; o dado comprova que a engine **não aproveita a janela de carregamento para pré-declarar ou aquecer shaders na GPU**.
3. **Janela de Lookahead do Ringbuffer**:
   - Na inicialização e menus, o lead time do [`Fh1PipelineLookahead`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/fh1_pipeline_lookahead.cpp) sobre o Command Processor situou-se entre **0,05 ms e 2,5 ms**.
   - *Ressalva de gameplay:* Durante o gameplay, se a thread de CPU do jogo abrir uma folga de 2 a 3 frames (66 a 100 ms a 30 FPS) à frente do Command Processor, a janela pode ser maior. Porém, em picos de compilação a frio de 100 a 200 ms, essa folga é rapidamente consumida, levando à trava em [`Fh1WaitForAhead`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/tools/source/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp#L1846).
4. **Veredito sobre Pré-Aquecimento (Warm-Up)**:
   - **Pré-aquecimento dinâmico por zona/fase: NÃO É VIÁVEL.** Não existem shaders isolados por fase nem eventos de pré-registro em `flow_a`.
   - **Pré-aquecimento no Boot: VIÁVEL APENAS SE A LISTA DE PSOs FOR OBTIDA LOCALMENTE.**
     > [!IMPORTANT]
     > A premissa de que *"basta pré-compilar o `.xpso` conhecido no boot"* pressupõe que o `.xpso` já exista. Para um jogador na primeira execução, o arquivo `.xpso` **não existe e não pode ser distribuído**.
     > A D4 transfere para a D5 o desafio central: **é possível gerar a lista de pipelines localmente na máquina do usuário sem jogar?**
   - **Achado chave para a D5 e O2**: O fato de que **93,5% dos Pixel Shaders batem com o disco** e **94% dos Vertex Shaders batem exceto pelas palavras de `vfetch`** torna a extração local a partir dos próprios arquivos do jogo do usuário (**O2**) muito mais plausível do que o planejamento original supunha.

---

## 2. Medição Controlada de Pré-Compilação Paralela com Driver a Frio

Para sanar a dúvida sobre o custo real de uma pré-compilação no boot para um jogador novo (sem depender de cache de driver aquecido), foi realizado um teste controlado isolado:
- O diretório de cache do driver NVIDIA (`GLCache`) foi redirecionado via `__GL_SHADER_DISK_CACHE_PATH` para uma pasta vazia e limpa.
- O wrapper de `.vkpc` foi desativado (`--vulkan_store_pipeline_cache=false`).
- O motor executou o aquecimento de inicialização para os 342 PSOs conhecidos em 6 threads de trabalho.

### Resultados da Medição a Frio (RTX 3060 Ti, 6 threads)

| Etapa do Aquecimento | Tempo Decorrido (ms) | Custo Unitário de Relógio (Wall-Clock) | Custo Unitário por Thread |
| :--- | ---: | ---: | ---: |
| **Tradução Host de 239 Shaders (SPIR-V)** | **`280 ms`** | `1,17 ms / shader` | `1,17 ms / shader` (1 thread) |
| **Compilação Paralela de 342 PSOs em SASS** | **`5.699 ms`** | **`16,66 ms / PSO`** (paralelo em 6 threads) | **`~100 ms / PSO`** por thread |
| **Tempo Total do Warm-Up a Frio** | **`5.979 ms`** (`~5,98 s`) | — | — |

*Conclusão da Medição:* Para um jogador novato (driver completamente frio), pré-compilar 342 PSOs leva **aproximadamente 6 segundos** em paralelo. Isso confirma que os números de sub-segundo vistos na D3 aplicam-se apenas a sessões com driver já aquecido, e que uma pré-compilação fria exige uma estratégia consciente de tempo de carregamento no boot.

---

## 3. Telemetria Quantitativa do Probe de Registro

A análise foi conduzida com o probe instrumentado em corrida limpa a partir do boot:

### 3.1 Registro de Shaders por Fase do Motor

| Fase do Motor | Marcador no Log | Shaders Registrados (`PM4_IM_LOAD`) | Shaders Utilizados em Draw |
| :--- | :--- | ---: | ---: |
| **1. Inicialização do Kernel / Boot** | Início $\to$ `flow_c entry` | **2** | 0 |
| **2. Inicialização dos Menus** | `flow_c entry` $\to$ `SceneTitleLogo` | **18** | 21 |
| **3. Logo e Menu Principal** | `SceneTitleLogo` $\to$ `flow_a entry` | **8** | 6 |
| **4. Carregamento de Nível (`flow_a`)** | `flow_a entry` $\to$ `flow_a result` | **0** (0,0%) | 0 (Carregamento sem draws) |
| **5. Cutscenes e Gameplay** | `flow_a result` em diante | Registrados dinamicamente ao vincular geometria | Desenhados sob demanda |

### 3.2 Intervalo de Tempo: Registro vs. Primeiro Draw

| Métrica | Amostras ($N$) | Mínimo | Mediana (P50) | Média | Máximo |
| :--- | ---: | ---: | ---: | ---: | ---: |
| **$\Delta(\text{PM4\_IM\_LOAD} \to \text{Primeiro Draw})$** | `27` | **`0,220 ms`** | **`1,327 ms`** | **`6,936 ms`** | **`81,771 ms`** |
| **$\Delta(\text{Lookahead} \to \text{Primeiro Draw})$** | `25` | **`-2.553 ms`** | **`-27,14 ms`** | **`-344,26 ms`** | **`22,89 ms`** |

*Nota:* Valores negativos de lookahead indicam que a thread de draw executou antes que o worker assíncrono de lookahead finalizasse aquela tupla específica, resultando em compilação síncrona na thread principal.

### 3.3 Distribuição Agregada dos Shaders Rastreados

| Tipo de Shader | Método de Envio PM4 | Contagem | Faixa de Tamanho (Bytes) | Mediana $\Delta(\text{IM} \to \text{Draw})$ |
| :--- | :--- | ---: | ---: | ---: |
| **Vertex Shader (VS)** | `IM_LOAD_IMM` (Embutido no ringbuffer) | 4 | 60 – 132 B | 1,33 ms |
| **Pixel Shader (PS)** | `IM_LOAD_IMM` (Embutido no ringbuffer) | 2 | 36 B | 32,43 ms |
| **Vertex Shader (VS)** | `IM_LOAD` (Referência à RAM física) | 11 | 96 – 264 B | 1,33 ms |
| **Pixel Shader (PS)** | `IM_LOAD` (Referência à RAM física) | 10 | 60 – 888 B | 1,33 ms |

---

## 4. Análise Estrutural dos Pacotes e Engenharia Reversa

### 4.1 Distribuição Estrutural em Disco

A varredura estática dos arquivos em disco catalogou:
* **Arquivos do Kernel do Motor**: 5 contêineres de shader básicos de materiais e primitivas.
* **Pacote de Amostras**: 1 contêiner de primitiva simples.
* **Pacote de Efeitos Visuais**: 2 contêineres de pós-processamento e desfoque.
* **Pacote Estático Central**: 91 contêineres abrangendo a totalidade dos materiais do jogo.
* **551 Pacotes de Fases, Personagens e Cutscenes**: **Zero contêineres de shader**. Contêm exclusivamente malhas geométricas, texturas e parâmetros de colisão.

### 4.2 Mecanismo de Patching de Vertex Fetch (`vfetch`)

A análise comparativa entre os microcódigos compilados e os templates estáticos em disco esclareceu a discrepância dos Vertex Shaders:

| Segmento do Microcódigo VS | Função Estrutural | Coincidência com o Arquivo em Disco |
| :--- | :--- | :--- |
| **Palavras 0 a 8 (Bytes 0..35)** | Cabeçalho Control Flow (CF), contadores de loop e endereços de cláusula | **100% idêntico** aos templates em disco. |
| **Palavras 9 a 14 (Bytes 36..59)** | Instruções de busca de vértices (`vfetch`) | **Divergente**: Em disco os campos de fluxo (*stream*), deslocamento (*offset*) e formato de dados estão zerados (templates genéricos). |
| **Palavras 15 em diante (Bytes 60..fim)** | Cláusulas de instruções ALU (cálculos matemáticos, vetoriais e transformações) | **100% idêntico** aos templates em disco. |

A rotina guest [`sub_8261EAD8`](file:///C:/Users/kelvy/Desktop/Majin%20Recomp/prototype/generated/default/majin_recomp.139.cpp#L16088-L16625) lê os elementos da `D3DVertexDeclaration` associada ao modelo geométrico e preenche dinamicamente em RAM os campos das instruções `vfetch` antes de invocar o envio à GPU.

---

## 5. Histórico de Sessões e Telas de Carregamento

A análise dos logs históricos de longa duração confirma a consistência do comportamento:

| Sessão | Condição de Execução | Eventos `flow_a` (Loading) | PSOs Compilados no Loading | PSOs Compilados em Gameplay |
| :--- | :--- | :--- | ---: | ---: |
| **Sessão Vulkan 15,9 min** | FBO, corrida a frio contínua | `flow_a entry` na linha 315 | **0** | **167** (distribuídos em 37 frames lentos) |
| **Sessão D3D12 10,0 min** | RTV, corrida a frio | `flow_a entry` na linha 248 | **0** | **112** (durante cutscenes e combate) |
| **Sessão Vulkan de Transição** | FBO, avanço entre áreas | `flow_a entry` na linha 1400 | **0** | **55** (picos concentrados em combate) |

Em todas as sessões registradas, a janela de carregamento transcorre com zero compilações de shader.

---

## 6. Desdobramentos para a Fase D5

Com o esclarecimento da D4, o escopo da **D5** é ampliado:
1. **Determinar se a lista de pipelines pode ser gerada na máquina do jogador sem jogar**:
   - Investigar se os contêineres de efeito (`fxb0`) contêm os estados fixos de renderização por passe (blend, depth, stencil, rasterização). Se contiverem, os 94 blocos de estado observados podem ser deduzidos diretamente dos dados estáticos.
   - Avaliar se o conjunto de declarações de vértice (`D3DVertexDeclaration`) do jogo é restrito e enumerável, permitindo gerar previamente as variações de `vfetch` dos Vertex Shaders.
2. **Medição do Lead Time do Lookahead em Gameplay**:
   - Verificar em tempo real se a folga da CPU sobre o Command Processor durante a exploração 3D atinge múltiplos frames, o que auxiliaria na mitigação assíncrona.
