#!/usr/bin/env node
/**
 * scripts/analyze-performance-csv.mjs
 *
 * Standalone Performance CSV Analyzer & Verification Gate
 *
 * Computes deterministic frame-time percentiles, spike metrics, and stutter rates
 * from .performance.csv or normalized benchmark CSV files.
 *
 * Supports:
 *   - Direct CSV analysis (full session or bounded by explicit frame range)
 *   - Log-marker lifecycle correlation (--log=<path>): detects CSV anchor timestamp,
 *     identifies 3D gameplay start (flow_a / cutscene bypass) and save I/O (ContentCreateEx),
 *     and separates full session from pure active gameplay.
 *   - Multi-run N-sample aggregation (mean, min, max, std dev across runs)
 *   - Automated verification gate (--verify / --gate) evaluating against official D8 thresholds
 *
 * Usage:
 *   node scripts/analyze-performance-csv.mjs --input=reports/baseline-shader/baseline-vulkan-cold-run1.csv --log=game-data/runtime/logs/runtime-20261108-114458.log
 *   node scripts/analyze-performance-csv.mjs --input=run1.csv,run2.csv,run3.csv --gate
 *   node scripts/analyze-performance-csv.mjs --input=reports/baseline-shader/baseline-vulkan-warm-*.csv --json
 */

import fs from 'fs';
import path from 'path';

function printUsage() {
  console.error(`Usage: node scripts/analyze-performance-csv.mjs --input=<csv_file> [options]

Options:
  --input=<path,...>    Path to CSV file(s). Supports comma-separated list or multiple flags.
  --log=<path,...>      Matching runtime log file(s) for lifecycle marker correlation.
  --start-frame=<N>     Explicit starting frame index (inclusive). Overrides log detection.
  --end-frame=<N>       Explicit ending frame index (inclusive). Overrides log detection.
  --gameplay-only       Restrict metrics and gate evaluation to active 3D gameplay window.
  --exclude-save-io     Exclude frames occurring during ContentCreateEx save disk I/O.
  --verify, --gate      Evaluate against official D8 thresholds (exit code 1 if failed).
  --max-p999=<ms>       Custom P99.9 threshold in ms (default: 33.33).
  --max-stalls-100=<N>  Custom stalls > 100 ms threshold (default: 0).
  --max-stalls-33=<N>   Custom stalls > 33.33 ms threshold (default: 8 full / 3 gameplay-only).
  --max-excess-rate=<ms>Custom excess spike ms per 1k frames threshold (default: 50.0).
  --json                Emit machine-readable JSON to stdout (default: pretty JSON).
  -h, --help            Show this help message.
`);
}

const args = process.argv.slice(2);
const inputPaths = [];
const logPaths = [];
let startFrameArg = null;
let endFrameArg = null;
let gameplayOnly = false;
let excludeSaveIO = false;
let verifyGate = false;
let compactJson = false;

let threshP999 = 33.33;
let threshStalls100 = 0;
let threshStalls33 = null; // null = set dynamically based on gameplayOnly
let threshExcessRate = 50.0;

for (const arg of args) {
  if (arg.startsWith('--input=')) {
    const val = arg.substring('--input='.length);
    for (const p of val.split(',')) {
      if (p.trim()) inputPaths.push(p.trim());
    }
  } else if (arg.startsWith('--log=')) {
    const val = arg.substring('--log='.length);
    for (const p of val.split(',')) {
      if (p.trim()) logPaths.push(p.trim());
    }
  } else if (arg.startsWith('--start-frame=')) {
    startFrameArg = parseInt(arg.substring('--start-frame='.length), 10);
  } else if (arg.startsWith('--end-frame=')) {
    endFrameArg = parseInt(arg.substring('--end-frame='.length), 10);
  } else if (arg === '--gameplay-only') {
    gameplayOnly = true;
  } else if (arg === '--exclude-save-io') {
    excludeSaveIO = true;
  } else if (arg === '--verify' || arg === '--gate') {
    verifyGate = true;
  } else if (arg === '--json') {
    compactJson = true;
  } else if (arg.startsWith('--max-p999=')) {
    threshP999 = parseFloat(arg.substring('--max-p999='.length));
  } else if (arg.startsWith('--max-stalls-100=')) {
    threshStalls100 = parseInt(arg.substring('--max-stalls-100='.length), 10);
  } else if (arg.startsWith('--max-stalls-33=')) {
    threshStalls33 = parseInt(arg.substring('--max-stalls-33='.length), 10);
  } else if (arg.startsWith('--max-excess-rate=')) {
    threshExcessRate = parseFloat(arg.substring('--max-excess-rate='.length));
  } else if (arg === '--help' || arg === '-h') {
    printUsage();
    process.exit(0);
  } else if (!arg.startsWith('--')) {
    inputPaths.push(arg);
  }
}

if (threshStalls33 === null) {
  threshStalls33 = gameplayOnly ? 3 : 8;
}

if (inputPaths.length === 0) {
  console.error('Error: No input CSV file specified. Use --input=<path>');
  printUsage();
  process.exit(1);
}

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

function percentile(sorted, p) {
  if (sorted.length === 0) return 0;
  const idx = Math.min(sorted.length - 1, Math.max(0, Math.ceil(sorted.length * p) - 1));
  return sorted[idx] / 1000; // ms
}

function computeMetrics(frames) {
  if (!frames || frames.length === 0) return null;

  const fts = frames.map(f => f.us);
  const sorted = [...fts].sort((a, b) => a - b);
  const totalUs = fts.reduce((a, b) => a + b, 0);
  const durationS = totalUs / 1000000;
  const avgFps = fts.length / (durationS || 1);

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
        frameIndex: frames[i].frameNum,
        frameTimeMs: ft / 1000,
        rollingMedianMs: med / 1000,
        excessMs
      });
    }
  }

  const excessSpikeMs = spikes.reduce((acc, s) => acc + s.excessMs, 0);
  const excessRate1k = (excessSpikeMs / fts.length) * 1000;
  const worstSpikeMs = spikes.length > 0 ? Math.max(...spikes.map(s => s.frameTimeMs)) : (sorted[sorted.length - 1] / 1000);

  return {
    totalFrames: fts.length,
    durationSeconds: parseFloat(durationS.toFixed(2)),
    averageFps: parseFloat(avgFps.toFixed(2)),
    percentilesMs: {
      p50: parseFloat(percentile(sorted, 0.50).toFixed(2)),
      p90: parseFloat(percentile(sorted, 0.90).toFixed(2)),
      p95: parseFloat(percentile(sorted, 0.95).toFixed(2)),
      p99: parseFloat(percentile(sorted, 0.99).toFixed(2)),
      p999: parseFloat(percentile(sorted, 0.999).toFixed(2)),
      max: parseFloat((sorted[sorted.length - 1] / 1000).toFixed(2))
    },
    stalls: {
      over33ms: over33,
      over50ms: over50,
      over100ms: over100
    },
    spikes: {
      count: spikes.length,
      ratePer1kFrames: parseFloat(((spikes.length / fts.length) * 1000).toFixed(2)),
      totalExcessTimeMs: parseFloat(excessSpikeMs.toFixed(2)),
      excessRatePer1kFrames: parseFloat(excessRate1k.toFixed(2)),
      worstSpikeMs: parseFloat(worstSpikeMs.toFixed(2))
    }
  };
}

function parseLogLifecycle(logPath) {
  if (!logPath || !fs.existsSync(logPath)) return null;

  const content = fs.readFileSync(logPath, 'utf8');
  const lines = content.split(/\r?\n/);

  let swapStartMs = null;
  let firstLogMs = null;
  let gameplayStartMs = null;
  let lastMarkerMs = null;
  const saveEventMs = [];

  for (const line of lines) {
    const ts = parseTimestampMs(line);
    if (!ts) continue;

    if (!firstLogMs) firstLogMs = ts;

    if (line.includes('[Majin performance] Release counter mode:')) {
      swapStartMs = ts;
    }

    // Detect gameplay entry markers across PAL and USA editions
    if (
      line.includes('flow_a returned') ||
      line.includes('flow_a result') ||
      line.includes('8236FBF0 returned') ||
      line.includes('[Majin Cutscene] fast-forward off')
    ) {
      if (!gameplayStartMs || ts > gameplayStartMs) {
        gameplayStartMs = ts;
      }
    }

    if (line.includes('ContentCreateEx') && line.includes('enter')) {
      saveEventMs.push(ts);
    }

    lastMarkerMs = ts;
  }

  return {
    anchorStartMs: swapStartMs || firstLogMs,
    gameplayStartMs,
    sessionEndMs: lastMarkerMs,
    saveEventMs
  };
}

function processSingleFile(csvPath, matchingLogPath) {
  if (!fs.existsSync(csvPath)) {
    throw new Error(`File not found: ${csvPath}`);
  }

  const raw = fs.readFileSync(csvPath, 'utf8').trim();
  const lines = raw.split(/\r?\n/);
  if (lines.length < 2) {
    throw new Error(`Insufficient CSV data in ${csvPath}`);
  }

  const header = lines[0].split(',').map(s => s.trim());
  let ftIdx = header.indexOf('frame_time_us');
  let ftIsMs = false;
  if (ftIdx === -1) {
    ftIdx = header.indexOf('frame_time_ms');
    if (ftIdx !== -1) ftIsMs = true;
  }
  if (ftIdx === -1) {
    throw new Error(`Neither 'frame_time_us' nor 'frame_time_ms' found in ${csvPath}`);
  }

  const frameIdxCol = header.indexOf('frame_index');

  // Parse log if available
  let autoLog = matchingLogPath;
  if (!autoLog) {
    const candidateSameDir = csvPath.replace(/\.performance\.csv$/, '.log').replace(/\.csv$/, '.log');
    if (fs.existsSync(candidateSameDir)) autoLog = candidateSameDir;
  }
  const lifecycle = parseLogLifecycle(autoLog);

  const allFrames = [];
  let cumUs = 0;

  for (let i = 1; i < lines.length; i++) {
    const parts = lines[i].split(',');
    const frameNum = frameIdxCol !== -1 ? parseInt(parts[frameIdxCol], 10) : i;
    const rawVal = parseFloat(parts[ftIdx]);
    if (isNaN(rawVal) || rawVal <= 0) continue;

    const us = ftIsMs ? Math.round(rawVal * 1000) : Math.round(rawVal);
    const frameTimestampMs = lifecycle && lifecycle.anchorStartMs
      ? lifecycle.anchorStartMs + (cumUs / 1000)
      : null;

    cumUs += us;

    const isGameplay = (!lifecycle || !lifecycle.gameplayStartMs || (frameTimestampMs !== null && frameTimestampMs >= lifecycle.gameplayStartMs));
    const isSaveIO = lifecycle && lifecycle.saveEventMs.some(sMs => Math.abs(frameTimestampMs - sMs) <= 600);

    allFrames.push({
      frameNum,
      us,
      timestampMs: frameTimestampMs,
      isGameplay,
      isSaveIO
    });
  }

  // Filter frames according to flags
  const filteredFrames = allFrames.filter(f => {
    if (startFrameArg !== null && f.frameNum < startFrameArg) return false;
    if (endFrameArg !== null && f.frameNum > endFrameArg) return false;
    if (gameplayOnly && !f.isGameplay) return false;
    if (excludeSaveIO && f.isSaveIO) return false;
    return true;
  });

  const fullMetrics = computeMetrics(allFrames);
  const activeMetrics = computeMetrics(filteredFrames);

  const firstGameplayFrame = allFrames.find(f => f.isGameplay);
  const gameplayStartFrameIndex = firstGameplayFrame ? firstGameplayFrame.frameNum : null;

  return {
    file: csvPath,
    logFile: autoLog || null,
    lifecycleDetected: !!lifecycle,
    gameplayStartFrame: gameplayStartFrameIndex,
    evaluatedWindow: {
      startFrame: filteredFrames.length > 0 ? filteredFrames[0].frameNum : null,
      endFrame: filteredFrames.length > 0 ? filteredFrames[filteredFrames.length - 1].frameNum : null,
      totalEvaluatedFrames: filteredFrames.length
    },
    metrics: activeMetrics,
    fullSessionMetrics: gameplayOnly ? fullMetrics : undefined
  };
}

function evaluateGate(metrics) {
  if (!metrics) return { passed: false, checks: [] };

  const p999 = metrics.percentilesMs.p999;
  const stalls100 = metrics.stalls.over100ms;
  const stalls33 = metrics.stalls.over33ms;
  const excessRate = metrics.spikes.excessRatePer1kFrames;

  const checks = [
    {
      metric: 'P99.9 Frame Time',
      target: `< ${threshP999.toFixed(2)} ms`,
      actual: `${p999.toFixed(2)} ms`,
      passed: p999 <= threshP999
    },
    {
      metric: 'Severe Stalls (> 100 ms)',
      target: `<= ${threshStalls100}`,
      actual: `${stalls100}`,
      passed: stalls100 <= threshStalls100
    },
    {
      metric: 'Stalled Frames (> 33.33 ms)',
      target: `<= ${threshStalls33}`,
      actual: `${stalls33}`,
      passed: stalls33 <= threshStalls33
    },
    {
      metric: 'Excess Spike Time / 1k Frames',
      target: `< ${threshExcessRate.toFixed(1)} ms`,
      actual: `${excessRate.toFixed(1)} ms`,
      passed: excessRate <= threshExcessRate
    }
  ];

  const allPassed = checks.every(c => c.passed);
  return { passed: allPassed, checks };
}

function computeAggregate(results) {
  if (results.length <= 1) return null;

  const n = results.length;
  const metricsList = results.map(r => r.metrics);

  function stat(getter) {
    const vals = metricsList.map(getter);
    const mean = vals.reduce((a, b) => a + b, 0) / n;
    const min = Math.min(...vals);
    const max = Math.max(...vals);
    const variance = vals.reduce((sum, v) => sum + Math.pow(v - mean, 2), 0) / n;
    const stdDev = Math.sqrt(variance);
    return {
      mean: parseFloat(mean.toFixed(2)),
      min: parseFloat(min.toFixed(2)),
      max: parseFloat(max.toFixed(2)),
      stdDev: parseFloat(stdDev.toFixed(2))
    };
  }

  return {
    sampleCount: n,
    averageFps: stat(m => m.averageFps),
    p50Ms: stat(m => m.percentilesMs.p50),
    p95Ms: stat(m => m.percentilesMs.p95),
    p99Ms: stat(m => m.percentilesMs.p99),
    p999Ms: stat(m => m.percentilesMs.p999),
    maxMs: stat(m => m.percentilesMs.max),
    stallsOver33ms: stat(m => m.stalls.over33ms),
    stallsOver50ms: stat(m => m.stalls.over50ms),
    stallsOver100ms: stat(m => m.stalls.over100ms),
    spikeCount: stat(m => m.spikes.count),
    excessRatePer1kFrames: stat(m => m.spikes.excessRatePer1kFrames)
  };
}

try {
  const individualResults = [];
  for (let i = 0; i < inputPaths.length; i++) {
    const csv = inputPaths[i];
    const log = logPaths[i] || null;
    const res = processSingleFile(csv, log);
    individualResults.push(res);
  }

  const aggregate = computeAggregate(individualResults);

  // If single run, evaluate gate on that run; if aggregate, evaluate on mean values
  let gateEvaluation = null;
  if (verifyGate) {
    if (aggregate) {
      const pseudoMetrics = {
        percentilesMs: { p999: aggregate.p999Ms.mean },
        stalls: { over100ms: Math.round(aggregate.stallsOver100ms.mean), over33ms: Math.round(aggregate.stallsOver33ms.mean) },
        spikes: { excessRatePer1kFrames: aggregate.excessRatePer1kFrames.mean }
      };
      gateEvaluation = evaluateGate(pseudoMetrics);
    } else {
      gateEvaluation = evaluateGate(individualResults[0].metrics);
    }
  }

  const finalOutput = {
    runs: individualResults,
    aggregate,
    gate: gateEvaluation
  };

  if (compactJson) {
    console.log(JSON.stringify(finalOutput));
  } else {
    console.log(JSON.stringify(finalOutput, null, 2));
  }

  if (verifyGate && gateEvaluation && !gateEvaluation.passed) {
    process.exit(1);
  }
} catch (err) {
  console.error(`Execution error: ${err.message}`);
  process.exit(1);
}
