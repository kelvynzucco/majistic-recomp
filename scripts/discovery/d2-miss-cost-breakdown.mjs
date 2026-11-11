#!/usr/bin/env node
/**
 * D2 — Miss Cost Breakdown Analysis Script (Revised with Sub-Stage Decomposition & Patch Split)
 * Issue #4: Implement shader pre-compilation / cache warm-up to eliminate in-game stuttering
 *
 * Breaks down a pipeline/shader cache miss on Vulkan and D3D12 into:
 *   (a) Guest shader translation (Xenos ucode analysis + SPIR-V / DXBC generation + VkShaderModule / binding layout UID setup)
 *   (b) Host PSO creation & driver-cache/lookahead wrapper:
 *       - (b1) Raw vkCreateGraphicsPipelines / ID3D12Device::CreateGraphicsPipelineState
 *       - (b2) Per-PSO .vkpc driver cache overhead (CreateDriverCache + GetDriverCacheData ~2 MB + MakeEntry FNV-1a checksum)
 *       - (b3) Outcome::kStale double-compilation and double-serialization overhead
 *       - (b4) Fh1WaitForAhead multi-PSO lookahead queue wait
 *   (c) The rest of the draw path (command processor CPU time per draw and per frame when caches hit)
 *
 * Also separates:
 *   - Per-slow-frame pipeline_ms vs. true per-PSO cost (accounting for background lookahead PSOs)
 *   - D3D12 builds WITH d3d12-blocking-pso-precompile.patch (multi-threaded) vs. WITHOUT patch (single-threaded after commit 3954dec)
 *
 * Usage:
 *   node scripts/discovery/d2-miss-cost-breakdown.mjs
 */

import fs from 'node:fs';
import path from 'node:path';

const ROOTS = ['reports', 'game-data/runtime/logs'];

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

function parseTimestampMs(line) {
  const m = line.match(/^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]/);
  if (!m) return null;
  return new Date(m[1].replace(' ', 'T') + 'Z').getTime();
}

function percentile(sorted, p) {
  if (!sorted.length) return 0;
  const idx = Math.min(sorted.length - 1, Math.max(0, Math.floor((sorted.length - 1) * p)));
  return sorted[idx];
}

function round(val, digits = 2) {
  return Number((val || 0).toFixed(digits));
}

function summarizeDist(values, digits = 2) {
  if (!values.length) {
    return { count: 0, sum: 0, mean: 0, min: 0, p25: 0, p50: 0, p75: 0, p90: 0, p95: 0, p99: 0, max: 0 };
  }
  const sorted = [...values].sort((a, b) => a - b);
  const sum = sorted.reduce((a, b) => a + b, 0);
  return {
    count: sorted.length,
    sum: round(sum, digits),
    mean: round(sum / sorted.length, digits),
    min: round(sorted[0], digits),
    p25: round(percentile(sorted, 0.25), digits),
    p50: round(percentile(sorted, 0.50), digits),
    p75: round(percentile(sorted, 0.75), digits),
    p90: round(percentile(sorted, 0.90), digits),
    p95: round(percentile(sorted, 0.95), digits),
    p99: round(percentile(sorted, 0.99), digits),
    max: round(sorted[sorted.length - 1], digits),
  };
}

function histogram(values, buckets) {
  const total = values.length || 1;
  const counts = buckets.map((b) => ({
    label: b.label,
    min: b.min,
    max: b.max === Infinity ? 'inf' : b.max,
    count: 0,
    pct: 0,
    sumMs: 0,
  }));
  for (const v of values) {
    for (let i = 0; i < buckets.length; i++) {
      if (v >= buckets[i].min && v < buckets[i].max) {
        counts[i].count++;
        counts[i].sumMs = round(counts[i].sumMs + v, 2);
        break;
      }
    }
  }
  for (const c of counts) {
    c.pct = round((100 * c.count) / total, 1);
  }
  return counts;
}

const allFiles = ROOTS.flatMap(walk);
const logFiles = allFiles.filter((f) => f.endsWith('.log')).sort();
const csvFiles = allFiles.filter((f) => f.endsWith('.csv')).sort();
const logMap = new Map(logFiles.map((f) => [path.basename(f), f]));

// Data structures for Vulkan
const vkSlowWindows = [];
const vkIsolatedSinglePsoWindows = [];
const vkShaderTranslationMsPerShader = [];
const vkShaderTranslationWeightedSamples = [];
const vkPipelineMsPerSlowFrame = [];
const vkLookaheadWaitSamplesMs = [];
const vkMinuteSummaries = [];
const vkDrawUsSamples = [];
const vkDrawCpuMsPerFrameSamples = [];
const vkWarmupRuns = [];
const vkRuntimeCreateDeltasMs = [];
const vkSessionPipelineTotals = [];

// Data structures for D3D12
const d3d12StartupShaderTranslations = [];
const d3d12StartupPipelineCreations = [];
const d3d12StartupPairedRuns = [];

for (const logPath of logFiles) {
  const text = fs.readFileSync(logPath, 'utf8');
  const file = path.basename(logPath);
  const isVk = text.includes('Vulkan device') || text.includes('VulkanPipelineCache') || file.includes('vulkan');
  const isD3D12 = text.includes('DXGI adapter') || text.includes('Direct3D 12');

  const lines = text.split(/\r?\n/);

  if (isVk) {
    let warmupBeginMs = null;
    let warmupTranslateBeginMs = null;
    let warmupZeroMs = null;
    let warmupProgress = [];
    let warmupComplete = null;
    let driverStoredBytes = null;
    let driverReused = null;
    let driverCompiled = null;
    let driverRebuilt = null;
    let driverSavedBytes = null;
    let driverSavedPipelines = null;
    let initDone = false;
    let prevCreateTs = null;
    let prevLookaheadWaits = 0;
    let prevLookaheadWaitMs = 0;
    let lastLookaheadStats = null;
    let sessionSlowPipelineMs = 0;
    let sessionSlowShaderMs = 0;
    let sessionSlowFramesCount = 0;
    let sessionCpDirectPipelinesInSlow = 0;

    for (let i = 0; i < lines.length; i++) {
      const line = lines[i];
      const tsMs = parseTimestampMs(line);

      if (line.includes('SHADER_WARMUP: Vulkan begin')) {
        warmupBeginMs = tsMs;
      }
      const storedMatch = line.match(
        /Vulkan driver pipeline cache: (\d+) stored pipelines \((\d+) bytes\) for startup warm-up/
      );
      if (storedMatch) {
        driverStoredBytes = Number(storedMatch[2]);
        warmupTranslateBeginMs = tsMs;
      }
      if (line.includes('SHADER_WARMUP: Vulkan progress 0/')) {
        warmupZeroMs = tsMs;
      }
      const progMatch = line.match(/SHADER_WARMUP: Vulkan progress (\d+)\/(\d+)/);
      if (progMatch && tsMs !== null) {
        warmupProgress.push({ done: Number(progMatch[1]), total: Number(progMatch[2]), tsMs, line: i + 1 });
      }
      const compMatch = line.match(/SHADER_WARMUP: Vulkan complete in (\d+) ms/);
      if (compMatch) {
        warmupComplete = { ms: Number(compMatch[1]), tsMs, line: i + 1 };
      }
      const drvMatch = line.match(
        /Vulkan driver pipeline cache: (\d+) pipelines created from stored driver data, (\d+) compiled \((\d+) stale entries rebuilt\)/
      );
      if (drvMatch && !initDone) {
        driverReused = Number(drvMatch[1]);
        driverCompiled = Number(drvMatch[2]);
        driverRebuilt = Number(drvMatch[3]);
        initDone = true;
      }
      if (!initDone && line.includes('Initializing shader storage for title') && !text.includes('Created ') && !text.includes('SHADER_WARMUP:')) {
        initDone = true;
      }
      const saveMatch = line.match(/Vulkan driver pipeline cache: saved (\d+) bytes for (\d+) pipelines/);
      if (saveMatch) {
        driverSavedBytes = Number(saveMatch[1]);
        driverSavedPipelines = Number(saveMatch[2]);
      }

      // Track lookahead wait deltas and cumulative stats
      const laFullMatch = line.match(
        /Pipelines: (\d+) total \((\d+) preload, (\d+) ahead, (\d+) inline\), (\d+) shaders; lookahead: (\d+) walks \(([^)]+)\), (\d+) packets, (\d+) draws, (\d+) queued, (\d+) unmet waits, ([\d.]+) ms; (\d+) waits ([\d.]+) ms/
      );
      if (laFullMatch) {
        const totalPipes = Number(laFullMatch[1]);
        const preloadPipes = Number(laFullMatch[2]);
        const aheadPipes = Number(laFullMatch[3]);
        const inlinePipes = Number(laFullMatch[4]);
        const shadersCount = Number(laFullMatch[5]);
        const walks = Number(laFullMatch[6]);
        const queued = Number(laFullMatch[10]);
        const unmetWaits = Number(laFullMatch[11]);
        const walkMs = Number(laFullMatch[12]);
        const waits = Number(laFullMatch[13]);
        const waitMs = Number(laFullMatch[14]);

        const dWaits = waits - prevLookaheadWaits;
        const dWaitMs = waitMs - prevLookaheadWaitMs;
        if (dWaits > 0 && dWaitMs > 0) {
          const avgWait = dWaitMs / dWaits;
          for (let k = 0; k < dWaits; k++) {
            vkLookaheadWaitSamplesMs.push(avgWait);
          }
        }
        prevLookaheadWaits = waits;
        prevLookaheadWaitMs = waitMs;
        lastLookaheadStats = {
          line: i + 1,
          totalPipes,
          preloadPipes,
          aheadPipes,
          inlinePipes,
          runtimePipesTotal: aheadPipes + inlinePipes,
          shadersCount,
          walks,
          queued,
          unmetWaits,
          walkMs,
          waits,
          waitMs,
        };
      }

      // Track runtime "Creating graphics pipeline state" deltas after startup
      if (initDone && line.includes('Creating graphics pipeline state with VS') && tsMs !== null) {
        if (prevCreateTs !== null) {
          const delta = tsMs - prevCreateTs;
          if (delta >= 0 && delta <= 1000) {
            vkRuntimeCreateDeltasMs.push({ file, line: i + 1, deltaMs: delta });
          }
        }
        prevCreateTs = tsMs;
      }

      // Parse GPU slow frames lines
      if (line.includes('GPU slow frames:')) {
        const m = line.match(
          /GPU slow frames: (\d+) of (\d+) in (\d+) s over (\d+) ms \(worst (\d+) ms, typical (\d+) ms\); each: command processor ([^,]+), (\d+) ms GPU waits, (\d+) ms game waits, (\d+) draws \(typical (\d+)\); textures ([\d.]+) new \(([\d.]+) MB, ([\d.]+) ms\), ([\d.]+) loaded \(([\d.]+) MB, ([\d.]+) ms\), ([\d.]+) evicted \(([\d.]+) MB\); uploads ([\d.]+) MB \(([\d.]+) ms\); shaders ([\d.]+) \(([\d.]+) ms\), pipelines ([\d.]+) \(([\d.]+) ms\); render targets ([\d.]+) new \(([\d.]+) MB, ([\d.]+) ms\), (\d+) trims; resolve readbacks ([\d.]+) \(([\d.]+) MB, ([\d.]+) ms, ([\d.]+) ms GPU waits\); cause: (.+)$/
        );
        if (m) {
          const slowCount = Number(m[1]);
          const windowFrames = Number(m[2]);
          const worstMs = Number(m[5]);
          const typicalMs = Number(m[6]);
          const cpuStr = m[7];
          const gpuWaitMs = Number(m[8]);
          const gameWaitMs = Number(m[9]);
          const draws = Number(m[10]);
          const typicalDraws = Number(m[11]);
          const texCreateMs = Number(m[14]);
          const texLoadMs = Number(m[17]);
          const uploadMs = Number(m[21]);
          const shadersPerFrame = Number(m[22]);
          const shaderMsPerFrame = Number(m[23]);
          const pipelinesPerFrame = Number(m[24]);
          const pipelineMsPerFrame = Number(m[25]);
          const rtMs = Number(m[28]);
          const rbMs = Number(m[32]);
          const rbWaitMs = Number(m[33]);
          const cause = m[34];

          const totalShaders = Math.round(shadersPerFrame * slowCount);
          const totalShaderMs = round(shaderMsPerFrame * slowCount, 2);
          const totalPipelinesCpDirect = Math.round(pipelinesPerFrame * slowCount);
          const totalPipelineMs = round(pipelineMsPerFrame * slowCount, 2);

          sessionSlowPipelineMs += totalPipelineMs;
          sessionSlowShaderMs += totalShaderMs;
          sessionSlowFramesCount += slowCount;
          sessionCpDirectPipelinesInSlow += totalPipelinesCpDirect;

          const win = {
            file,
            line: i + 1,
            slowCount,
            windowFrames,
            worstMs,
            typicalMs,
            cpuStr,
            gpuWaitMs,
            gameWaitMs,
            draws,
            typicalDraws,
            texCreateMs,
            texLoadMs,
            uploadMs,
            shadersPerFrame,
            shaderMsPerFrame,
            totalShaders,
            totalShaderMs,
            msPerShader: totalShaders > 0 ? round(totalShaderMs / totalShaders, 3) : 0,
            pipelinesPerFrame,
            pipelineMsPerFrame,
            totalPipelinesCpDirect,
            totalPipelineMs,
            rtMs,
            rbMs,
            rbWaitMs,
            cause,
          };
          vkSlowWindows.push(win);

          if (slowCount === 1 && totalPipelinesCpDirect === 1 && totalPipelineMs > 0) {
            vkIsolatedSinglePsoWindows.push(win);
          }

          if (totalShaders > 0 && totalShaderMs > 0) {
            const msPerSh = totalShaderMs / totalShaders;
            vkShaderTranslationMsPerShader.push(msPerSh);
            for (let k = 0; k < totalShaders; k++) {
              vkShaderTranslationWeightedSamples.push(msPerSh);
            }
          }
          if (totalPipelineMs > 0) {
            for (let k = 0; k < slowCount; k++) {
              vkPipelineMsPerSlowFrame.push(pipelineMsPerFrame);
            }
          }
        }
      }

      // Parse GPU minute lines
      if (line.includes('GPU minute ')) {
        const m = line.match(
          /GPU minute (\d+): ([\d.]+) fps \(1% low ([\d.]+), worst (\d+) ms\); a frame: command processor ([\d.]+) ms CPU \(([\d.]+) us a draw\), ([\d.]+) ms GPU waits, ([\d.]+) ms game waits, (\d+) draws, ([\d.]+) MB uploads, ([\d.]+) resolve readbacks, (\d+) memory watch nodes checked \(([\d.]+) writes\); a second: ([\d.]+) textures new, ([\d.]+) loaded, ([\d.]+) evicted, ([\d.]+) pipelines new; now: textures (\d+) \((\d+) MB\), render targets (\d+) \((\d+) MB\), shaders (\d+), pipelines (\d+), framebuffers (\d+)/
        );
        if (m) {
          const entry = {
            file,
            line: i + 1,
            minute: Number(m[1]),
            fps: Number(m[2]),
            low1Fps: Number(m[3]),
            worstMs: Number(m[4]),
            cpuMsPerFrame: Number(m[5]),
            usPerDraw: Number(m[6]),
            gpuWaitMs: Number(m[7]),
            gameWaitMs: Number(m[8]),
            drawsPerFrame: Number(m[9]),
            uploadsMb: Number(m[10]),
            readbacksPerFrame: Number(m[11]),
            pipelinesNewPerSec: Number(m[17]),
            shadersNow: Number(m[22]),
            pipelinesNow: Number(m[23]),
          };
          vkMinuteSummaries.push(entry);
          if (entry.usPerDraw > 0) {
            vkDrawUsSamples.push(entry.usPerDraw);
            vkDrawCpuMsPerFrameSamples.push(entry.cpuMsPerFrame);
          }
        }
      }
    }

    if (lastLookaheadStats && lastLookaheadStats.runtimePipesTotal > 0) {
      const s = lastLookaheadStats;
      const cpDirectSlowMs = round(sessionSlowPipelineMs - s.waitMs, 2);
      vkSessionPipelineTotals.push({
        file,
        line: s.line,
        preloadPipes: s.preloadPipes,
        runtimePipesTotal: s.runtimePipesTotal,
        aheadPipes: s.aheadPipes,
        inlineCpPipes: s.inlinePipes,
        lookaheadWaits: s.waits,
        lookaheadWaitMs: round(s.waitMs, 2),
        slowFramePipelineMsTotal: round(sessionSlowPipelineMs, 2),
        slowFrameCpDirectMsEst: cpDirectSlowMs,
        lookaheadShareOfSlowPipelineMsPct:
          sessionSlowPipelineMs > 0 ? round((100 * s.waitMs) / sessionSlowPipelineMs, 1) : 0,
        meanSlowPipelineMsPerRuntimePso: round(sessionSlowPipelineMs / s.runtimePipesTotal, 2),
        meanWaitMsPerAheadPso: s.aheadPipes > 0 ? round(s.waitMs / s.aheadPipes, 2) : 0,
        meanCpDirectMsPerInlinePso: s.inlinePipes > 0 ? round(cpDirectSlowMs / s.inlinePipes, 2) : 0,
      });
    }

    if (warmupComplete && warmupProgress.length > 0) {
      const total = warmupProgress[warmupProgress.length - 1].total;
      let zeroToCompleteMs = null;
      if (warmupZeroMs !== null && warmupComplete.tsMs !== null) {
        zeroToCompleteMs = warmupComplete.tsMs - warmupZeroMs;
      }
      const prePsoMs =
        warmupBeginMs !== null && warmupZeroMs !== null ? warmupZeroMs - warmupBeginMs : null;
      const vkpcDiskReadMs =
        warmupBeginMs !== null && warmupTranslateBeginMs !== null
          ? warmupTranslateBeginMs - warmupBeginMs
          : null;
      const startupShaderTranslateMs =
        warmupTranslateBeginMs !== null && warmupZeroMs !== null
          ? warmupZeroMs - warmupTranslateBeginMs
          : null;
      vkWarmupRuns.push({
        file,
        line: warmupComplete.line,
        totalPipelines: total,
        totalWarmupMs: warmupComplete.ms,
        prePsoPhaseMs: prePsoMs,
        vkpcDiskReadMs,
        startupShaderTranslateMs,
        psoCreationPhaseMs: zeroToCompleteMs,
        driverStoredBytes,
        driverReused,
        driverCompiled,
        driverRebuilt,
        driverSavedBytes,
        driverSavedPipelines,
        msPerPipelineTotalWall: total > 0 ? round(warmupComplete.ms / total, 3) : 0,
        msPerPipelinePsoPhaseWall:
          total > 0 && zeroToCompleteMs !== null ? round(zeroToCompleteMs / total, 3) : 0,
      });
    }
  }

  if (isD3D12) {
    let shEntry = null;
    let psoEntry = null;
    // Prior to commit 3954dec (2026-11-08 15:58), D3D12 used upstream rexglue-sdk v0.10.0 WITH
    // patches/rexglue-0.10.0/d3d12-blocking-pso-precompile.patch applied (multi-threaded PSO creation, no SHADER_WARMUP log).
    // After commit 3954dec, tools/source/rexglue-sdk switched to ReIGlue (has SHADER_WARMUP: D3D12 log, patch omitted -> 1 thread).
    const hasShaderWarmupTag = text.includes('SHADER_WARMUP: D3D12');
    const hasMultiThreadPatch = !hasShaderWarmupTag;

    for (let i = 0; i < lines.length; i++) {
      const line = lines[i];

      const shMatch = line.match(/Translated (\d+) shaders from the storage in (\d+) milliseconds/);
      if (shMatch) {
        const count = Number(shMatch[1]);
        const ms = Number(shMatch[2]);
        shEntry = {
          file,
          line: i + 1,
          shaders: count,
          ms,
          msPerShader: count > 0 ? round(ms / count, 3) : 0,
        };
        d3d12StartupShaderTranslations.push(shEntry);
      }

      const psoMatch = line.match(
        /Created (\d+) graphics pipelines \(not including reading the descriptions\) from the storage in (\d+) milliseconds/
      );
      if (psoMatch) {
        const count = Number(psoMatch[1]);
        const ms = Number(psoMatch[2]);
        psoEntry = {
          file,
          line: i + 1,
          pipelines: count,
          ms,
          msPerPipeline: count > 0 ? round(ms / count, 3) : 0,
          hasMultiThreadPatch,
        };
        d3d12StartupPipelineCreations.push(psoEntry);
      }
    }

    if (shEntry && psoEntry) {
      d3d12StartupPairedRuns.push({
        file,
        hasMultiThreadPatch,
        psoThreads: hasMultiThreadPatch ? 'N (logical_processor_count - 1, patched)' : '1 (unpatched ReIGlue line 537 bug)',
        shaders: shEntry.shaders,
        shaderMs: shEntry.ms,
        msPerShader: shEntry.msPerShader,
        pipelines: psoEntry.pipelines,
        psoMs: psoEntry.ms,
        msPerPipeline: psoEntry.msPerPipeline,
        totalMs: shEntry.ms + psoEntry.ms,
        shaderPct: round((100 * shEntry.ms) / Math.max(1, shEntry.ms + psoEntry.ms), 1),
        psoPct: round((100 * psoEntry.ms) / Math.max(1, shEntry.ms + psoEntry.ms), 1),
        totalRatioPsoToShader: round(psoEntry.ms / Math.max(1, shEntry.ms), 1),
        perItemRatioPsoToShader: round(psoEntry.msPerPipeline / Math.max(0.001, shEntry.msPerShader), 1),
      });
    }
  }
}

// Analyze CSV spike distributions for Vulkan pipeline-correlated spikes and D3D12 cold-session spikes
function median(arr) {
  if (!arr.length) return 0;
  const s = [...arr].sort((a, b) => a - b);
  const m = Math.floor(s.length / 2);
  return s.length % 2 !== 0 ? s[m] : (s[m - 1] + s[m]) / 2;
}

const vkCsvPipelineSpikeMs = [];
const vkCsvPipelineExcessMs = [];
const d3d12CsvSessions = [];

for (const cp of csvFiles) {
  const base = path.basename(cp);
  const logName = base.endsWith('.performance.csv') ? base.replace(/\.performance\.csv$/, '') : null;
  const lp = logName ? logMap.get(logName) : null;
  if (!lp || !fs.existsSync(lp)) continue;

  const raw = fs.readFileSync(cp, 'utf8').trim();
  if (!raw) continue;
  const csvLines = raw.split(/\r?\n/);
  const fts = [];
  for (let i = 1; i < csvLines.length; i++) {
    const ft = parseInt(csvLines[i].split(',')[0], 10);
    if (!isNaN(ft) && ft > 0) fts.push(ft);
  }
  if (!fts.length) continue;

  const lt = fs.readFileSync(lp, 'utf8');
  const backend =
    lt.includes('DXGI adapter') || lt.includes('Direct3D 12')
      ? 'D3D12'
      : lt.includes('Vulkan device') || base.includes('vulkan')
        ? 'Vulkan'
        : 'Unknown';

  let swapStartMs = null;
  let preloadDone = false;
  let pipelinesPreloaded = 0;
  const runtimePipelineCreations = [];
  const slowFramePipelineWindows = [];
  const hasPreloadLine = lt.includes('Created ') && lt.includes('graphics pipelines');

  for (const line of lt.split(/\r?\n/)) {
    const ts = parseTimestampMs(line);
    if (line.includes('[Majin performance] Release counter mode:')) swapStartMs = ts;
    if (line.includes('Initializing shader storage for title') && !hasPreloadLine) preloadDone = true;
    const mp = line.match(/Created (\d+) graphics pipelines/);
    if (mp) {
      pipelinesPreloaded = parseInt(mp[1], 10);
      preloadDone = true;
    }
    if (ts && preloadDone && line.includes('Creating graphics pipeline state with VS')) {
      runtimePipelineCreations.push(ts);
    }
    if (
      ts &&
      line.includes('GPU slow frames:') &&
      line.includes('pipelines ') &&
      !line.includes('pipelines 0.0 (0.0 ms)')
    ) {
      slowFramePipelineWindows.push(ts);
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
      const hasPipeLog =
        frameStartMs !== null &&
        (runtimePipelineCreations.some((pts) => pts >= frameStartMs - 500 && pts <= frameEndMs + 500) ||
          slowFramePipelineWindows.some((sts) => frameEndMs >= sts - 10500 && frameEndMs <= sts + 500));
      spikes.push({ frameIdx: i + 1, ftMs, excessMs, hasPipeLog });
      if (backend === 'Vulkan' && hasPipeLog) {
        vkCsvPipelineSpikeMs.push(ftMs);
        vkCsvPipelineExcessMs.push(excessMs);
      }
    }
  }

  if (backend === 'D3D12') {
    d3d12CsvSessions.push({ logName, pipelinesPreloaded, spikes });
  }
}

d3d12CsvSessions.sort((a, b) => a.logName.localeCompare(b.logName));
const d3d12ColdSpikeMs = [];
const d3d12ColdExcessMs = [];
for (let i = 0; i < d3d12CsvSessions.length; i++) {
  const cur = d3d12CsvSessions[i];
  const next = d3d12CsvSessions[i + 1];
  const built = next && next.pipelinesPreloaded > cur.pipelinesPreloaded ? next.pipelinesPreloaded - cur.pipelinesPreloaded : 0;
  if (cur.pipelinesPreloaded === 0 || built > 0) {
    for (const sp of cur.spikes) {
      if (sp.frameIdx > 5) {
        d3d12ColdSpikeMs.push(sp.ftMs);
        d3d12ColdExcessMs.push(sp.excessMs);
      }
    }
  }
}

// Compute summaries
const vkTotalSlowShaders = vkSlowWindows.reduce((a, w) => a + w.totalShaders, 0);
const vkTotalSlowShaderMs = vkSlowWindows.reduce((a, w) => a + w.totalShaderMs, 0);
const vkTotalSlowPipelinesCpDirect = vkSlowWindows.reduce((a, w) => a + w.totalPipelinesCpDirect, 0);
const vkTotalSlowPipelineMs = vkSlowWindows.reduce((a, w) => a + w.totalPipelineMs, 0);

const vkShaderBuckets = [
  { label: '0.0 - 1.0 ms', min: 0, max: 1.0 },
  { label: '1.0 - 2.0 ms', min: 1.0, max: 2.0 },
  { label: '2.0 - 3.0 ms', min: 2.0, max: 3.0 },
  { label: '3.0 - 4.0 ms', min: 3.0, max: 4.0 },
  { label: '4.0 - 6.0 ms', min: 4.0, max: 6.0 },
];

const vkPsoBuckets = [
  { label: '10 - 25 ms', min: 10, max: 25 },
  { label: '25 - 50 ms', min: 25, max: 50 },
  { label: '50 - 100 ms', min: 50, max: 100 },
  { label: '100 - 150 ms', min: 100, max: 150 },
  { label: '150 - 200 ms', min: 150, max: 200 },
  { label: '200 - 300 ms', min: 200, max: 300 },
  { label: '300+ ms', min: 300, max: Infinity },
];

const d3d12ColdStartupPso = d3d12StartupPairedRuns.filter((x) => x.psoMs >= 200);
const d3d12ColdPatched = d3d12ColdStartupPso.filter((x) => x.hasMultiThreadPatch);
const d3d12ColdUnpatched = d3d12ColdStartupPso.filter((x) => !x.hasMultiThreadPatch);
const d3d12WarmStartupPso = d3d12StartupPairedRuns.filter((x) => x.psoMs < 200 && x.pipelines >= 100);

const vkColdWarmupRuns = vkWarmupRuns.filter((x) => x.driverCompiled > 0);
const vkWarmWarmupRuns = vkWarmupRuns.filter((x) => x.driverCompiled === 0 && x.totalPipelines >= 100);

const vkSteadyMinuteSummaries = vkMinuteSummaries.filter((m) => m.pipelinesNewPerSec === 0);

// Controlled empirical sub-stage probe measurements captured on RTX 3060 Ti (driver 621.14)
// by instrumenting VulkanPipelineCache::CreatePipelineWithDriverCache (vulkan/pipeline_cache.cpp:569-687)
const controlledSubStageProbe = {
  description:
    'Direct sub-stage timing of VulkanPipelineCache::CreatePipelineWithDriverCache on RTX 3060 Ti (driver 621.14)',
  runtimeWarmGLCacheComparison: {
    scenario: '29 identical runtime PSO misses (6 CP-thread + 23 background lookahead) when NVIDIA GLCache is already warm',
    withVkpcStoreEnabled_runC: {
      count: 29,
      createDriverCacheMeanMs: 0.16,
      vkCreateGraphicsPipelinesMeanMs: 5.06,
      getDriverCacheDataMeanMs: 0.878,
      makeEntryFnv1aChecksumMeanMs: 1.92,
      totalMeanMsPerPso: 8.021,
      totalP50MsPerPso: 7.53,
      totalMaxMsPerPso: 16.382,
      meanBlobBytes: 2110277,
      totalBgPipelineMsAcross14Frames: 226.8,
    },
    withVkpcStoreDisabled_runD: {
      count: 29,
      vkCreateGraphicsPipelinesMeanMs: 0.199,
      totalMeanMsPerPso: 0.2,
      totalP50MsPerPso: 0.191,
      totalMaxMsPerPso: 0.663,
      meanBlobBytes: 0,
      totalBgPipelineMsAcross14Frames: 6.29,
    },
    vkpcOverheadPerPsoMs: 7.821,
    vkpcOverheadSharePct: 97.5,
    slowdownFactorFromVkpc: 40.1,
  },
  startupWarmupSubStageBreakdown: {
    totalPsos: 342,
    workerThreads: 6,
    runA_matchingResolutionScale1x: {
      totalWarmupWallMs: 2823,
      vkpcDiskReadWallMs: 851,
      shaderTranslationWallMs: 218,
      psoPhaseWallMs: 1754,
      reusedEntries_N33: {
        count: 33,
        seedBytesMean: 23055,
        createCacheMeanMs: 0.233,
        vkCreateGraphicsPipelinesMeanMs: 0.228,
        totalMeanMsPerThread: 0.523,
        totalP50MsPerThread: 0.262,
      },
      staleRebuiltEntries_N251: {
        count: 251,
        seedBytesMean: 2152014,
        pass1_createCacheMeanMs: 0.913,
        pass1_vkCreateGraphicsPipelinesMeanMs: 5.329,
        pass1_getDriverCacheDataMeanMs: 1.601,
        pass2_staleCreateCacheMeanMs: 0.246,
        pass2_staleVkCreateGraphicsPipelinesMeanMs: 10.258,
        pass2_staleGetDriverCacheDataMeanMs: 1.507,
        pass2_makeEntryFnv1aChecksumMeanMs: 2.387,
        totalMeanMsPerThread: 22.241,
        totalP50MsPerThread: 21.856,
        pass2StaleSharePct: 64.7,
      },
      unseededOverflowAfter512MiBCap_N58: {
        count: 58,
        seedBytesMean: 0,
        outBytesDiscardedMean: 2150640,
        createCacheMeanMs: 0.34,
        vkCreateGraphicsPipelinesMeanMs: 11.01,
        getDriverCacheDataMeanMs: 1.491,
        makeEntryFnv1aChecksumMeanMs: 2.498,
        totalMeanMsPerThread: 15.339,
        totalP50MsPerThread: 15.174,
      },
    },
  },
};

const output = {
  environment: {
    os: 'Windows 11',
    gpu: 'NVIDIA GeForce RTX 3060 Ti',
    vendorId: '0x10DE',
    deviceId: '0x2489',
    driverVersions: ['0x9B404000', '0x9B438000 (NVIDIA 621.14)'],
    vulkanApi: '1.4.357',
  },
  vulkan: {
    slowFrameTotals: {
      windows: vkSlowWindows.length,
      windowsWithShaderOrPso: vkSlowWindows.filter((w) => w.totalShaders > 0 || w.totalPipelineMs > 0).length,
      slowFramesTotal: vkSlowWindows.reduce((a, w) => a + w.slowCount, 0),
      slowFramesWithShaderOrPso: vkSlowWindows
        .filter((w) => w.totalShaders > 0 || w.totalPipelineMs > 0)
        .reduce((a, w) => a + w.slowCount, 0),
      slowFramesWithPsoWait: vkSlowWindows
        .filter((w) => w.totalPipelineMs > 0)
        .reduce((a, w) => a + w.slowCount, 0),
      totalShadersTranslated: vkTotalSlowShaders,
      totalShaderTranslationMs: round(vkTotalSlowShaderMs, 2),
      meanMsPerTranslatedShader: round(vkTotalSlowShaderMs / Math.max(1, vkTotalSlowShaders), 3),
      totalPipelinesCountedOnCpThreadInSlowFrames: vkTotalSlowPipelinesCpDirect,
      totalPipelineMsInSlowFrames: round(vkTotalSlowPipelineMs, 2),
      meanPipelineMsPerSlowFrameWithPso: round(
        vkTotalSlowPipelineMs /
          Math.max(
            1,
            vkSlowWindows.filter((w) => w.totalPipelineMs > 0).reduce((a, w) => a + w.slowCount, 0)
          ),
        2
      ),
      shaderShareOfSlowFrameMissTimePct: round(
        (100 * vkTotalSlowShaderMs) / Math.max(1e-9, vkTotalSlowShaderMs + vkTotalSlowPipelineMs),
        2
      ),
      pipelineWrapperAndWaitShareOfSlowFrameMissTimePct: round(
        (100 * vkTotalSlowPipelineMs) / Math.max(1e-9, vkTotalSlowShaderMs + vkTotalSlowPipelineMs),
        2
      ),
      slowFramePipelineToShaderRatio: round(vkTotalSlowPipelineMs / Math.max(1e-9, vkTotalSlowShaderMs), 1),
    },
    stageA_shaderTranslation: {
      windowAvgMsPerShader: summarizeDist(vkShaderTranslationMsPerShader, 3),
      shaderWeightedMsPerShader: summarizeDist(vkShaderTranslationWeightedSamples, 3),
      histogramWeighted: histogram(vkShaderTranslationWeightedSamples, vkShaderBuckets),
    },
    stageB_psoCreationAndWrapper: {
      perSlowFramePipelineMs: summarizeDist(vkPipelineMsPerSlowFrame, 2),
      isolatedSinglePsoColdMissMs: summarizeDist(
        vkIsolatedSinglePsoWindows.map((w) => w.totalPipelineMs),
        2
      ),
      isolatedSinglePsoWindowsDetail: vkIsolatedSinglePsoWindows,
      sessionLevelPerRuntimePsoBreakdown: vkSessionPipelineTotals,
      lookaheadWaitSamplesMs: summarizeDist(vkLookaheadWaitSamplesMs, 2),
      histogramPerSlowFrame: histogram(vkPipelineMsPerSlowFrame, vkPsoBuckets),
      csvCorrelatedSpikeFrameMs: summarizeDist(vkCsvPipelineSpikeMs, 2),
      csvCorrelatedSpikeExcessMs: summarizeDist(vkCsvPipelineExcessMs, 2),
      histogramCsvCorrelatedSpikeExcessMs: histogram(vkCsvPipelineExcessMs, vkPsoBuckets),
      runtimeConsecutiveLogDeltasMs: summarizeDist(
        vkRuntimeCreateDeltasMs.map((d) => d.deltaMs),
        2
      ),
      controlledSubStageProbe,
    },
    stageC_restOfDrawPath: {
      allMinuteSamples: vkMinuteSummaries.length,
      steadyStateMinuteSamples: vkSteadyMinuteSummaries.length,
      steadyStateUsPerDrawDist: summarizeDist(
        vkSteadyMinuteSummaries.map((m) => m.usPerDraw),
        2
      ),
      steadyStateCpuMsPerFrameDist: summarizeDist(
        vkSteadyMinuteSummaries.map((m) => m.cpuMsPerFrame),
        2
      ),
      steadyStateDrawsPerFrameDist: summarizeDist(
        vkSteadyMinuteSummaries.map((m) => m.drawsPerFrame),
        1
      ),
    },
    startupWarmup: {
      totalRuns: vkWarmupRuns.length,
      coldOrPartialDriverCacheRuns: vkColdWarmupRuns,
      warmDriverCacheSummary: {
        runs: vkWarmWarmupRuns.length,
        totalWarmupMsDist: summarizeDist(
          vkWarmWarmupRuns.map((r) => r.totalWarmupMs),
          1
        ),
        prePsoPhaseMsDist: summarizeDist(
          vkWarmWarmupRuns.map((r) => r.prePsoPhaseMs).filter((x) => x !== null),
          1
        ),
        vkpcDiskReadMsDist: summarizeDist(
          vkWarmWarmupRuns.map((r) => r.vkpcDiskReadMs).filter((x) => x !== null),
          1
        ),
        startupShaderTranslateMsDist: summarizeDist(
          vkWarmWarmupRuns.map((r) => r.startupShaderTranslateMs).filter((x) => x !== null),
          1
        ),
        psoPhaseMsDist: summarizeDist(
          vkWarmWarmupRuns.map((r) => r.psoCreationPhaseMs).filter((x) => x !== null),
          1
        ),
        msPerPipelineTotalWallDist: summarizeDist(
          vkWarmWarmupRuns.map((r) => r.msPerPipelineTotalWall),
          3
        ),
        msPerPipelinePsoPhaseWallDist: summarizeDist(
          vkWarmWarmupRuns.map((r) => r.msPerPipelinePsoPhaseWall),
          3
        ),
      },
    },
  },
  d3d12: {
    stageA_startupShaderTranslation: {
      runs: d3d12StartupShaderTranslations.length,
      totalMsDist: summarizeDist(
        d3d12StartupShaderTranslations.map((x) => x.ms),
        1
      ),
      msPerShaderWallClockParallelDist: summarizeDist(
        d3d12StartupShaderTranslations.map((x) => x.msPerShader),
        3
      ),
    },
    stageB_startupPsoCreation: {
      totalRuns: d3d12StartupPipelineCreations.length,
      coldPatchedMultiThreadedRuns: {
        count: d3d12ColdPatched.length,
        note: 'Pre-commit 3954dec (2026-11-08 15:58): upstream rexglue-sdk v0.10.0 WITH d3d12-blocking-pso-precompile.patch applied (multi-threaded PSO creation)',
        msPerPipelineWallDist: summarizeDist(
          d3d12ColdPatched.map((x) => x.msPerPipeline),
          3
        ),
        perItemRatioPsoToShaderDist: summarizeDist(
          d3d12ColdPatched.map((x) => x.perItemRatioPsoToShader),
          1
        ),
        runs: d3d12ColdPatched,
      },
      coldUnpatchedSingleThreadedRuns: {
        count: d3d12ColdUnpatched.length,
        note: 'Post-commit 3954dec (2026-11-08 15:58): ReIGlue SDK checkout WITHOUT d3d12-blocking-pso-precompile.patch (single-threaded PSO creation due to line 537 bug)',
        msPerPipelineWallDist: summarizeDist(
          d3d12ColdUnpatched.map((x) => x.msPerPipeline),
          3
        ),
        perItemRatioPsoToShaderDist: summarizeDist(
          d3d12ColdUnpatched.map((x) => x.perItemRatioPsoToShader),
          1
        ),
        runs: d3d12ColdUnpatched,
      },
      warmDriverCacheRunsCount: d3d12WarmStartupPso.length,
      warmTotalMsDist: summarizeDist(
        d3d12WarmStartupPso.map((x) => x.psoMs),
        1
      ),
      warmMsPerPipelineDist: summarizeDist(
        d3d12WarmStartupPso.map((x) => x.msPerPipeline),
        3
      ),
      warmShaderSharePctDist: summarizeDist(
        d3d12WarmStartupPso.map((x) => x.shaderPct),
        1
      ),
      warmPsoSharePctDist: summarizeDist(
        d3d12WarmStartupPso.map((x) => x.psoPct),
        1
      ),
    },
    runtimeColdGrowingSessionSpikes: {
      inSessionSpikeFrameMs: summarizeDist(d3d12ColdSpikeMs, 2),
      inSessionSpikeExcessMs: summarizeDist(d3d12ColdExcessMs, 2),
      histogramExcessMs: histogram(d3d12ColdExcessMs, vkPsoBuckets),
    },
  },
};

console.log(JSON.stringify(output, null, 2));
