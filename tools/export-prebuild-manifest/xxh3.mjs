// Pure-JavaScript implementation of XXH3_64bits (seed = 0, default secret)
// Spec: xxHash v0.8.x (https://github.com/Cyan4973/xxHash)

const MASK64 = 0xFFFFFFFFFFFFFFFFn;
const MASK32 = 0xFFFFFFFFn;

const PRIME32_1 = 0x9E3779B1n;
const PRIME32_2 = 0x85EBCA77n;
const PRIME32_3 = 0xC2B2AE3Dn;

const PRIME64_1 = 0x9E3779B185EBCA87n;
const PRIME64_2 = 0xC2B2AE3D27D4EB4Fn;
const PRIME64_3 = 0x165667B19E3779F9n;
const PRIME64_4 = 0x85EBCA77C2B2AE63n;
const PRIME64_5 = 0x27D4EB2F165667C5n;

const PRIME_MX1 = 0x165667919E3779F9n;
const PRIME_MX2 = 0x9FB21C651E98DF25n;

const DEFAULT_SECRET = new Uint8Array([
  0xb8, 0xfe, 0x6c, 0x39, 0x23, 0xa4, 0x4b, 0xbe, 0x7c, 0x01, 0x81, 0x2c, 0xf7, 0x21, 0xad, 0x1c,
  0xde, 0xd4, 0x6d, 0xe9, 0x83, 0x90, 0x97, 0xdb, 0x72, 0x40, 0xa4, 0xa4, 0xb7, 0xb3, 0x67, 0x1f,
  0xcb, 0x79, 0xe6, 0x4e, 0xcc, 0xc0, 0xe5, 0x78, 0x82, 0x5a, 0xd0, 0x7d, 0xcc, 0xff, 0x72, 0x21,
  0xb8, 0x08, 0x46, 0x74, 0xf7, 0x43, 0x24, 0x8e, 0xe0, 0x35, 0x90, 0xe6, 0x81, 0x3a, 0x26, 0x4c,
  0x3c, 0x28, 0x52, 0xbb, 0x91, 0xc3, 0x00, 0xcb, 0x88, 0xd0, 0x65, 0x8b, 0x1b, 0x53, 0x2e, 0xa3,
  0x71, 0x64, 0x48, 0x97, 0xa2, 0x0d, 0xf9, 0x4e, 0x38, 0x19, 0xef, 0x46, 0xa9, 0xde, 0xac, 0xd8,
  0xa8, 0xfa, 0x76, 0x3f, 0xe3, 0x9c, 0x34, 0x3f, 0xf9, 0xdc, 0xbb, 0xc7, 0xc7, 0x0b, 0x4f, 0x1d,
  0x8a, 0x51, 0xe0, 0x4b, 0xcd, 0xb4, 0x59, 0x31, 0xc8, 0x9f, 0x7e, 0xc9, 0xd9, 0x78, 0x73, 0x64,
  0xea, 0xc5, 0xac, 0x83, 0x34, 0xd3, 0xeb, 0xc3, 0xc5, 0x81, 0xa0, 0xff, 0xfa, 0x13, 0x63, 0xeb,
  0x17, 0x0d, 0xdd, 0x51, 0xb7, 0xf0, 0xda, 0x49, 0xd3, 0x16, 0x55, 0x26, 0x29, 0xd4, 0x68, 0x9e,
  0x2b, 0x16, 0xbe, 0x58, 0x7d, 0x47, 0xa1, 0xfc, 0x8f, 0xf8, 0xb8, 0xd1, 0x7a, 0xd0, 0x31, 0xce,
  0x45, 0xcb, 0x3a, 0x8f, 0x95, 0x16, 0x04, 0x28, 0xaf, 0xd7, 0xfb, 0xca, 0xbb, 0x4b, 0x40, 0x7e,
]);

const SECRET_VIEW = new DataView(DEFAULT_SECRET.buffer, DEFAULT_SECRET.byteOffset, DEFAULT_SECRET.byteLength);

function readU32LE(bytes, offset) {
  return BigInt(
    (bytes[offset] |
      (bytes[offset + 1] << 8) |
      (bytes[offset + 2] << 16) |
      (bytes[offset + 3] << 24)) >>> 0
  );
}

function readU64LE(bytes, offset) {
  const lo = readU32LE(bytes, offset);
  const hi = readU32LE(bytes, offset + 4);
  return lo | (hi << 32n);
}

function readSecretU32LE(offset) {
  return BigInt(SECRET_VIEW.getUint32(offset, true));
}

function readSecretU64LE(offset) {
  return SECRET_VIEW.getBigUint64(offset, true);
}

function swap32(x) {
  return (
    ((x & 0x000000FFn) << 24n) |
    ((x & 0x0000FF00n) << 8n) |
    ((x & 0x00FF0000n) >> 8n) |
    ((x & 0xFF000000n) >> 24n)
  );
}

function swap64(x) {
  const lo = swap32(x & MASK32);
  const hi = swap32((x >> 32n) & MASK32);
  return (lo << 32n) | hi;
}

function rotl64(x, r) {
  return ((x << r) | (x >> (64n - r))) & MASK64;
}

function mul128Fold64(lhs, rhs) {
  const prod = lhs * rhs;
  return (prod & MASK64) ^ ((prod >> 64n) & MASK64);
}

function xxh64Avalanche(h) {
  h ^= h >> 33n;
  h = (h * PRIME64_2) & MASK64;
  h ^= h >> 29n;
  h = (h * PRIME64_3) & MASK64;
  h ^= h >> 32n;
  return h;
}

function xxh3Avalanche(h) {
  h ^= h >> 37n;
  h = (h * PRIME_MX1) & MASK64;
  h ^= h >> 32n;
  return h;
}

function rrmxmx(h, len) {
  h ^= rotl64(h, 49n) ^ rotl64(h, 24n);
  h = (h * PRIME_MX2) & MASK64;
  h ^= (h >> 35n) + len;
  h = (h * PRIME_MX2) & MASK64;
  return h ^ (h >> 28n);
}

function mix16B(bytes, inputOffset, secretOffset, seed) {
  const inputLo = readU64LE(bytes, inputOffset);
  const inputHi = readU64LE(bytes, inputOffset + 8);
  const keyLo = (readSecretU64LE(secretOffset) + seed) & MASK64;
  const keyHi = (readSecretU64LE(secretOffset + 8) - seed) & MASK64;
  return mul128Fold64(inputLo ^ keyLo, inputHi ^ keyHi);
}

function hashLen1to3(bytes, len) {
  const c1 = BigInt(bytes[0]);
  const c2 = BigInt(bytes[len >> 1]);
  const c3 = BigInt(bytes[len - 1]);
  const combined = (c1 << 16n) | (c2 << 24n) | c3 | (BigInt(len) << 8n);
  const bitflip = (readSecretU32LE(0) ^ readSecretU32LE(4)) & MASK32;
  const keyed = combined ^ bitflip;
  return xxh64Avalanche(keyed);
}

function hashLen4to8(bytes, len) {
  const input1 = readU32LE(bytes, 0);
  const input2 = readU32LE(bytes, len - 4);
  const bitflip = readSecretU64LE(8) ^ readSecretU64LE(16);
  const input64 = input2 + (input1 << 32n);
  const keyed = input64 ^ bitflip;
  return rrmxmx(keyed, BigInt(len));
}

function hashLen9to16(bytes, len) {
  const bitflip1 = readSecretU64LE(24) ^ readSecretU64LE(32);
  const bitflip2 = readSecretU64LE(40) ^ readSecretU64LE(48);
  const inputLo = readU64LE(bytes, 0) ^ bitflip1;
  const inputHi = readU64LE(bytes, len - 8) ^ bitflip2;
  const acc =
    (BigInt(len) + swap64(inputLo) + inputHi + mul128Fold64(inputLo, inputHi)) & MASK64;
  return xxh3Avalanche(acc);
}

function hashLen17to128(bytes, len) {
  let acc = (BigInt(len) * PRIME64_1) & MASK64;
  if (len > 32) {
    if (len > 64) {
      if (len > 96) {
        acc = (acc + mix16B(bytes, 48, 96, 0n)) & MASK64;
        acc = (acc + mix16B(bytes, len - 64, 112, 0n)) & MASK64;
      }
      acc = (acc + mix16B(bytes, 32, 64, 0n)) & MASK64;
      acc = (acc + mix16B(bytes, len - 48, 80, 0n)) & MASK64;
    }
    acc = (acc + mix16B(bytes, 16, 32, 0n)) & MASK64;
    acc = (acc + mix16B(bytes, len - 32, 48, 0n)) & MASK64;
  }
  acc = (acc + mix16B(bytes, 0, 0, 0n)) & MASK64;
  acc = (acc + mix16B(bytes, len - 16, 16, 0n)) & MASK64;
  return xxh3Avalanche(acc);
}

function hashLen129to240(bytes, len) {
  let acc = (BigInt(len) * PRIME64_1) & MASK64;
  const nbRounds = len >>> 4;
  for (let i = 0; i < 8; i++) {
    acc = (acc + mix16B(bytes, 16 * i, 16 * i, 0n)) & MASK64;
  }
  acc = xxh3Avalanche(acc);
  for (let i = 8; i < nbRounds; i++) {
    acc = (acc + mix16B(bytes, 16 * i, 16 * (i - 8) + 3, 0n)) & MASK64;
  }
  acc = (acc + mix16B(bytes, len - 16, 136 - 17, 0n)) & MASK64;
  return xxh3Avalanche(acc);
}

function accumulate512(acc, bytes, inputOffset, secretOffset) {
  for (let i = 0; i < 8; i++) {
    const dataVal = readU64LE(bytes, inputOffset + i * 8);
    const dataKey = dataVal ^ readSecretU64LE(secretOffset + i * 8);
    acc[i ^ 1] = (acc[i ^ 1] + dataVal) & MASK64;
    acc[i] = (acc[i] + ((dataKey & MASK32) * (dataKey >> 32n))) & MASK64;
  }
}

function scrambleAcc(acc, secretOffset) {
  for (let i = 0; i < 8; i++) {
    let v = acc[i];
    v ^= v >> 47n;
    v ^= readSecretU64LE(secretOffset + i * 8);
    v = (v * PRIME32_1) & MASK64;
    acc[i] = v;
  }
}

function mergeAccs(acc, secretOffset, start) {
  let result = start & MASK64;
  for (let i = 0; i < 4; i++) {
    const a0 = acc[2 * i] ^ readSecretU64LE(secretOffset + 16 * i);
    const a1 = acc[2 * i + 1] ^ readSecretU64LE(secretOffset + 16 * i + 8);
    result = (result + mul128Fold64(a0, a1)) & MASK64;
  }
  return xxh3Avalanche(result);
}

function hashLong(bytes, len) {
  const acc = [
    PRIME32_3,
    PRIME64_1,
    PRIME64_2,
    PRIME64_3,
    PRIME64_4,
    PRIME32_2,
    PRIME64_5,
    PRIME32_1,
  ];
  const secretLen = 192;
  const stripeLen = 64;
  const nbStripesPerBlock = (secretLen - stripeLen) / 8; // 16
  const blockLen = stripeLen * nbStripesPerBlock; // 1024
  const nbBlocks = Math.floor((len - 1) / blockLen);

  for (let n = 0; n < nbBlocks; n++) {
    for (let s = 0; s < nbStripesPerBlock; s++) {
      accumulate512(acc, bytes, n * blockLen + s * stripeLen, s * 8);
    }
    scrambleAcc(acc, secretLen - stripeLen);
  }

  const nbStripes = Math.floor((len - 1 - blockLen * nbBlocks) / stripeLen);
  for (let s = 0; s < nbStripes; s++) {
    accumulate512(acc, bytes, nbBlocks * blockLen + s * stripeLen, s * 8);
  }
  accumulate512(acc, bytes, len - stripeLen, secretLen - stripeLen - 7);

  return mergeAccs(acc, 11, (BigInt(len) * PRIME64_1) & MASK64);
}

/**
 * Compute XXH3_64bits(data) with default seed (0).
 * @param {Uint8Array | Buffer} data
 * @returns {bigint} 64-bit unsigned hash
 */
export function xxh3_64bits(data) {
  const len = data.length;
  if (len === 0) {
    return xxh64Avalanche(readSecretU64LE(56) ^ readSecretU64LE(64));
  }
  if (len <= 3) return hashLen1to3(data, len);
  if (len <= 8) return hashLen4to8(data, len);
  if (len <= 16) return hashLen9to16(data, len);
  if (len <= 128) return hashLen17to128(data, len);
  if (len <= 240) return hashLen129to240(data, len);
  return hashLong(data, len);
}

export function formatHash64(h) {
  return h.toString(16).toUpperCase().padStart(16, '0');
}
