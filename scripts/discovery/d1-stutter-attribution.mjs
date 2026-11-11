/**
 * DISCOVERY ONLY — D1: Stutter Attribution Analysis Script
 *
 * Analyzes all existing `*.performance.csv` and `*.log` files under `reports/`
 * and `game-data/runtime/logs/` using the D1 spike definition:
 *   frame_time_us > 2x rolling median (window = 60 frames) AND > 25 ms (25,000 us)
 *
 * Also parses all Vulkan `fh1_frame_cost` slow-frame windows (`GPU slow frames: ...`)
 * logged by `tools/source/rexglue-sdk/src/graphics/fh1_frame_cost.h`.
 *
 * Usage:
 *   node scripts/discovery/d1-stutter-attribution.mjs
 */

import fs from 'fs';
import path from 'path';

const roots = ['reports', 'game-data/runtime/logs'];

function walk(dir) {
  let results = [];
  if (!fs.existsSync(dir)) return results;
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      results = results.concat(walk(full));
    } else {
      results.push(full);
    }
  }
  return results;
}

const allFiles = roots.flatMap(walk);
const csvFiles = allFiles.filter(f => f.endsWith('.csv'));
const logFiles = allFiles.filter(f => f.endsWith('.log'));
const logMap = new Map(logFiles.map(f => [path.basename(f), f]));

function parseTimestampMs(line) {
  const m = line.match(/^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]/);
  if (!m) return null;
  return new Date(m[1].replace(' ', 'T') + 'Z').getTime();
}

function median(arr) {
  if (arr.length === 0) return 0;
  const sorted = [...arr].sort((a, b) => a - b);
  const mid = Math.floor(sorted.length / 2);
  return sorted.length % 2 !== 0 ? sorted[mid] : (sorted[mid - 1] + sorted[mid]) / 2;
}

let emptyOrHeaderOnlyCsvs = 0;
let validCsvs = 0;
let nonZeroCounterCsvs = 0;

const sessions = [];

for (const cp of csvFiles) {
  const base = path.basename(cp);
  const logName = base.endsWith('.performance.csv') ? base.replace(/\.performance\.csv$/, '') : null;
  const lp = logName ? logMap.get(logName) : null;

  const raw = fs.readFileSync(cp, 'utf8').trim();
  if (!raw) {
    emptyOrHeaderOnlyCsvs++;
    continue;
  }
  const csvLines = raw.split(/\r?\n/);
  const fts = [];
  let fileHasNonZeroCounters = false;
  for (let i = 1; i < csvLines.length; i++) {
    const parts = csvLines[i].split(',');
    const ft = parseInt(parts[0], 10);
    if (!isNaN(ft) && ft > 0) fts.push(ft);
    for (let c = 2; c < parts.length; c++) {
      if (parts[c] && parts[c] !== '0') fileHasNonZeroCounters = true;
    }
  }
  if (fileHasNonZeroCounters) nonZeroCounterCsvs++;
  if (fts.length === 0) {
    emptyOrHeaderOnlyCsvs++;
    continue;
  }
  validCsvs++;

  let backend = base.includes('vulkan') ? 'Vulkan' : 'Unknown';
  let edition = base.includes('usa') ? 'USA' : 'PAL';
  let shadersPreloaded = 0;
  let pipelinesPreloaded = 0;
  let swapStartMs = null;
  let preloadDone = false;
  const runtimePipelineCreations = [];
  const slowFramePipelineWindows = [];
  const ioTimestamps = [];
  const swapchainTimestamps = [];

  if (lp && fs.existsSync(lp)) {
    const lt = fs.readFileSync(lp, 'utf8');
    if (lt.includes('DXGI adapter') || lt.includes('Direct3D 12')) backend = 'D3D12';
    else if (lt.includes('Vulkan device')) backend = 'Vulkan';
    if (lt.includes('USA')) edition = 'USA';
    else if (lt.includes('Europe / PAL')) edition = 'PAL';

    const hasPreloadLine = lt.includes('Created ') && lt.includes('graphics pipelines');

    for (const line of lt.split(/\r?\n/)) {
      const ts = parseTimestampMs(line);
      if (line.includes('[Majin performance] Release counter mode:')) swapStartMs = ts;
      if (line.includes('Initializing shader storage for title') && !hasPreloadLine) preloadDone = true;
      const ms = line.match(/Translated (\d+) shaders from the storage/);
      if (ms) shadersPreloaded = parseInt(ms[1], 10);
      const mp = line.match(/Created (\d+) graphics pipelines/);
      if (mp) {
        pipelinesPreloaded = parseInt(mp[1], 10);
        preloadDone = true;
      }
      if (ts && preloadDone && line.includes('Creating graphics pipeline state with VS')) {
        runtimePipelineCreations.push(ts);
      }
      if (ts && line.includes('GPU slow frames:') && line.includes('pipelines ') && !line.includes('pipelines 0.0 (0.0 ms)')) {
        slowFramePipelineWindows.push(ts);
      }
      if (ts && (line.includes('[Majin loading') || line.includes('[Majin save') || line.includes('[Majin SceneTitleLogo]') || line.includes('[Majin XMA') || line.includes('audio endpoint'))) {
        ioTimestamps.push(ts);
      }
      if (ts && (line.includes('Swapchain') || line.includes('swapchain') || line.includes('Window gained focus') || line.includes('Frame captured:'))) {
        swapchainTimestamps.push(ts);
      }
    }
  }

  const spikes = [];
  let cumUs = 0;
  for (let i = 0; i < fts.length; i++) {
    const ft = fts[i];
    const frameStartMs = swapStartMs ? swapStartMs + cumUs / 1000 : null;
    cumUs += ft;
    const frameEndMs = swapStartMs ? swapStartMs + cumUs / 1000 : null;
    const med = median(fts.slice(Math.max(0, i - 59), i + 1));
    if (ft > 2 * med && ft > 25000 && med > 0) {
      const ftMs = ft / 1000;
      const excessMs = (ft - med) / 1000;
      const hasPipeLog = frameStartMs !== null && (
        runtimePipelineCreations.some(pts => pts >= frameStartMs - 500 && pts <= frameEndMs + 500) ||
        slowFramePipelineWindows.some(sts => frameEndMs >= sts - 10500 && frameEndMs <= sts + 500)
      );
      const hasSwapchain = frameStartMs !== null && swapchainTimestamps.some(sts => sts >= frameStartMs - 500 && sts <= frameEndMs + 500);
      const hasIo = i <= 5 || (frameStartMs !== null && ioTimestamps.some(its => its >= frameStartMs - 1000 && its <= frameEndMs + 1000));

      spikes.push({
        frameIdx: i + 1,
        ftMs,
        medMs: med / 1000,
        excessMs,
        hasPipeLog,
        hasSwapchain,
        hasIo,
      });
    }
  }

  sessions.push({
    base,
    logName: logName || base,
    backend,
    edition,
    frames: fts.length,
    totalMs: fts.reduce((a, b) => a + b, 0) / 1000,
    shadersPreloaded,
    pipelinesPreloaded,
    runtimePipeLogs: runtimePipelineCreations.length,
    spikes,
    spikeCount: spikes.length,
    spikeMs: spikes.reduce((a, b) => a + b.ftMs, 0),
    excessMs: spikes.reduce((a, b) => a + b.excessMs, 0),
  });
}

const d3dByTime = sessions.filter(s => s.backend === 'D3D12').sort((a, b) => a.logName.localeCompare(b.logName));
for (let i = 0; i < d3dByTime.length; i++) {
  const cur = d3dByTime[i];
  const next = d3dByTime[i + 1];
  if (next && next.pipelinesPreloaded > cur.pipelinesPreloaded) {
    cur.pipelinesBuiltInSession = next.pipelinesPreloaded - cur.pipelinesPreloaded;
  } else {
    cur.pipelinesBuiltInSession = 0;
  }
}

console.log('=== CSV INVENTORY ===');
console.log({ totalCsvFiles: csvFiles.length, validCsvs, emptyOrHeaderOnlyCsvs, nonZeroCounterCsvs });

for (const b of ['Vulkan', 'D3D12']) {
  const bs = sessions.filter(s => s.backend === b);
  const totalFrames = bs.reduce((a, s) => a + s.frames, 0);
  const totalMs = bs.reduce((a, s) => a + s.totalMs, 0);
  const allSpikes = bs.flatMap(s => s.spikes.map(sp => ({ ...sp, session: s })));
  const totalSpikes = allSpikes.length;
  const totalSpikeMs = allSpikes.reduce((a, sp) => a + sp.ftMs, 0);
  const totalExcessMs = allSpikes.reduce((a, sp) => a + sp.excessMs, 0);

  console.log(`\n=== ${b} (${bs.length} valid CSV sessions, ${totalFrames} frames, ${(totalMs / 1000).toFixed(1)}s) ===`);
  console.log(`Total Spikes: ${totalSpikes} (${((totalSpikes / totalFrames) * 100).toFixed(3)}% of frames)`);
  console.log(`Total Spike Frame Time: ${totalSpikeMs.toFixed(1)} ms | Total Excess Time Lost (> median): ${totalExcessMs.toFixed(1)} ms`);

  if (b === 'Vulkan') {
    const cats = {
      pipeline_miss_logged: { count: 0, spikeMs: 0, excessMs: 0 },
      io_load_save_boot: { count: 0, spikeMs: 0, excessMs: 0 },
      swapchain_window: { count: 0, spikeMs: 0, excessMs: 0 },
      unexplained_pacing: { count: 0, spikeMs: 0, excessMs: 0 },
    };
    for (const sp of allSpikes) {
      let c;
      if (sp.hasPipeLog) c = cats.pipeline_miss_logged;
      else if (sp.hasSwapchain) c = cats.swapchain_window;
      else if (sp.hasIo) c = cats.io_load_save_boot;
      else c = cats.unexplained_pacing;
      c.count++;
      c.spikeMs += sp.ftMs;
      c.excessMs += sp.excessMs;
    }
    for (const [k, v] of Object.entries(cats)) {
      console.log(`  ${k}: ${v.count} spikes (${((v.count / totalSpikes) * 100).toFixed(1)}%) | spikeMs=${v.spikeMs.toFixed(1)} ms (${((v.spikeMs / totalSpikeMs) * 100).toFixed(1)}%) | excessMs=${v.excessMs.toFixed(1)} ms (${((v.excessMs / totalExcessMs) * 100).toFixed(1)}%)`);
    }
  } else {
    const cats = {
      cold_or_growing_cache_sessions: { count: 0, spikeMs: 0, excessMs: 0 },
      warm_cache_io_load_save_boot: { count: 0, spikeMs: 0, excessMs: 0 },
      warm_cache_swapchain_window: { count: 0, spikeMs: 0, excessMs: 0 },
      warm_cache_unexplained_pacing: { count: 0, spikeMs: 0, excessMs: 0 },
    };
    for (const sp of allSpikes) {
      const isColdOrGrowing = sp.session.pipelinesPreloaded === 0 || sp.session.pipelinesBuiltInSession > 0;
      let c;
      if (isColdOrGrowing) c = cats.cold_or_growing_cache_sessions;
      else if (sp.hasSwapchain) c = cats.warm_cache_swapchain_window;
      else if (sp.hasIo) c = cats.warm_cache_io_load_save_boot;
      else c = cats.warm_cache_unexplained_pacing;
      c.count++;
      c.spikeMs += sp.ftMs;
      c.excessMs += sp.excessMs;
    }
    for (const [k, v] of Object.entries(cats)) {
      console.log(`  ${k}: ${v.count} spikes (${((v.count / totalSpikes) * 100).toFixed(1)}%) | spikeMs=${v.spikeMs.toFixed(1)} ms (${((v.spikeMs / totalSpikeMs) * 100).toFixed(1)}%) | excessMs=${v.excessMs.toFixed(1)} ms (${((v.excessMs / totalExcessMs) * 100).toFixed(1)}%)`);
    }
  }
}

// Vulkan fh1_frame_cost slow-window breakdown across all logs
const slowRegex = /GPU slow frames: (\d+) of (\d+) in (\d+) s over (\d+) ms \(worst (\d+) ms, typical (\d+) ms\); each: command processor ([^,]+), (\d+) ms GPU waits, (\d+) ms game waits, (\d+) draws \(typical (\d+)\); textures ([\d.]+) new \(([\d.]+) MB, ([\d.]+) ms\), ([\d.]+) loaded \(([\d.]+) MB, ([\d.]+) ms\), ([\d.]+) evicted \(([\d.]+) MB\); uploads ([\d.]+) MB \(([\d.]+) ms\); shaders ([\d.]+) \(([\d.]+) ms\), pipelines ([\d.]+) \(([\d.]+) ms\); render targets ([\d.]+) new \(([\d.]+) MB, ([\d.]+) ms\), (\d+) trims; resolve readbacks ([\d.]+) \(([\d.]+) MB, ([\d.]+) ms, ([\d.]+) ms GPU waits\); cause: (.+)$/;

let slowWindows = 0;
let slowFramesTotal = 0;
let slowFramesShaderPipe = 0;
let slowFramesGameWait = 0;
let slowFramesUnclear = 0;
let sumShaderMs = 0;
let sumPipelineMs = 0;
let sumTexMs = 0;
let sumUploadMs = 0;
let sumGpuWaitMs = 0;
let sumGameWaitMs = 0;

for (const lp of logFiles) {
  const lt = fs.readFileSync(lp, 'utf8');
  for (const line of lt.split(/\r?\n/)) {
    const m = line.match(slowRegex);
    if (!m) continue;
    slowWindows++;
    const n = parseInt(m[1], 10);
    const gpuWaitMs = parseFloat(m[8]);
    const gameWaitMs = parseFloat(m[9]);
    const texCreateMs = parseFloat(m[14]);
    const texLoadMs = parseFloat(m[17]);
    const uploadMs = parseFloat(m[21]);
    const shaderMs = parseFloat(m[23]);
    const pipelineMs = parseFloat(m[25]);

    slowFramesTotal += n;
    sumShaderMs += shaderMs * n;
    sumPipelineMs += pipelineMs * n;
    sumTexMs += (texCreateMs + texLoadMs) * n;
    sumUploadMs += uploadMs * n;
    sumGpuWaitMs += gpuWaitMs * n;
    sumGameWaitMs += gameWaitMs * n;

    if (shaderMs + pipelineMs >= 4.0) slowFramesShaderPipe += n;
    else if (gameWaitMs >= 4.0) slowFramesGameWait += n;
    else slowFramesUnclear += n;
  }
}

console.log('\n=== VULKAN FH1_FRAME_COST SLOW-FRAME WINDOWS (ALL LOGS) ===');
console.log({
  slowWindows,
  slowFramesTotal,
  slowFramesShaderPipe: `${slowFramesShaderPipe} (${((slowFramesShaderPipe / slowFramesTotal) * 100).toFixed(1)}%)`,
  slowFramesGameWait: `${slowFramesGameWait} (${((slowFramesGameWait / slowFramesTotal) * 100).toFixed(1)}%)`,
  slowFramesUnclear: `${slowFramesUnclear} (${((slowFramesUnclear / slowFramesTotal) * 100).toFixed(1)}%)`,
  sumShaderMs: sumShaderMs.toFixed(1),
  sumPipelineMs: sumPipelineMs.toFixed(1),
  sumShaderPlusPipelineMs: (sumShaderMs + sumPipelineMs).toFixed(1),
  sumTexMs: sumTexMs.toFixed(1),
  sumUploadMs: sumUploadMs.toFixed(1),
  sumGpuWaitMs: sumGpuWaitMs.toFixed(1),
  sumGameWaitMs: sumGameWaitMs.toFixed(1),
});
