#!/usr/bin/env node
/**
 * scripts/analyze-performance-csv.mjs
 *
 * Standalone Performance CSV Analyzer for CI Quality Gates
 *
 * Computes deterministic frame-time percentiles, spike metrics, and stutter rates
 * from a .performance.csv or normalized benchmark CSV.
 *
 * Usage:
 *   node scripts/analyze-performance-csv.mjs --input=reports/ci-run.csv [--log=game-data/runtime/logs/runtime-xxx.log] [--start-frame=200]
 *   node scripts/analyze-performance-csv.mjs --input=reports/ci-run.csv --json
 */

import fs from 'fs';
import path from 'path';

function printUsage() {
  console.error(`Usage: node scripts/analyze-performance-csv.mjs --input=<csv_file> [options]
Options:
  --input=<path>        Path to .performance.csv or benchmark CSV (required)
  --log=<path>          Optional path to runtime log to extract lifecycle markers
  --start-frame=<N>     Explicit starting frame index (inclusive)
  --end-frame=<N>       Explicit ending frame index (inclusive)
  --json                Emit compact JSON to stdout (default: pretty JSON)
`);
}

const args = process.argv.slice(2);
let inputPath = null;
let logPath = null;
let startFrame = null;
let endFrame = null;
let compactJson = false;

for (const arg of args) {
  if (arg.startsWith('--input=')) {
    inputPath = arg.substring('--input='.length);
  } else if (arg.startsWith('--log=')) {
    logPath = arg.substring('--log='.length);
  } else if (arg.startsWith('--start-frame=')) {
    startFrame = parseInt(arg.substring('--start-frame='.length), 10);
  } else if (arg.startsWith('--end-frame=')) {
    endFrame = parseInt(arg.substring('--end-frame='.length), 10);
  } else if (arg === '--json') {
    compactJson = true;
  } else if (arg === '--help' || arg === '-h') {
    printUsage();
    process.exit(0);
  } else if (!inputPath && !arg.startsWith('--')) {
    inputPath = arg;
  }
}

if (!inputPath) {
  console.error('Error: --input=<csv_file> is required.');
  printUsage();
  process.exit(1);
}

if (!fs.existsSync(inputPath)) {
  console.error(`Error: Input file "${inputPath}" not found.`);
  process.exit(1);
}

// Helper: Parse lifecycle markers from log if provided
if (logPath && fs.existsSync(logPath)) {
  const logContent = fs.readFileSync(logPath, 'utf8');
  // Detect gameplay entry: flow_a returned r3=00000001 (or similar)
  const flowMatch = logContent.match(/flow_a returned/);
  // Detect checkpoint save: ContentCreateEx
  const saveMatch = logContent.match(/ContentCreateEx/);
}

function median(arr) {
  if (arr.length === 0) return 0;
  const s = [...arr].sort((a, b) => a - b);
  const mid = Math.floor(s.length / 2);
  return s.length % 2 !== 0 ? s[mid] : (s[mid - 1] + s[mid]) / 2;
}

function percentile(sorted, p) {
  if (sorted.length === 0) return 0;
  const idx = Math.min(sorted.length - 1, Math.max(0, Math.ceil(sorted.length * p) - 1));
  return sorted[idx] / 1000; // ms
}

const raw = fs.readFileSync(inputPath, 'utf8').trim();
const lines = raw.split(/\r?\n/);
if (lines.length < 2) {
  console.error('Error: CSV file contains insufficient data.');
  process.exit(1);
}

const header = lines[0].split(',').map(s => s.trim());
let ftIdx = header.indexOf('frame_time_us');
let ftIsMs = false;
if (ftIdx === -1) {
  ftIdx = header.indexOf('frame_time_ms');
  if (ftIdx !== -1) ftIsMs = true;
}
if (ftIdx === -1) {
  console.error(`Error: Neither 'frame_time_us' nor 'frame_time_ms' column found in header: ${lines[0]}`);
  process.exit(1);
}

const frameIdxCol = header.indexOf('frame_index');

const rawFts = [];
for (let i = 1; i < lines.length; i++) {
  const parts = lines[i].split(',');
  const frameNum = frameIdxCol !== -1 ? parseInt(parts[frameIdxCol], 10) : i;
  if (startFrame !== null && frameNum < startFrame) continue;
  if (endFrame !== null && frameNum > endFrame) continue;

  const rawVal = parseFloat(parts[ftIdx]);
  if (!isNaN(rawVal) && rawVal > 0) {
    const us = ftIsMs ? Math.round(rawVal * 1000) : Math.round(rawVal);
    rawFts.push({ frameNum, us });
  }
}

if (rawFts.length === 0) {
  console.error('Error: No valid frame records found in the specified window.');
  process.exit(1);
}

const fts = rawFts.map(r => r.us);
const sorted = [...fts].sort((a, b) => a - b);
const totalUs = fts.reduce((a, b) => a + b, 0);
const durationS = totalUs / 1000000;
const avgFps = fts.length / durationS;

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
      frameIndex: rawFts[i].frameNum,
      frameTimeMs: ft / 1000,
      rollingMedianMs: med / 1000,
      excessMs
    });
  }
}

const excessSpikeMs = spikes.reduce((acc, s) => acc + s.excessMs, 0);
const excessRate1k = (excessSpikeMs / fts.length) * 1000;
const worstSpikeMs = spikes.length > 0 ? Math.max(...spikes.map(s => s.frameTimeMs)) : (sorted[sorted.length - 1] / 1000);

const result = {
  input: inputPath,
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
    excessTimeMs: parseFloat(excessSpikeMs.toFixed(2)),
    excessRatePer1kFrames: parseFloat(excessRate1k.toFixed(2)),
    worstSpikeMs: parseFloat(worstSpikeMs.toFixed(2))
  }
};

if (compactJson) {
  console.log(JSON.stringify(result));
} else {
  console.log(JSON.stringify(result, null, 2));
}
