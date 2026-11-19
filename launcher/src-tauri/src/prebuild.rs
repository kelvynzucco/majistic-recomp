use flate2::read::ZlibDecoder;
use serde::Deserialize;
use std::collections::HashMap;
use std::fs;
use std::io::Read;
use std::path::{Path, PathBuf};

// =============================================================================
// Pure-Rust XXH3_64bits (seed = 0, default secret)
// =============================================================================

const PRIME32_1: u64 = 0x9E3779B1;
const PRIME32_2: u64 = 0x85EBCA77;
const PRIME32_3: u64 = 0xC2B2AE3D;

const PRIME64_1: u64 = 0x9E3779B185EBCA87;
const PRIME64_2: u64 = 0xC2B2AE3D27D4EB4F;
const PRIME64_3: u64 = 0x165667B19E3779F9;
const PRIME64_4: u64 = 0x85EBCA77C2B2AE63;
const PRIME64_5: u64 = 0x27D4EB2F165667C5;

const PRIME_MX1: u64 = 0x165667919E3779F9;
const PRIME_MX2: u64 = 0x9FB21C651E98DF25;

const DEFAULT_SECRET: [u8; 192] = [
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
];

#[inline]
fn read_u32_le(bytes: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes([
        bytes[offset],
        bytes[offset + 1],
        bytes[offset + 2],
        bytes[offset + 3],
    ])
}

#[inline]
fn read_u64_le(bytes: &[u8], offset: usize) -> u64 {
    u64::from_le_bytes([
        bytes[offset],
        bytes[offset + 1],
        bytes[offset + 2],
        bytes[offset + 3],
        bytes[offset + 4],
        bytes[offset + 5],
        bytes[offset + 6],
        bytes[offset + 7],
    ])
}

#[inline]
fn read_secret_u32(offset: usize) -> u32 {
    read_u32_le(&DEFAULT_SECRET, offset)
}

#[inline]
fn read_secret_u64(offset: usize) -> u64 {
    read_u64_le(&DEFAULT_SECRET, offset)
}

#[inline]
fn mul128_fold64(lhs: u64, rhs: u64) -> u64 {
    let prod = (lhs as u128).wrapping_mul(rhs as u128);
    (prod as u64) ^ ((prod >> 64) as u64)
}

#[inline]
fn xxh64_avalanche(mut h: u64) -> u64 {
    h ^= h >> 33;
    h = h.wrapping_mul(PRIME64_2);
    h ^= h >> 29;
    h = h.wrapping_mul(PRIME64_3);
    h ^= h >> 32;
    h
}

#[inline]
fn xxh3_avalanche(mut h: u64) -> u64 {
    h ^= h >> 37;
    h = h.wrapping_mul(PRIME_MX1);
    h ^= h >> 32;
    h
}

#[inline]
fn rrmxmx(mut h: u64, len: u64) -> u64 {
    h ^= h.rotate_left(49) ^ h.rotate_left(24);
    h = h.wrapping_mul(PRIME_MX2);
    h ^= (h >> 35).wrapping_add(len);
    h = h.wrapping_mul(PRIME_MX2);
    h ^ (h >> 28)
}

#[inline]
fn mix16b(bytes: &[u8], input_offset: usize, secret_offset: usize) -> u64 {
    let input_lo = read_u64_le(bytes, input_offset);
    let input_hi = read_u64_le(bytes, input_offset + 8);
    let key_lo = read_secret_u64(secret_offset);
    let key_hi = read_secret_u64(secret_offset + 8);
    mul128_fold64(input_lo ^ key_lo, input_hi ^ key_hi)
}

fn accumulate_512(acc: &mut [u64; 8], bytes: &[u8], input_offset: usize, secret_offset: usize) {
    for i in 0..8 {
        let data_val = read_u64_le(bytes, input_offset + i * 8);
        let data_key = data_val ^ read_secret_u64(secret_offset + i * 8);
        acc[i ^ 1] = acc[i ^ 1].wrapping_add(data_val);
        let prod = (data_key & 0xFFFF_FFFF).wrapping_mul(data_key >> 32);
        acc[i] = acc[i].wrapping_add(prod);
    }
}

fn scramble_acc(acc: &mut [u64; 8], secret_offset: usize) {
    for i in 0..8 {
        let mut v = acc[i];
        v ^= v >> 47;
        v ^= read_secret_u64(secret_offset + i * 8);
        v = v.wrapping_mul(PRIME32_1);
        acc[i] = v;
    }
}

fn merge_accs(acc: &[u64; 8], secret_offset: usize, start: u64) -> u64 {
    let mut result = start;
    for i in 0..4 {
        let a0 = acc[2 * i] ^ read_secret_u64(secret_offset + 16 * i);
        let a1 = acc[2 * i + 1] ^ read_secret_u64(secret_offset + 16 * i + 8);
        result = result.wrapping_add(mul128_fold64(a0, a1));
    }
    xxh3_avalanche(result)
}

pub fn xxh3_64bits(bytes: &[u8]) -> u64 {
    let len = bytes.len();
    if len == 0 {
        return xxh64_avalanche(read_secret_u64(56) ^ read_secret_u64(64));
    }
    if len <= 3 {
        let c1 = bytes[0] as u64;
        let c2 = bytes[len >> 1] as u64;
        let c3 = bytes[len - 1] as u64;
        let combined = (c1 << 16) | (c2 << 24) | c3 | ((len as u64) << 8);
        let bitflip = (read_secret_u32(0) ^ read_secret_u32(4)) as u64;
        return xxh64_avalanche(combined ^ bitflip);
    }
    if len <= 8 {
        let input1 = read_u32_le(bytes, 0) as u64;
        let input2 = read_u32_le(bytes, len - 4) as u64;
        let bitflip = read_secret_u64(8) ^ read_secret_u64(16);
        let input64 = input2.wrapping_add(input1 << 32);
        return rrmxmx(input64 ^ bitflip, len as u64);
    }
    if len <= 16 {
        let bitflip1 = read_secret_u64(24) ^ read_secret_u64(32);
        let bitflip2 = read_secret_u64(40) ^ read_secret_u64(48);
        let input_lo = read_u64_le(bytes, 0) ^ bitflip1;
        let input_hi = read_u64_le(bytes, len - 8) ^ bitflip2;
        let acc = (len as u64)
            .wrapping_add(input_lo.swap_bytes())
            .wrapping_add(input_hi)
            .wrapping_add(mul128_fold64(input_lo, input_hi));
        return xxh3_avalanche(acc);
    }
    if len <= 128 {
        let mut acc = (len as u64).wrapping_mul(PRIME64_1);
        if len > 32 {
            if len > 64 {
                if len > 96 {
                    acc = acc.wrapping_add(mix16b(bytes, 48, 96));
                    acc = acc.wrapping_add(mix16b(bytes, len - 64, 112));
                }
                acc = acc.wrapping_add(mix16b(bytes, 32, 64));
                acc = acc.wrapping_add(mix16b(bytes, len - 48, 80));
            }
            acc = acc.wrapping_add(mix16b(bytes, 16, 32));
            acc = acc.wrapping_add(mix16b(bytes, len - 32, 48));
        }
        acc = acc.wrapping_add(mix16b(bytes, 0, 0));
        acc = acc.wrapping_add(mix16b(bytes, len - 16, 16));
        return xxh3_avalanche(acc);
    }
    if len <= 240 {
        let mut acc = (len as u64).wrapping_mul(PRIME64_1);
        let nb_rounds = len / 16;
        for i in 0..8 {
            acc = acc.wrapping_add(mix16b(bytes, 16 * i, 16 * i));
        }
        acc = xxh3_avalanche(acc);
        for i in 8..nb_rounds {
            acc = acc.wrapping_add(mix16b(bytes, 16 * i, 16 * (i - 8) + 3));
        }
        acc = acc.wrapping_add(mix16b(bytes, len - 16, 136 - 17));
        return xxh3_avalanche(acc);
    }

    let mut acc: [u64; 8] = [
        PRIME32_3, PRIME64_1, PRIME64_2, PRIME64_3, PRIME64_4, PRIME32_2, PRIME64_5, PRIME32_1,
    ];
    let secret_len = 192;
    let stripe_len = 64;
    let nb_stripes_per_block = (secret_len - stripe_len) / 8; // 16
    let block_len = stripe_len * nb_stripes_per_block; // 1024
    let nb_blocks = (len - 1) / block_len;

    for n in 0..nb_blocks {
        for s in 0..nb_stripes_per_block {
            accumulate_512(&mut acc, bytes, n * block_len + s * stripe_len, s * 8);
        }
        scramble_acc(&mut acc, secret_len - stripe_len);
    }

    let nb_stripes = (len - 1 - block_len * nb_blocks) / stripe_len;
    for s in 0..nb_stripes {
        accumulate_512(&mut acc, bytes, nb_blocks * block_len + s * stripe_len, s * 8);
    }
    accumulate_512(&mut acc, bytes, len - stripe_len, secret_len - stripe_len - 7);

    merge_accs(&acc, 11, (len as u64).wrapping_mul(PRIME64_1))
}

// =============================================================================
// Clean-Room Shader Prebuild Reconstructor
// =============================================================================

#[derive(Debug, Deserialize)]
pub struct BytePatch {
    pub offset: usize,
    pub hex: String,
}

#[derive(Debug, Deserialize)]
pub struct ShaderRecipe {
    pub hash: String,
    pub shader_type: String,
    pub dword_count: u32,
    pub byte_length: usize,
    pub mode: String,
    pub source_file: String,
    pub zlib_offset: Option<usize>,
    pub zlib_compressed_size: Option<usize>,
    pub uncompressed_offset: Option<usize>,
    pub raw_offset: Option<usize>,
    pub patch_offset: Option<usize>,
    pub vfetch_patch_hex: Option<String>,
    pub patches: Option<Vec<BytePatch>>,
}

#[allow(dead_code)]
#[derive(Debug, Deserialize)]
pub struct ShaderRecipesManifest {
    pub format_version: u32,
    pub title_id: String,
    pub pipeline_list_file: String,
    pub recipes: Vec<ShaderRecipe>,
}

#[allow(dead_code)]
pub struct RebuildSummary {
    pub reconstructed_count: usize,
    pub skipped_raw_count: usize,
    pub total_recipes: usize,
    pub xsh_path: PathBuf,
    pub xpl_path: PathBuf,
}

fn decode_hex(hex_str: &str) -> Result<Vec<u8>, String> {
    if hex_str.len() % 2 != 0 {
        return Err(format!("Odd hex string length: {}", hex_str.len()));
    }
    let mut out = Vec::with_capacity(hex_str.len() / 2);
    for i in (0..hex_str.len()).step_by(2) {
        let byte = u8::from_str_radix(&hex_str[i..i + 2], 16)
            .map_err(|e| format!("Invalid hex at {}: {}", i, e))?;
        out.push(byte);
    }
    Ok(out)
}

fn find_prebuild_resources_dir(root: &Path) -> Result<PathBuf, String> {
    let candidates = [
        root.join("launcher/resources/prebuild"),
        root.join("resources/prebuild"),
    ];
    for dir in &candidates {
        if dir.join("shader_recipes.json").exists() && dir.join("4E4D0816.fbo.vk.xpl").exists() {
            return Ok(dir.clone());
        }
    }
    Err(format!(
        "Clean-room prebuild resources (shader_recipes.json / 4E4D0816.fbo.vk.xpl) not found under {}",
        root.display()
    ))
}

fn resolve_iso_source_file(game_data_dir: &Path, rel_source: &str) -> Result<PathBuf, String> {
    let candidates = [
        game_data_dir.join("finalizedXBox360").join(rel_source),
        game_data_dir.join(rel_source),
    ];
    for p in &candidates {
        if p.exists() {
            return Ok(p.clone());
        }
    }
    Err(format!(
        "Required ISO asset '{}' not found in '{}' (or finalizedXBox360/). Please extract your game ISO first.",
        rel_source,
        game_data_dir.display()
    ))
}

fn resolve_decrypted_loaded_image(root: &Path, game_data_dir: &Path) -> Option<PathBuf> {
    let candidates = [
        game_data_dir.join("unpacked/loaded-image.bin"),
        root.join("prototype/private/loaded-image.bin"),
        root.join("prototype-usa/private/loaded-image.bin"),
    ];
    for p in &candidates {
        if let Ok(mut f) = fs::File::open(p) {
            let mut magic = [0u8; 2];
            if f.read_exact(&mut magic).is_ok() && &magic == b"MZ" {
                return Some(p.clone());
            }
        }
    }
    None
}

pub fn rebuild_prebuild_shader_cache(
    root: &Path,
    game_data_dir: &Path,
    configuration: &str,
    emit_log: &dyn Fn(&str),
) -> Result<RebuildSummary, String> {
    let res_dir = find_prebuild_resources_dir(root)?;
    let recipes_path = res_dir.join("shader_recipes.json");
    let xpl_src_path = res_dir.join("4E4D0816.fbo.vk.xpl");

    let json_text = fs::read_to_string(&recipes_path)
        .map_err(|e| format!("Failed to read {}: {}", recipes_path.display(), e))?;
    let manifest: ShaderRecipesManifest = serde_json::from_str(&json_text)
        .map_err(|e| format!("Failed to parse shader_recipes.json: {}", e))?;

    let decrypted_loaded_image = resolve_decrypted_loaded_image(root, game_data_dir);
    if let Some(ref img_path) = decrypted_loaded_image {
        emit_log(&format!(
            "[Clean-Room] Found decrypted PE executable image: {}",
            img_path.display()
        ));
    } else {
        emit_log(
            "[Clean-Room] Decrypted PE loaded-image.bin (MZ) not present; skipping 5 raw_* D3D9 clear/blit recipes.",
        );
    }

    let mut file_cache: HashMap<String, Vec<u8>> = HashMap::new();
    let mut zlib_cache: HashMap<(String, usize, usize), Vec<u8>> = HashMap::new();

    // Build XSH binary payload:
    // Header: magic = 0x48534558 ('XESH') LE, version_swapped = 0x20201219 BE
    let mut xsh_out: Vec<u8> = Vec::with_capacity(340 * 1024);
    xsh_out.extend_from_slice(&0x48534558u32.to_le_bytes());
    xsh_out.extend_from_slice(&0x20201219u32.to_be_bytes());

    let mut reconstructed_count = 0usize;
    let mut skipped_raw_count = 0usize;

    for recipe in &manifest.recipes {
        if recipe.source_file == "loaded-image.bin" && decrypted_loaded_image.is_none() {
            skipped_raw_count += 1;
            continue;
        }

        if (recipe.dword_count as usize) * 4 != recipe.byte_length {
            return Err(format!(
                "Recipe {} has inconsistent dword_count ({}) vs byte_length ({})",
                recipe.hash, recipe.dword_count, recipe.byte_length
            ));
        }

        let mut ucode: Vec<u8> = match recipe.mode.as_str() {
            "zlib_exact" | "zlib_vfetch_patch" | "zlib_vfetch_reg_patches" => {
                let z_off = recipe
                    .zlib_offset
                    .ok_or_else(|| format!("Recipe {} missing zlib_offset", recipe.hash))?;
                let z_size = recipe.zlib_compressed_size.ok_or_else(|| {
                    format!("Recipe {} missing zlib_compressed_size", recipe.hash)
                })?;
                let u_off = recipe
                    .uncompressed_offset
                    .ok_or_else(|| format!("Recipe {} missing uncompressed_offset", recipe.hash))?;

                let cache_key = (recipe.source_file.clone(), z_off, z_size);
                if !zlib_cache.contains_key(&cache_key) {
                    if !file_cache.contains_key(&recipe.source_file) {
                        let full_path = resolve_iso_source_file(game_data_dir, &recipe.source_file)?;
                        let bytes = fs::read(&full_path).map_err(|e| {
                            format!("Failed to read ISO file {}: {}", full_path.display(), e)
                        })?;
                        file_cache.insert(recipe.source_file.clone(), bytes);
                    }
                    let src_bytes = file_cache.get(&recipe.source_file).unwrap();
                    if z_off + z_size > src_bytes.len() {
                        return Err(format!(
                            "Recipe {} zlib slice out of bounds in {}",
                            recipe.hash, recipe.source_file
                        ));
                    }
                    let mut decoder = ZlibDecoder::new(&src_bytes[z_off..z_off + z_size]);
                    let mut inflated = Vec::new();
                    decoder.read_to_end(&mut inflated).map_err(|e| {
                        format!(
                            "Failed to inflate zlib block at {}+{} in {}: {}",
                            z_off, z_size, recipe.source_file, e
                        )
                    })?;
                    zlib_cache.insert(cache_key.clone(), inflated);
                }

                let inflated = zlib_cache.get(&cache_key).unwrap();
                if u_off + recipe.byte_length > inflated.len() {
                    return Err(format!(
                        "Recipe {} uncompressed slice out of bounds ({}+{} > {})",
                        recipe.hash,
                        u_off,
                        recipe.byte_length,
                        inflated.len()
                    ));
                }
                inflated[u_off..u_off + recipe.byte_length].to_vec()
            }
            "raw_exact" | "raw_descriptor_patches" => {
                let raw_off = recipe
                    .raw_offset
                    .ok_or_else(|| format!("Recipe {} missing raw_offset", recipe.hash))?;
                if !file_cache.contains_key("loaded-image.bin") {
                    let img_path = decrypted_loaded_image.as_ref().unwrap();
                    let bytes = fs::read(img_path).map_err(|e| {
                        format!("Failed to read {}: {}", img_path.display(), e)
                    })?;
                    file_cache.insert("loaded-image.bin".to_string(), bytes);
                }
                let raw_bytes = file_cache.get("loaded-image.bin").unwrap();
                if raw_off + recipe.byte_length > raw_bytes.len() {
                    return Err(format!(
                        "Recipe {} raw slice out of bounds in loaded-image.bin",
                        recipe.hash
                    ));
                }
                raw_bytes[raw_off..raw_off + recipe.byte_length].to_vec()
            }
            other => {
                return Err(format!("Unsupported recipe mode '{}' for {}", other, recipe.hash));
            }
        };

        // Apply patches if present
        if recipe.mode == "zlib_vfetch_patch" {
            let p_off = recipe
                .patch_offset
                .ok_or_else(|| format!("Recipe {} missing patch_offset", recipe.hash))?;
            let patch_hex = recipe
                .vfetch_patch_hex
                .as_ref()
                .ok_or_else(|| format!("Recipe {} missing vfetch_patch_hex", recipe.hash))?;
            let patch_bytes = decode_hex(patch_hex)?;
            if p_off + patch_bytes.len() > ucode.len() {
                return Err(format!("Recipe {} vfetch patch out of bounds", recipe.hash));
            }
            ucode[p_off..p_off + patch_bytes.len()].copy_from_slice(&patch_bytes);
        } else if recipe.mode == "zlib_vfetch_reg_patches"
            || recipe.mode == "raw_descriptor_patches"
        {
            let patches = recipe
                .patches
                .as_ref()
                .ok_or_else(|| format!("Recipe {} missing patches array", recipe.hash))?;
            for p in patches {
                let patch_bytes = decode_hex(&p.hex)?;
                if p.offset + patch_bytes.len() > ucode.len() {
                    return Err(format!(
                        "Recipe {} patch at offset {} out of bounds",
                        recipe.hash, p.offset
                    ));
                }
                ucode[p.offset..p.offset + patch_bytes.len()].copy_from_slice(&patch_bytes);
            }
        }

        // Verify XXH3_64bits hash
        let expected_hash = u64::from_str_radix(&recipe.hash, 16)
            .map_err(|e| format!("Invalid recipe hash '{}': {}", recipe.hash, e))?;
        let computed_hash = xxh3_64bits(&ucode);
        if computed_hash != expected_hash {
            return Err(format!(
                "Clean-Room XXH3_64bits verification failed for shader {}: expected {:016X}, got {:016X}",
                recipe.hash, expected_hash, computed_hash
            ));
        }

        let type_bit: u32 = if recipe.shader_type == "ps" { 1 } else { 0 };
        let count_type: u32 = (recipe.dword_count & 0x7FFF_FFFF) | (type_bit << 31);

        xsh_out.extend_from_slice(&expected_hash.to_le_bytes());
        xsh_out.extend_from_slice(&count_type.to_le_bytes());
        xsh_out.extend_from_slice(&ucode);
        reconstructed_count += 1;
    }

    // Write to <cache_root>/shaders/prebuild/ for both 'release' and active configuration
    let cfg_lower = {
        let c = configuration.trim().to_lowercase();
        if c.is_empty() {
            "release".to_string()
        } else {
            c
        }
    };
    let mut target_configs = vec!["release".to_string()];
    if cfg_lower != "release" {
        target_configs.push(cfg_lower.clone());
    }

    let mut primary_xsh_path = PathBuf::new();
    let mut primary_xpl_path = PathBuf::new();

    for cfg in &target_configs {
        let prebuild_dir = game_data_dir
            .join("runtime/cache/vulkan-vulkan")
            .join(cfg)
            .join("shaders/prebuild");
        fs::create_dir_all(&prebuild_dir).map_err(|e| {
            format!(
                "Failed to create prebuild directory {}: {}",
                prebuild_dir.display(),
                e
            )
        })?;

        let dst_xsh = prebuild_dir.join(format!("{}.xsh", manifest.title_id));
        let dst_xpl = prebuild_dir.join(&manifest.pipeline_list_file);

        fs::write(&dst_xsh, &xsh_out)
            .map_err(|e| format!("Failed to write {}: {}", dst_xsh.display(), e))?;
        fs::copy(&xpl_src_path, &dst_xpl)
            .map_err(|e| format!("Failed to copy {}: {}", dst_xpl.display(), e))?;

        if cfg == &cfg_lower || primary_xsh_path.as_os_str().is_empty() {
            primary_xsh_path = dst_xsh;
            primary_xpl_path = dst_xpl;
        }
    }

    emit_log(&format!(
        "[Clean-Room] Reconstructed {}/{} Xenos shaders ({} skipped raw_*) — 100% XXH3_64bits verified ({} bytes)",
        reconstructed_count,
        manifest.recipes.len(),
        skipped_raw_count,
        xsh_out.len()
    ));
    emit_log(&format!(
        "[Clean-Room] Deployed prebuild cache to: {}",
        primary_xsh_path
            .parent()
            .unwrap_or(&primary_xsh_path)
            .display()
    ));

    Ok(RebuildSummary {
        reconstructed_count,
        skipped_raw_count,
        total_recipes: manifest.recipes.len(),
        xsh_path: primary_xsh_path,
        xpl_path: primary_xpl_path,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_clean_room_rebuild_and_xxh3() {
        let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
        let root = manifest_dir
            .parent()
            .and_then(|p| p.parent())
            .expect("repo root");
        let game_data_dir = root.join("game-data");
        let summary = rebuild_prebuild_shader_cache(root, &game_data_dir, "Release", &|msg| {
            println!("{}", msg);
        })
        .expect("rebuild_prebuild_shader_cache failed");

        assert_eq!(summary.total_recipes, 257);
        assert_eq!(summary.reconstructed_count, 257);
        assert_eq!(summary.skipped_raw_count, 0);
        assert!(summary.xsh_path.exists());
        assert!(summary.xpl_path.exists());
    }
}
