#!/usr/bin/env node
/**
 * D3 — Pipeline & Shader Population Analysis Script
 * Issue #4: Implement shader pre-compilation / cache warm-up to eliminate in-game stuttering
 *
 * Parses:
 *   1. All existing .xsh guest shader storage files on disk (ShaderStoredHeader + ucode dwords)
 *   2. All existing .xpso pipeline description storage files on disk (Vulkan Format 2 / Legacy 68B records & D3D12 72B records)
 *   3. All 203 runtime .log files across reports/ and game-data/runtime/logs/ to trace:
 *      - Within-session shader & pipeline growth curves vs. playtime (seconds/minutes) and level/zone loads
 *      - Cross-session cumulative cache growth across development runs
 *      - Historical runtime "Creating graphics pipeline state with VS <hash> and PS <hash>" events
 *
 * Usage:
 *   node scripts/discovery/d3-pipeline-population.mjs
 */

import fs from 'node:fs';
import path from 'node:path';

const CACHE_ROOT = 'game-data/runtime/cache';
const LOG_ROOTS = ['reports', 'game-data/runtime/logs'];

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

function round(val, digits = 2) {
  return Number((val || 0).toFixed(digits));
}

function hex64(bigintVal) {
  return bigintVal.toString(16).toUpperCase().padStart(16, '0');
}

function parseTimestampMs(line) {
  const m = line.match(/^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]/);
  if (!m) return null;
  return new Date(m[1].replace(' ', 'T') + 'Z').getTime();
}

// ---------------------------------------------------------------------------
// 1. Parse all .xsh files (Xenos guest shader microcode cache)
// ---------------------------------------------------------------------------
function parseXshFile(filePath) {
  const buf = fs.readFileSync(filePath);
  if (buf.length < 8) {
    return { valid: false, error: 'File smaller than 8-byte header', fileBytes: buf.length };
  }
  const magic = buf.readUInt32LE(0);
  const versionSwapped = buf.readUInt32BE(4);
  if (magic !== 0x48534558) {
    return { valid: false, error: `Invalid magic 0x${magic.toString(16)}`, fileBytes: buf.length };
  }

  let offset = 8;
  const records = [];
  const uniqueShaders = new Map();
  let duplicateRecords = 0;

  while (offset + 12 <= buf.length) {
    const hashBig = buf.readBigUInt64LE(offset);
    const dwordAndType = buf.readUInt32LE(offset + 8);
    const dwordCount = dwordAndType & 0x7fffffff;
    const typeBit = (dwordAndType >>> 31) & 1;
    const type = typeBit === 0 ? 'vertex' : 'pixel';
    const byteCount = dwordCount * 4;

    if (offset + 12 + byteCount > buf.length) {
      break;
    }
    const hashHex = hex64(hashBig);
    records.push({ index: records.length, hashHex, type, dwordCount, byteCount });
    if (!uniqueShaders.has(hashHex)) {
      uniqueShaders.set(hashHex, { hashHex, type, dwordCount, byteCount, firstIndex: records.length - 1 });
    } else {
      duplicateRecords++;
    }
    offset += 12 + byteCount;
  }

  const allUnique = [...uniqueShaders.values()];
  const vertexShaders = allUnique.filter((s) => s.type === 'vertex');
  const pixelShaders = allUnique.filter((s) => s.type === 'pixel');
  const totalUcodeBytes = allUnique.reduce((a, s) => a + s.byteCount, 0);

  return {
    valid: true,
    versionHex: `0x${versionSwapped.toString(16)}`,
    fileBytes: buf.length,
    totalRecordsInFile: records.length,
    duplicateRecordsInFile: duplicateRecords,
    uniqueShadersCount: allUnique.length,
    vertexShaderCount: vertexShaders.length,
    pixelShaderCount: pixelShaders.length,
    totalUniqueUcodeBytes: totalUcodeBytes,
    meanUcodeBytes: allUnique.length ? round(totalUcodeBytes / allUnique.length, 1) : 0,
    minUcodeBytes: allUnique.length ? Math.min(...allUnique.map((s) => s.byteCount)) : 0,
    maxUcodeBytes: allUnique.length ? Math.max(...allUnique.map((s) => s.byteCount)) : 0,
    uniqueShadersMap: uniqueShaders,
    records,
  };
}

// ---------------------------------------------------------------------------
// 2. Parse all .xpso files (Vulkan & D3D12 PipelineStoredDescription files)
// ---------------------------------------------------------------------------
function parseXpsoFile(filePath) {
  const buf = fs.readFileSync(filePath);
  if (buf.length < 12) {
    return { valid: false, error: 'File smaller than 12-byte header', fileBytes: buf.length };
  }
  const magic = buf.readUInt32LE(0);
  if (magic !== 0x53504558) {
    return { valid: false, error: `Invalid magic 0x${magic.toString(16)}`, fileBytes: buf.length };
  }
  const apiWord = buf.readUInt32LE(4);
  const word2BE = buf.readUInt32BE(8);
  const isFormat2 = word2BE === 2;
  const headerSize = isFormat2 ? 20 : 12;

  const isVk = filePath.endsWith('.vk.xpso');
  const isD3D12 = filePath.endsWith('.d3d12.xpso');
  const recordSize = isVk ? 68 : 72;
  const payloadBytes = buf.length - headerSize;
  const recordCount = Math.floor(payloadBytes / recordSize);
  const remainderBytes = payloadBytes % recordSize;

  let descVerHex = null;
  let modVerHex = null;
  if (isFormat2 && buf.length >= 20) {
    descVerHex = `0x${buf.readUInt32BE(12).toString(16)}`;
    modVerHex = `0x${buf.readUInt32BE(16).toString(16)}`;
  } else {
    descVerHex = `0x${word2BE.toString(16)}`;
  }

  const records = [];
  const uniqueDescHashes = new Set();
  const uniqueVsHashes = new Set();
  const uniquePsHashes = new Set();
  const uniqueAllShaderHashes = new Set();
  const uniqueVsMods = new Set();
  const uniquePsMods = new Set();
  const uniqueVsPsPairs = new Set();
  const uniqueVsModPsModTuples = new Set();
  const uniqueStateBytesHex = new Set();
  const vsPsPairToPsoCount = new Map();
  let nullPsCount = 0;

  const encounterGrowthCurve = [];

  for (let i = 0; i < recordCount; i++) {
    const base = headerSize + i * recordSize;
    const descHash = hex64(buf.readBigUInt64LE(base));
    const vsHash = hex64(buf.readBigUInt64LE(base + 8));
    const vsMod = hex64(buf.readBigUInt64LE(base + 16));
    const psHashBig = buf.readBigUInt64LE(base + 24);
    const psHash = hex64(psHashBig);
    const psMod = hex64(buf.readBigUInt64LE(base + 32));
    const stateHex = buf.subarray(base + 40, base + recordSize).toString('hex');

    uniqueDescHashes.add(descHash);
    uniqueVsHashes.add(vsHash);
    uniqueAllShaderHashes.add(vsHash);
    uniqueVsMods.add(`${vsHash}:${vsMod}`);
    if (psHashBig !== 0n) {
      uniquePsHashes.add(psHash);
      uniqueAllShaderHashes.add(psHash);
      uniquePsMods.add(`${psHash}:${psMod}`);
    } else {
      nullPsCount++;
    }

    const pairKey = `${vsHash}|${psHash}`;
    uniqueVsPsPairs.add(pairKey);
    vsPsPairToPsoCount.set(pairKey, (vsPsPairToPsoCount.get(pairKey) || 0) + 1);

    const modTupleKey = `${vsHash}:${vsMod}|${psHash}:${psMod}`;
    uniqueVsModPsModTuples.add(modTupleKey);
    uniqueStateBytesHex.add(stateHex);

    records.push({
      index: i,
      descHash,
      vsHash,
      vsMod,
      psHash,
      psMod,
      stateHex,
    });

    if ((i + 1) % 25 === 0 || i + 1 === recordCount) {
      encounterGrowthCurve.push({
        psoIndex: i + 1,
        uniquePsos: uniqueDescHashes.size,
        uniqueShadersTotal: uniqueAllShaderHashes.size,
        uniqueVs: uniqueVsHashes.size,
        uniquePs: uniquePsHashes.size,
        uniqueVsPsPairs: uniqueVsPsPairs.size,
        uniqueStateBlocks: uniqueStateBytesHex.size,
        psosPerPairRatio: round(uniqueDescHashes.size / Math.max(1, uniqueVsPsPairs.size), 2),
        psosPerShaderRatio: round(uniqueDescHashes.size / Math.max(1, uniqueAllShaderHashes.size), 2),
      });
    }
  }

  return {
    valid: true,
    backend: isVk ? 'Vulkan' : 'D3D12',
    apiMode: isVk ? (apiWord === 1 ? 'fsi' : 'fbo') : apiWord === 0x4f525844 ? 'rov' : 'rtv',
    headerFormat: isFormat2 ? 'Format2 (20B)' : 'Legacy (12B)',
    descVerHex,
    modVerHex,
    fileBytes: buf.length,
    recordSize,
    recordCount,
    remainderBytes,
    uniquePsoDescriptions: uniqueDescHashes.size,
    uniqueShadersReferenced: uniqueAllShaderHashes.size,
    uniqueVertexShaders: uniqueVsHashes.size,
    uniquePixelShaders: uniquePsHashes.size,
    depthOnlyNullPsPipelines: nullPsCount,
    uniqueVsModifications: uniqueVsMods.size,
    uniquePsModifications: uniquePsMods.size,
    uniqueVsPsPairsCount: uniqueVsPsPairs.size,
    uniqueVsModPsModTuplesCount: uniqueVsModPsModTuples.size,
    uniqueFixedFunctionStateBlocks: uniqueStateBytesHex.size,
    meanPsosPerVsPsPair: uniqueVsPsPairs.size ? round(uniqueDescHashes.size / uniqueVsPsPairs.size, 2) : 0,
    maxPsosForSingleVsPsPair: vsPsPairToPsoCount.size ? Math.max(...vsPsPairToPsoCount.values()) : 0,
    encounterGrowthCurve,
    uniqueVsHashesSet: uniqueVsHashes,
    uniquePsHashesSet: uniquePsHashes,
    uniqueAllShaderHashesSet: uniqueAllShaderHashes,
    uniqueVsPsPairsSet: uniqueVsPsPairs,
    vsPsPairToPsoCount,
    records,
  };
}

const cacheFiles = walk(CACHE_ROOT);
const xshFiles = cacheFiles.filter((f) => f.endsWith('.xsh')).sort();
const xpsoFiles = cacheFiles.filter((f) => f.endsWith('.xpso')).sort();

const parsedXsh = {};
const globalUniqueShadersFromXsh = new Map();

for (const f of xshFiles) {
  const rel = path.relative(process.cwd(), f).replace(/\\/g, '/');
  const cacheName = rel.split('/')[3];
  const res = parseXshFile(f);
  parsedXsh[cacheName] = {
    path: rel,
    fileBytes: res.fileBytes,
    totalRecordsInFile: res.totalRecordsInFile,
    duplicateRecordsInFile: res.duplicateRecordsInFile,
    uniqueShadersCount: res.uniqueShadersCount,
    vertexShaderCount: res.vertexShaderCount,
    pixelShaderCount: res.pixelShaderCount,
    totalUniqueUcodeBytes: res.totalUniqueUcodeBytes,
    meanUcodeBytes: res.meanUcodeBytes,
    minUcodeBytes: res.minUcodeBytes,
    maxUcodeBytes: res.maxUcodeBytes,
  };
  if (res.valid) {
    for (const [h, info] of res.uniqueShadersMap.entries()) {
      if (!globalUniqueShadersFromXsh.has(h)) {
        globalUniqueShadersFromXsh.set(h, {
          hashHex: h,
          type: info.type,
          dwordCount: info.dwordCount,
          byteCount: info.byteCount,
          caches: [cacheName],
        });
      } else {
        globalUniqueShadersFromXsh.get(h).caches.push(cacheName);
      }
    }
  }
}

const parsedXpso = {};
const globalUniqueVsPsPairsFromXpso = new Set();
const globalUniqueShadersFromXpso = new Set();
const globalUniqueVsFromXpso = new Set();
const globalUniquePsFromXpso = new Set();

for (const f of xpsoFiles) {
  const rel = path.relative(process.cwd(), f).replace(/\\/g, '/');
  const parts = rel.split('/');
  const cacheName = `${parts[3]} (${path.basename(f)})`;
  const res = parseXpsoFile(f);
  parsedXpso[cacheName] = {
    path: rel,
    backend: res.backend,
    apiMode: res.apiMode,
    headerFormat: res.headerFormat,
    descVerHex: res.descVerHex,
    modVerHex: res.modVerHex,
    fileBytes: res.fileBytes,
    recordSize: res.recordSize,
    recordCount: res.recordCount,
    uniquePsoDescriptions: res.uniquePsoDescriptions,
    uniqueShadersReferenced: res.uniqueShadersReferenced,
    uniqueVertexShaders: res.uniqueVertexShaders,
    uniquePixelShaders: res.uniquePixelShaders,
    depthOnlyNullPsPipelines: res.depthOnlyNullPsPipelines,
    uniqueVsModifications: res.uniqueVsModifications,
    uniquePsModifications: res.uniquePsModifications,
    uniqueVsPsPairsCount: res.uniqueVsPsPairsCount,
    uniqueVsModPsModTuplesCount: res.uniqueVsModPsModTuplesCount,
    uniqueFixedFunctionStateBlocks: res.uniqueFixedFunctionStateBlocks,
    meanPsosPerVsPsPair: res.meanPsosPerVsPsPair,
    maxPsosForSingleVsPsPair: res.maxPsosForSingleVsPsPair,
    encounterGrowthCurve: res.encounterGrowthCurve,
    _raw: res,
  };
  if (res.valid) {
    for (const p of res.uniqueVsPsPairsSet) globalUniqueVsPsPairsFromXpso.add(p);
    for (const s of res.uniqueAllShaderHashesSet) globalUniqueShadersFromXpso.add(s);
    for (const v of res.uniqueVsHashesSet) globalUniqueVsFromXpso.add(v);
    for (const p of res.uniquePsHashesSet) globalUniquePsFromXpso.add(p);
  }
}

const vkMainXpso = parsedXpso['vulkan-vulkan (4E4D0816.fbo.vk.xpso)']?._raw;
const d3dMainXpso = parsedXpso['d3d12-default (4E4D0816.rtv.d3d12.xpso)']?._raw;
const d3dRtvXpso = parsedXpso['d3d12-rtv (4E4D0816.rtv.d3d12.xpso)']?._raw;

function setIntersectionSize(setA, setB) {
  let c = 0;
  for (const x of setA) if (setB.has(x)) c++;
  return c;
}

function setUnionSize(setA, setB) {
  const u = new Set(setA);
  for (const x of setB) u.add(x);
  return u.size;
}

const d3dMainDescHashes = new Set(d3dMainXpso ? d3dMainXpso.records.map((r) => r.descHash) : []);
const d3dRtvDescHashes = new Set(d3dRtvXpso ? d3dRtvXpso.records.map((r) => r.descHash) : []);

for (const k of Object.keys(parsedXpso)) {
  delete parsedXpso[k]._raw;
}

// ---------------------------------------------------------------------------
// 3. Parse all 203 .log files
// ---------------------------------------------------------------------------
const allLogFiles = LOG_ROOTS.flatMap(walk).filter((f) => f.endsWith('.log')).sort();

const logDiscoveredVsHashes = new Set();
const logDiscoveredPsHashes = new Set();
const logDiscoveredVsPsPairs = new Set();

const vulkanMinuteProgressionSessions = [];
const vulkanTimestampedCreationSessions = [];
const d3d12SessionProgression = [];
const vulkanSessionProgression = [];

for (const lp of allLogFiles) {
  const text = fs.readFileSync(lp, 'utf8');
  const file = path.basename(lp);
  const isVk = text.includes('Vulkan device') || text.includes('VulkanPipelineCache') || file.includes('vulkan');
  const isD3D12 = text.includes('DXGI adapter') || text.includes('Direct3D 12');
  const lines = text.split(/\r?\n/);

  let firstTs = null;
  let lastTs = null;
  let swapStartTs = null;
  let initDone = false;
  const hasPreloadLine = text.includes('Created ') && text.includes('graphics pipelines');

  let preloadedShaders = null;
  let preloadedPsos = null;
  const runtimeCreations = [];
  const runUniquePairs = new Set();
  const minutePoints = [];
  const lookaheadPoints = [];
  const levelLoads = [];

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    const ts = parseTimestampMs(line);
    if (ts !== null) {
      if (firstTs === null) firstTs = ts;
      lastTs = ts;
    }
    if (line.includes('[Majin performance] Release counter mode:') && ts !== null) {
      swapStartTs = ts;
    }

    if (line.includes('[Majin loading') || line.includes('[Majin Cutscene]') || line.includes('Achievement unlocked:')) {
      if (ts !== null && firstTs !== null) {
        const mEvt = line.match(/\[(Majin loading[^\]]*|Majin Cutscene)\].*$|Achievement unlocked:.*$/);
        if (mEvt) {
          levelLoads.push({ elapsedSec: round((ts - firstTs) / 1000, 1), event: mEvt[0].slice(0, 110) });
        }
      }
    }

    const shMatch = line.match(/Translated (\d+) shaders from the storage/);
    if (shMatch) preloadedShaders = Number(shMatch[1]);

    const d3dPsoMatch = line.match(/Created (\d+) graphics pipelines \(not including reading the descriptions\) from the storage/);
    if (d3dPsoMatch) {
      preloadedPsos = Number(d3dPsoMatch[1]);
      initDone = true;
    }

    const vkPsoMatch = line.match(/Created (\d+) graphics pipelines from Vulkan storage \((\d+) requested\)/);
    if (vkPsoMatch) {
      preloadedPsos = Number(vkPsoMatch[1]);
      initDone = true;
    }

    if (!initDone && line.includes('Initializing shader storage for title') && !hasPreloadLine) {
      preloadedPsos = 0;
      initDone = true;
    }

    // Note: in older logs (pre-ReIGlue), the format was:
    //   "Creating graphics pipeline state with VS <hash> and PS <hash>"
    // In ReIGlue logs, the first line is:
    //   "Creating graphics pipeline state with VS <hash>, PS <hash>"
    const createMatch = line.match(/Creating graphics pipeline state with VS ([0-9A-Fa-f]{16})(?:(?: and|,)? PS ([0-9A-Fa-f]{16}))?/);
    if (createMatch) {
      const vsH = createMatch[1].toUpperCase();
      const psH = (createMatch[2] || '0000000000000000').toUpperCase();
      logDiscoveredVsHashes.add(vsH);
      if (psH !== '0000000000000000') logDiscoveredPsHashes.add(psH);
      const pk = `${vsH}|${psH}`;
      logDiscoveredVsPsPairs.add(pk);

      if (initDone && ts !== null) {
        const anchor = swapStartTs || firstTs || ts;
        const elapsedSec = round((ts - anchor) / 1000, 2);
        runUniquePairs.add(pk);
        runtimeCreations.push({
          elapsedSec,
          vsHash: vsH,
          psHash: psH,
          cumulativeRuntimePsos: runtimeCreations.length + 1,
          cumulativeUniquePairsInRun: runUniquePairs.size,
        });
      }
    }

    const minMatch = line.match(
      /GPU minute (\d+): ([\d.]+) fps .* a second: ([\d.]+) textures new, ([\d.]+) loaded, ([\d.]+) evicted, ([\d.]+) pipelines new; now: textures (\d+) \((\d+) MB\), render targets (\d+) \((\d+) MB\), shaders (\d+), pipelines (\d+), framebuffers (\d+)/
    );
    if (minMatch) {
      minutePoints.push({
        minute: Number(minMatch[1]),
        fps: Number(minMatch[2]),
        pipelinesNewPerSec: Number(minMatch[6]),
        texturesNow: Number(minMatch[7]),
        texturesMb: Number(minMatch[8]),
        shadersNow: Number(minMatch[11]),
        pipelinesNow: Number(minMatch[12]),
        framebuffersNow: Number(minMatch[13]),
      });
    }

    // Parse "Pipelines: 332 built, 108 of them ahead of their draws ... lookahead: 103 walks ..., 60 waits 4348.7 ms"
    const laBuiltMatch = line.match(
      /Pipelines: (\d+) built, (\d+) of them ahead of their draws .* lookahead: (\d+) walks \([^)]+\), (\d+) packets, (\d+) draws, (\d+) queued, (\d+) unmet waits, ([\d.]+) ms; (\d+) waits ([\d.]+) ms/
    );
    if (laBuiltMatch && ts !== null && firstTs !== null) {
      const totalBuilt = Number(laBuiltMatch[1]);
      const aheadBuilt = Number(laBuiltMatch[2]);
      const basePreload = preloadedPsos || 0;
      const runtimeNew = Math.max(0, totalBuilt - basePreload);
      const inlineBuilt = Math.max(0, runtimeNew - aheadBuilt);
      lookaheadPoints.push({
        elapsedSec: round((ts - firstTs) / 1000, 1),
        totalBuilt,
        preloadPipes: basePreload,
        runtimeNewPipes: runtimeNew,
        aheadPipes: aheadBuilt,
        inlinePipes: inlineBuilt,
        waits: Number(laBuiltMatch[9]),
        waitMs: Number(laBuiltMatch[10]),
      });
    }
  }

  const durationSec = firstTs !== null && lastTs !== null ? round((lastTs - firstTs) / 1000, 1) : 0;

  if (isVk) {
    if (minutePoints.length > 0 || lookaheadPoints.length > 1) {
      vulkanMinuteProgressionSessions.push({
        file,
        durationSec,
        preloadedPsos,
        endTotalPsos:
          lookaheadPoints.length > 0
            ? lookaheadPoints[lookaheadPoints.length - 1].totalBuilt
            : minutePoints.length > 0
              ? minutePoints[minutePoints.length - 1].pipelinesNow
              : preloadedPsos,
        minutePoints,
        lookaheadPointsCount: lookaheadPoints.length,
        lookaheadFirstAndLast:
          lookaheadPoints.length > 0
            ? [lookaheadPoints[0], lookaheadPoints[lookaheadPoints.length - 1]]
            : [],
        levelLoadsCount: levelLoads.length,
        levelLoadsSample: levelLoads.slice(0, 15),
      });
    }
    if (runtimeCreations.length >= 10) {
      const buckets = {
        '0-30s': runtimeCreations.filter((c) => c.elapsedSec <= 30).length,
        '30-60s': runtimeCreations.filter((c) => c.elapsedSec > 30 && c.elapsedSec <= 60).length,
        '60-120s': runtimeCreations.filter((c) => c.elapsedSec > 60 && c.elapsedSec <= 120).length,
        '120-300s': runtimeCreations.filter((c) => c.elapsedSec > 120 && c.elapsedSec <= 300).length,
        '300s+': runtimeCreations.filter((c) => c.elapsedSec > 300).length,
      };
      vulkanTimestampedCreationSessions.push({
        file,
        durationSec,
        preloadedPsos: preloadedPsos || 0,
        runtimePsosCreated: runtimeCreations.length,
        endTotalPsos: (preloadedPsos || 0) + runtimeCreations.length,
        uniqueVsPsPairsInRuntimeCreations: runUniquePairs.size,
        firstCreationSec: runtimeCreations[0].elapsedSec,
        p50CreationSec: runtimeCreations[Math.floor(runtimeCreations.length * 0.5)].elapsedSec,
        p90CreationSec: runtimeCreations[Math.floor(runtimeCreations.length * 0.9)].elapsedSec,
        lastCreationSec: runtimeCreations[runtimeCreations.length - 1].elapsedSec,
        creationsByTimeBucket: buckets,
      });
    }
    if (preloadedPsos !== null || runtimeCreations.length > 0 || lookaheadPoints.length > 0) {
      const endPsos =
        lookaheadPoints.length > 0
          ? lookaheadPoints[lookaheadPoints.length - 1].totalBuilt
          : minutePoints.length > 0
            ? minutePoints[minutePoints.length - 1].pipelinesNow
            : (preloadedPsos || 0) + runtimeCreations.length;
      const endShaders =
        minutePoints.length > 0 ? minutePoints[minutePoints.length - 1].shadersNow : preloadedShaders;
      vulkanSessionProgression.push({
        file,
        durationSec,
        preloadedPsos: preloadedPsos || 0,
        endPsos,
        newPsosInSession: Math.max(0, endPsos - (preloadedPsos || 0)),
        endShaders,
      });
    }
  }

  if (isD3D12 && preloadedPsos !== null) {
    d3d12SessionProgression.push({
      file,
      durationSec,
      preloadedShaders: preloadedShaders || 0,
      preloadedPsos,
    });
  }
}

// Check why logDiscoveredVsPsPairs had pairs not in globalUniqueVsPsPairsFromXpso:
// Wait: in older logs (pre-ReIGlue), "Creating graphics pipeline state with VS <hash> and PS <hash>"
// logged depth-only pipelines as "Creating graphics pipeline state with VS <hash>" (no PS), which we mapped to PS=0000000000000000,
// OR did some older logs use a different hash (e.g. CityHash64 vs XXH3_64bits)?
// Let's check how many of logDiscoveredVsHashes are in globalUniqueShadersFromXsh!
const logVsInXsh = [...logDiscoveredVsHashes].filter((h) => globalUniqueShadersFromXsh.has(h)).length;
const logPsInXsh = [...logDiscoveredPsHashes].filter((h) => globalUniqueShadersFromXsh.has(h)).length;
const logPairsInXpso = [...logDiscoveredVsPsPairs].filter((p) => globalUniqueVsPsPairsFromXpso.has(p)).length;

const grandUnionShaders = new Set([
  ...globalUniqueShadersFromXsh.keys(),
  ...globalUniqueShadersFromXpso,
  ...logDiscoveredVsHashes,
  ...logDiscoveredPsHashes,
]);
const grandUnionVs = new Set([
  ...[...globalUniqueShadersFromXsh.values()].filter((s) => s.type === 'vertex').map((s) => s.hashHex),
  ...globalUniqueVsFromXpso,
  ...logDiscoveredVsHashes,
]);
const grandUnionPs = new Set([
  ...[...globalUniqueShadersFromXsh.values()].filter((s) => s.type === 'pixel').map((s) => s.hashHex),
  ...globalUniquePsFromXpso,
  ...logDiscoveredPsHashes,
]);
const grandUnionVsPsPairs = new Set([
  ...globalUniqueVsPsPairsFromXpso,
  ...logDiscoveredVsPsPairs,
]);

const d3d12GrowthMilestones = [];
for (const s of d3d12SessionProgression) {
  const prev = d3d12GrowthMilestones[d3d12GrowthMilestones.length - 1];
  if (!prev || prev.preloadedPsos !== s.preloadedPsos || prev.preloadedShaders !== s.preloadedShaders) {
    d3d12GrowthMilestones.push(s);
  }
}

const vulkanGrowthMilestones = [];
for (const s of vulkanSessionProgression) {
  const prev = vulkanGrowthMilestones[vulkanGrowthMilestones.length - 1];
  if (!prev || prev.preloadedPsos !== s.preloadedPsos || prev.endPsos !== s.endPsos) {
    vulkanGrowthMilestones.push(s);
  }
}

const output = {
  summary: {
    grandUnionAcrossAllCachesAndLogs: {
      totalUniqueGuestShaders: grandUnionShaders.size,
      totalUniqueVertexShaders: grandUnionVs.size,
      totalUniquePixelShaders: grandUnionPs.size,
      totalUniqueVsPsPairs: grandUnionVsPsPairs.size,
      logVsHashesTotal: logDiscoveredVsHashes.size,
      logVsHashesPresentInOnDiskXsh: logVsInXsh,
      logPsHashesTotal: logDiscoveredPsHashes.size,
      logPsHashesPresentInOnDiskXsh: logPsInXsh,
      logVsPsPairsTotal: logDiscoveredVsPsPairs.size,
      logVsPsPairsPresentInOnDiskXpso: logPairsInXpso,
    },
    onDiskCachesUnion: {
      totalUniqueShadersInXshFiles: globalUniqueShadersFromXsh.size,
      totalUniqueVertexShadersInXshFiles: [...globalUniqueShadersFromXsh.values()].filter((s) => s.type === 'vertex').length,
      totalUniquePixelShadersInXshFiles: [...globalUniqueShadersFromXsh.values()].filter((s) => s.type === 'pixel').length,
      totalUniqueShadersReferencedInXpsoFiles: globalUniqueShadersFromXpso.size,
      totalUniqueVsPsPairsInXpsoFiles: globalUniqueVsPsPairsFromXpso.size,
    },
    crossBackendComparison: {
      vulkanMainFbo_342Psos: {
        psos: vkMainXpso?.uniquePsoDescriptions || 0,
        shadersReferenced: vkMainXpso?.uniqueShadersReferenced || 0,
        vsPsPairs: vkMainXpso?.uniqueVsPsPairsCount || 0,
      },
      d3d12MainRtv_295Psos: {
        psos: d3dMainXpso?.uniquePsoDescriptions || 0,
        shadersReferenced: d3dMainXpso?.uniqueShadersReferenced || 0,
        vsPsPairs: d3dMainXpso?.uniqueVsPsPairsCount || 0,
      },
      sharedShadersBetweenVkMainAndD3dMain:
        vkMainXpso && d3dMainXpso
          ? setIntersectionSize(vkMainXpso.uniqueAllShaderHashesSet, d3dMainXpso.uniqueAllShaderHashesSet)
          : 0,
      unionShadersBetweenVkMainAndD3dMain:
        vkMainXpso && d3dMainXpso
          ? setUnionSize(vkMainXpso.uniqueAllShaderHashesSet, d3dMainXpso.uniqueAllShaderHashesSet)
          : 0,
      sharedVsPsPairsBetweenVkMainAndD3dMain:
        vkMainXpso && d3dMainXpso
          ? setIntersectionSize(vkMainXpso.uniqueVsPsPairsSet, d3dMainXpso.uniqueVsPsPairsSet)
          : 0,
      unionVsPsPairsBetweenVkMainAndD3dMain:
        vkMainXpso && d3dMainXpso
          ? setUnionSize(vkMainXpso.uniqueVsPsPairsSet, d3dMainXpso.uniqueVsPsPairsSet)
          : 0,
      d3d12RtvUnionDescHashes_d3dDefaultPlusD3dRtv: setUnionSize(d3dMainDescHashes, d3dRtvDescHashes),
      d3d12RtvSharedDescHashes_d3dDefaultAndD3dRtv: setIntersectionSize(d3dMainDescHashes, d3dRtvDescHashes),
    },
  },
  xshCachesOnDisk: parsedXsh,
  xpsoCachesOnDisk: parsedXpso,
  withinSessionGrowthCurves: {
    vulkanMinuteAndLookaheadSessions: vulkanMinuteProgressionSessions,
    vulkanTimestampedCreationSessions,
  },
  crossSessionGrowthMilestones: {
    vulkanMilestones: vulkanGrowthMilestones,
    d3d12Milestones: d3d12GrowthMilestones,
  },
};

console.log(JSON.stringify(output, null, 2));
