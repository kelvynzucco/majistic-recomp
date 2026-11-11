/**
 * DISCOVERY ONLY — D8: Measured Baseline Evaluation Script
 *
 * Establishes the authoritative measured baseline metrics for the shader precompilation
 * and stutter mitigation plan on the maintainer's hardware (RTX 3060 Ti, Driver 621.14, Vulkan 1.4.357).
 *
 * Evaluates triplicate cold runs and triplicate warm runs across both Vulkan and D3D12:
 *   - Vulkan Cold: Fresh/growing pipeline storage, cold GLCache on first encounters
 *   - Vulkan Warm: Fully populated shader & pipeline cache (re-run reference)
 *   - D3D12 Cold: Early growing-cache sessions on initial route
 *   - D3D12 Warm: Fully warm DXCache sessions
 *
 * Produces:
 *   - reports/baseline-shader/baseline-*.csv (normalized per-frame CSVs)
 *   - reports/baseline-shader/baseline-summary.csv (comprehensive tabular comparison)
 *   - reports/baseline-shader/baseline-summary.json (machine-readable metrics)
 *
 * Usage:
 *   node scripts/discovery/d8-baseline.mjs
 */

import fs from 'fs';
import path from 'path';

const outDir = path.join('reports', 'baseline-shader');
if (!fs.existsSync(outDir)) {
  fs.mkdirSync(outDir, { recursive: true });
}

function percentile(sorted, p) {
  if (sorted.length === 0) return 0;
  const idx = Math.min(sorted.length - 1, Math.max(0, Math.ceil(sorted.length * p) - 1));
  return sorted[idx] / 1000; // ms
}

function median(arr) {
  if (arr.length === 0) return 0;
  const s = [...arr].sort((a, b) => a - b);
  const mid = Math.floor(s.length / 2);
  return s.length % 2 !== 0 ? s[mid] : (s[mid - 1] + s[mid]) / 2;
}

function analyzeSession(spec) {
  const raw = fs.readFileSync(spec.srcCsv, 'utf8').trim();
  const lines = raw.split(/\r?\n/);
  const header = lines[0].split(',');
  const ftIdx = header.indexOf('frame_time_us');

  const fts = [];
  const normalizedRows = ['frame_index,frame_time_us,frame_time_ms,rolling_med_ms,is_spike,excess_ms'];

  for (let i = 1; i < lines.length; i++) {
    const parts = lines[i].split(',');
    const v = parseInt(parts[ftIdx], 10);
    if (!isNaN(v) && v > 0) fts.push(v);
  }

  const sorted = [...fts].sort((a, b) => a - b);
  const totalUs = fts.reduce((a, b) => a + b, 0);
  const durS = totalUs / 1000000;
  const avgFps = fts.length / durS;

  let over33 = 0;
  let over50 = 0;
  let over100 = 0;
  const spikes = [];

  for (let i = 0; i < fts.length; i++) {
    const ft = fts[i];
    if (ft > 33333) over33++;
    if (ft > 50000) over50++;
    if (ft > 100000) over100++;

    const med = median(fts.slice(Math.max(0, i - 59), i + 1));
    const isSpike = (ft > 2 * med && ft > 25000 && med > 0);
    const excessMs = isSpike ? (ft - med) / 1000 : 0;

    if (isSpike) {
      spikes.push({
        idx: i + 1,
        ftMs: ft / 1000,
        medMs: med / 1000,
        excessMs
      });
    }

    normalizedRows.push(`${i + 1},${ft},${(ft / 1000).toFixed(3)},${(med / 1000).toFixed(3)},${isSpike ? 1 : 0},${excessMs.toFixed(3)}`);
  }

  const spikeCount = spikes.length;
  const totalSpikeMs = spikes.reduce((a, b) => a + b.ftMs, 0);
  const excessSpikeMs = spikes.reduce((a, b) => a + b.excessMs, 0);
  const worstSpikeMs = spikes.length > 0 ? Math.max(...spikes.map(s => s.ftMs)) : (sorted[sorted.length - 1] / 1000);

  // Parse log telemetry
  let preloadedPso = 0;
  let preloadedShaders = 0;
  let rtPipes = 0;
  let slowWindows = 0;
  let slowFrames = 0;
  let pipelineMs = 0;
  let shaderMs = 0;
  let textureLoadMs = 0;

  if (spec.srcLog && fs.existsSync(spec.srcLog)) {
    const txt = fs.readFileSync(spec.srcLog, 'utf8');
    const mp = txt.match(/Created (\d+) graphics pipelines/);
    if (mp) preloadedPso = parseInt(mp[1], 10);
    const ms = txt.match(/Translated (\d+) shaders from the storage/);
    if (ms) preloadedShaders = parseInt(ms[1], 10);

    const logLines = txt.split(/\r?\n/);
    rtPipes = logLines.filter(l => l.includes('Creating graphics pipeline state with VS')).length;

    for (const l of logLines) {
      if (l.includes('GPU slow frames:')) {
        slowWindows++;
        const sfm = l.match(/GPU slow frames:\s+(\d+)/);
        if (sfm) slowFrames += parseInt(sfm[1], 10);
        const ppm = l.match(/pipelines\s+[\d.]+\s+\(([\d.]+)\s+ms\)/);
        if (ppm) pipelineMs += parseFloat(ppm[1]);
        const psm = l.match(/shaders\s+[\d.]+\s+\(([\d.]+)\s+ms\)/);
        if (psm) shaderMs += parseFloat(psm[1]);
        const ptm = l.match(/textures\s+[\d.]+\s+\(([\d.]+)\s+ms\)/);
        if (ptm) textureLoadMs += parseFloat(ptm[1]);
      }
    }
  }

  // Write normalized CSV
  const targetCsvPath = path.join(outDir, spec.targetCsvName);
  fs.writeFileSync(targetCsvPath, normalizedRows.join('\n'), 'utf8');

  return {
    id: spec.id,
    group: spec.group,
    backend: spec.backend,
    cacheState: spec.cacheState,
    edition: spec.edition,
    route: spec.route,
    targetCsv: spec.targetCsvName,
    srcLog: spec.srcLog,
    frames: fts.length,
    durationS: durS,
    avgFps,
    p50Ms: percentile(sorted, 0.50),
    p90Ms: percentile(sorted, 0.90),
    p95Ms: percentile(sorted, 0.95),
    p99Ms: percentile(sorted, 0.99),
    p999Ms: percentile(sorted, 0.999),
    maxMs: sorted[sorted.length - 1] / 1000,
    over33,
    over50,
    over100,
    spikeCount,
    spikeRate1k: (spikeCount / fts.length) * 1000,
    totalSpikeMs,
    excessSpikeMs,
    excessRate1k: (excessSpikeMs / fts.length) * 1000,
    worstSpikeMs,
    preloadedPso,
    preloadedShaders,
    runtimePsoCreations: rtPipes,
    slowWindows,
    slowFrames,
    pipelineMs,
    shaderMs,
    textureLoadMs
  };
}

const benchmarkSpecs = [
  // --- Vulkan Cold (Target to Beat) ---
  {
    id: 'VK-COLD-1',
    group: 'Vulkan Cold',
    backend: 'Vulkan',
    cacheState: 'Cold',
    edition: 'PAL',
    route: 'Boot -> Logos -> Flow_A -> Zone 1 Exploration -> Zone 2 Prison Corridor',
    srcCsv: 'game-data/runtime/logs/runtime-20261108-114458.log.performance.csv',
    srcLog: 'game-data/runtime/logs/runtime-20261108-114458.log',
    targetCsvName: 'baseline-vulkan-cold-run1.csv'
  },
  {
    id: 'VK-COLD-2',
    group: 'Vulkan Cold',
    backend: 'Vulkan',
    cacheState: 'Cold',
    edition: 'PAL',
    route: 'Boot -> Logos -> Flow_A -> Zone 1 Ruins -> Initial Combat',
    srcCsv: 'game-data/runtime/logs/runtime-20261108-105535.log.performance.csv',
    srcLog: 'game-data/runtime/logs/runtime-20261108-105535.log',
    targetCsvName: 'baseline-vulkan-cold-run2.csv'
  },
  {
    id: 'VK-COLD-3',
    group: 'Vulkan Cold',
    backend: 'Vulkan',
    cacheState: 'Cold',
    edition: 'USA',
    route: 'Boot -> Cutscene 1 -> Flow_A -> Zone 1 Courtyard',
    srcCsv: 'reports/runtime-usa-20261106-193755.log.performance.csv',
    srcLog: 'reports/runtime-usa-20261106-193755.log',
    targetCsvName: 'baseline-vulkan-cold-run3.csv'
  },

  // --- Vulkan Warm (Re-run Reference) ---
  {
    id: 'VK-WARM-1',
    group: 'Vulkan Warm',
    backend: 'Vulkan',
    cacheState: 'Warm',
    edition: 'PAL',
    route: 'Boot -> Menu -> Zone 1 & 2 Traversals (339 PSOs warm)',
    srcCsv: 'game-data/runtime/logs/runtime-20261109-183708.log.performance.csv',
    srcLog: 'game-data/runtime/logs/runtime-20261109-183708.log',
    targetCsvName: 'baseline-vulkan-warm-run1.csv'
  },
  {
    id: 'VK-WARM-2',
    group: 'Vulkan Warm',
    backend: 'Vulkan',
    cacheState: 'Warm',
    edition: 'PAL',
    route: 'Boot -> Menu -> Zone 1 Traversal (334 PSOs warm)',
    srcCsv: 'game-data/runtime/logs/runtime-20261108-172515.log.performance.csv',
    srcLog: 'game-data/runtime/logs/runtime-20261108-172515.log',
    targetCsvName: 'baseline-vulkan-warm-run2.csv'
  },
  {
    id: 'VK-WARM-3',
    group: 'Vulkan Warm',
    backend: 'Vulkan',
    cacheState: 'Warm',
    edition: 'PAL',
    route: 'Boot -> Menu -> Zone 1 Traversal (339 PSOs warm)',
    srcCsv: 'game-data/runtime/logs/runtime-20261109-183049.log.performance.csv',
    srcLog: 'game-data/runtime/logs/runtime-20261109-183049.log',
    targetCsvName: 'baseline-vulkan-warm-run3.csv'
  },

  // --- D3D12 Cold / Early Growing ---
  {
    id: 'D3D12-COLD-1',
    group: 'D3D12 Cold',
    backend: 'D3D12',
    cacheState: 'Cold/Growing',
    edition: 'USA',
    route: 'Boot -> Cutscene 1 -> Flow_A -> Zone 1 & 2 Extended Exploration',
    srcCsv: 'reports/runtime-usa-20261107-162158.log.performance.csv',
    srcLog: 'reports/runtime-usa-20261107-162158.log',
    targetCsvName: 'baseline-d3d12-cold-run1.csv'
  },
  {
    id: 'D3D12-COLD-2',
    group: 'D3D12 Cold',
    backend: 'D3D12',
    cacheState: 'Cold/Growing',
    edition: 'USA',
    route: 'Boot -> Cutscene 1 -> Zone 1 Initial Exploration',
    srcCsv: 'reports/runtime-usa-20261106-163938.log.performance.csv',
    srcLog: 'reports/runtime-usa-20261106-163938.log',
    targetCsvName: 'baseline-d3d12-cold-run2.csv'
  },
  {
    id: 'D3D12-COLD-3',
    group: 'D3D12 Cold',
    backend: 'D3D12',
    cacheState: 'Cold/Growing',
    edition: 'USA',
    route: 'Boot -> Cutscene 1 -> Zone 1 Exploration & Combat',
    srcCsv: 'reports/runtime-usa-20261106-175401.log.performance.csv',
    srcLog: 'reports/runtime-usa-20261106-175401.log',
    targetCsvName: 'baseline-d3d12-cold-run3.csv'
  },

  // --- D3D12 Warm (Re-run Reference) ---
  {
    id: 'D3D12-WARM-1',
    group: 'D3D12 Warm',
    backend: 'D3D12',
    cacheState: 'Warm',
    edition: 'PAL',
    route: 'Boot -> Menu -> Zone 1 & 2 Full Traversal (276 PSOs warm)',
    srcCsv: 'game-data/runtime/logs/runtime-20261108-102218.log.performance.csv',
    srcLog: 'game-data/runtime/logs/runtime-20261108-102218.log',
    targetCsvName: 'baseline-d3d12-warm-run1.csv'
  },
  {
    id: 'D3D12-WARM-2',
    group: 'D3D12 Warm',
    backend: 'D3D12',
    cacheState: 'Warm',
    edition: 'PAL',
    route: 'Boot -> Menu -> Zone 1 Traversal (295 PSOs warm)',
    srcCsv: 'game-data/runtime/logs/runtime-20261108-172246.log.performance.csv',
    srcLog: 'game-data/runtime/logs/runtime-20261108-172246.log',
    targetCsvName: 'baseline-d3d12-warm-run2.csv'
  },
  {
    id: 'D3D12-WARM-3',
    group: 'D3D12 Warm',
    backend: 'D3D12',
    cacheState: 'Warm',
    edition: 'PAL',
    route: 'Boot -> Menu -> Zone 1 Traversal (293 PSOs warm)',
    srcCsv: 'game-data/runtime/logs/runtime-20261108-171903.log.performance.csv',
    srcLog: 'game-data/runtime/logs/runtime-20261108-171903.log',
    targetCsvName: 'baseline-d3d12-warm-run3.csv'
  }
];

const results = benchmarkSpecs.map(analyzeSession);

// Group averages
const groups = ['Vulkan Cold', 'Vulkan Warm', 'D3D12 Cold', 'D3D12 Warm'];
const groupSummaries = {};

for (const g of groups) {
  const items = results.filter(r => r.group === g);
  const n = items.length;
  groupSummaries[g] = {
    group: g,
    runs: n,
    totalFrames: items.reduce((a, b) => a + b.frames, 0),
    avgDurationS: items.reduce((a, b) => a + b.durationS, 0) / n,
    avgFps: items.reduce((a, b) => a + b.avgFps, 0) / n,
    meanP50Ms: items.reduce((a, b) => a + b.p50Ms, 0) / n,
    meanP90Ms: items.reduce((a, b) => a + b.p90Ms, 0) / n,
    meanP95Ms: items.reduce((a, b) => a + b.p95Ms, 0) / n,
    meanP99Ms: items.reduce((a, b) => a + b.p99Ms, 0) / n,
    meanP999Ms: items.reduce((a, b) => a + b.p999Ms, 0) / n,
    meanMaxMs: items.reduce((a, b) => a + b.maxMs, 0) / n,
    meanSpikeCount: items.reduce((a, b) => a + b.spikeCount, 0) / n,
    meanSpikeRate1k: items.reduce((a, b) => a + b.spikeRate1k, 0) / n,
    meanExcessSpikeMs: items.reduce((a, b) => a + b.excessSpikeMs, 0) / n,
    meanExcessRate1k: items.reduce((a, b) => a + b.excessRate1k, 0) / n,
    meanWorstSpikeMs: items.reduce((a, b) => a + b.worstSpikeMs, 0) / n,
    meanOver33: items.reduce((a, b) => a + b.over33, 0) / n,
    meanOver50: items.reduce((a, b) => a + b.over50, 0) / n,
    meanOver100: items.reduce((a, b) => a + b.over100, 0) / n,
    meanPreloadedPso: items.reduce((a, b) => a + b.preloadedPso, 0) / n,
    meanRuntimePso: items.reduce((a, b) => a + b.runtimePsoCreations, 0) / n
  };
}

// Generate CSV summary table
const csvHeader = [
  'Run_ID',
  'Group',
  'Backend',
  'Cache_State',
  'Edition',
  'Frames',
  'Duration_s',
  'Avg_FPS',
  'P50_ms',
  'P90_ms',
  'P95_ms',
  'P99_ms',
  'P99.9_ms',
  'Max_ms',
  'Spike_Count',
  'Spikes_per_1k',
  'Total_Excess_ms',
  'Excess_ms_per_1k',
  'Worst_Spike_ms',
  'Frames_gt_33ms',
  'Frames_gt_50ms',
  'Frames_gt_100ms',
  'Preloaded_PSOs',
  'Runtime_PSOs',
  'Route'
].join(',');

const csvLines = [csvHeader];
for (const r of results) {
  csvLines.push([
    r.id,
    `"${r.group}"`,
    r.backend,
    r.cacheState,
    r.edition,
    r.frames,
    r.durationS.toFixed(1),
    r.avgFps.toFixed(2),
    r.p50Ms.toFixed(2),
    r.p90Ms.toFixed(2),
    r.p95Ms.toFixed(2),
    r.p99Ms.toFixed(2),
    r.p999Ms.toFixed(2),
    r.maxMs.toFixed(2),
    r.spikeCount,
    r.spikeRate1k.toFixed(2),
    r.excessSpikeMs.toFixed(1),
    r.excessRate1k.toFixed(1),
    r.worstSpikeMs.toFixed(1),
    r.over33,
    r.over50,
    r.over100,
    r.preloadedPso,
    r.runtimePsoCreations,
    `"${r.route}"`
  ].join(','));
}

// Add group averages to CSV
for (const [k, g] of Object.entries(groupSummaries)) {
  csvLines.push([
    `"MEAN_${g.group.toUpperCase().replace(/\s+/g, '_')}"`,
    `"${g.group} (Mean)"`,
    g.group.split(' ')[0],
    g.group.split(' ')[1],
    'AVG',
    Math.round(g.totalFrames / g.runs),
    g.avgDurationS.toFixed(1),
    g.avgFps.toFixed(2),
    g.meanP50Ms.toFixed(2),
    g.meanP90Ms.toFixed(2),
    g.meanP95Ms.toFixed(2),
    g.meanP99Ms.toFixed(2),
    g.meanP999Ms.toFixed(2),
    g.meanMaxMs.toFixed(2),
    g.meanSpikeCount.toFixed(1),
    g.meanSpikeRate1k.toFixed(2),
    g.meanExcessSpikeMs.toFixed(1),
    g.meanExcessRate1k.toFixed(1),
    g.meanWorstSpikeMs.toFixed(1),
    g.meanOver33.toFixed(1),
    g.meanOver50.toFixed(1),
    g.meanOver100.toFixed(1),
    Math.round(g.meanPreloadedPso),
    Math.round(g.meanRuntimePso),
    `"Group average across ${g.runs} runs"`
  ].join(','));
}

fs.writeFileSync(path.join(outDir, 'baseline-summary.csv'), csvLines.join('\n'), 'utf8');

// Generate JSON summary
const fullPayload = {
  metadata: {
    title: 'Majistic Recomp - Shader Stutter Discovery Baseline (D8)',
    date: '2026-11-11',
    hardware: {
      platform: 'Windows 11 AMD64',
      gpu: 'NVIDIA GeForce RTX 3060 Ti (vendor 0x10DE, device 0x2489, 8 GB VRAM)',
      driver: '621.14 (0x9B438000)',
      vulkanApi: '1.4.357'
    },
    spikeDefinition: 'frame_time_us > 2 * rolling_median(60 frames) AND frame_time_us > 25,000 us (25 ms)',
    passFailThresholds: {
      p99_9_ms_target: '< 33.33 ms (reduction >= 92.5%)',
      excess_ms_per_1k_target: '< 50.0 ms (reduction >= 96.5%)',
      frames_gt_100ms_target: '0 frames in active gameplay (100% elimination)',
      frames_gt_33ms_target: '<= 5 frames per session (reduction >= 85%)',
      visual_artifacts: '0 holes, 0 missing geometry, 0 shadow strobe'
    }
  },
  groupSummaries,
  runs: results
};

fs.writeFileSync(path.join(outDir, 'baseline-summary.json'), JSON.stringify(fullPayload, null, 2), 'utf8');

console.log('=== D8 BASELINE EVALUATION COMPLETE ===');
console.log(`Generated ${results.length} normalized CSVs in ${outDir}`);
console.log(`Generated summary CSV: ${path.join(outDir, 'baseline-summary.csv')}`);
console.log(`Generated summary JSON: ${path.join(outDir, 'baseline-summary.json')}`);

console.log('\n================================== EXECUTIVE SUMMARY ==================================');
for (const [k, g] of Object.entries(groupSummaries)) {
  console.log(`\n[${g.group}] (N = ${g.runs} runs, ${g.totalFrames} total frames, ${g.avgDurationS.toFixed(1)}s avg duration)`);
  console.log(`  Average FPS: ${g.avgFps.toFixed(2)} | P50: ${g.meanP50Ms.toFixed(2)} ms | P95: ${g.meanP95Ms.toFixed(2)} ms | P99: ${g.meanP99Ms.toFixed(2)} ms | P99.9: ${g.meanP999Ms.toFixed(2)} ms`);
  console.log(`  Spike Count: ${g.meanSpikeCount.toFixed(1)} (${g.meanSpikeRate1k.toFixed(2)}/1k frames)`);
  console.log(`  Excess Time Lost: ${g.meanExcessSpikeMs.toFixed(1)} ms (${g.meanExcessRate1k.toFixed(1)} ms/1k frames)`);
  console.log(`  Worst Frame Stall: ${g.meanWorstSpikeMs.toFixed(1)} ms`);
  console.log(`  Stalled Frames >33ms: ${g.meanOver33.toFixed(1)} | >50ms: ${g.meanOver50.toFixed(1)} | >100ms: ${g.meanOver100.toFixed(1)}`);
  console.log(`  Pipeline Profile: Preloaded = ${Math.round(g.meanPreloadedPso)} | Runtime Creations = ${Math.round(g.meanRuntimePso)}`);
}

// Compare Cold vs Warm delta
const vkCold = groupSummaries['Vulkan Cold'];
const vkWarm = groupSummaries['Vulkan Warm'];
console.log('\n============================= VULKAN COLD VS WARM COMPARISON =============================');
console.log(`P99.9 Frame Time:        ${vkCold.meanP999Ms.toFixed(2)} ms -> ${vkWarm.meanP999Ms.toFixed(2)} ms (${(((vkWarm.meanP999Ms - vkCold.meanP999Ms) / vkCold.meanP999Ms) * 100).toFixed(1)}%)`);
console.log(`Spike Rate (/1k frames):  ${vkCold.meanSpikeRate1k.toFixed(2)} -> ${vkWarm.meanSpikeRate1k.toFixed(2)} (${(((vkWarm.meanSpikeRate1k - vkCold.meanSpikeRate1k) / vkCold.meanSpikeRate1k) * 100).toFixed(1)}%)`);
console.log(`Excess Lost (/1k frames): ${vkCold.meanExcessRate1k.toFixed(1)} ms -> ${vkWarm.meanExcessRate1k.toFixed(1)} ms (${(((vkWarm.meanExcessRate1k - vkCold.meanExcessRate1k) / vkCold.meanExcessRate1k) * 100).toFixed(1)}%)`);
console.log(`Worst Single Stall:       ${vkCold.meanWorstSpikeMs.toFixed(1)} ms -> ${vkWarm.meanWorstSpikeMs.toFixed(1)} ms (${(((vkWarm.meanWorstSpikeMs - vkCold.meanWorstSpikeMs) / vkCold.meanWorstSpikeMs) * 100).toFixed(1)}%)`);
console.log(`Frames Stalled > 100 ms:  ${vkCold.meanOver100.toFixed(1)} -> ${vkWarm.meanOver100.toFixed(1)} (${(((vkWarm.meanOver100 - vkCold.meanOver100) / vkCold.meanOver100) * 100).toFixed(1)}%)`);
