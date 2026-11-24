use std::collections::HashSet;
use std::fs;
use std::io::{Read, Write};
use std::path::{Component, Path, PathBuf};
use std::process::Command;
use std::time::{SystemTime, UNIX_EPOCH};

use chrono::{DateTime, Local};
use serde::{Deserialize, Serialize};
use zip::write::SimpleFileOptions;
use zip::{CompressionMethod, ZipArchive, ZipWriter};

use crate::{get_project_root, load_settings_internal as load_settings, save_settings_internal as save_settings, Settings};

pub const DEFAULT_PROFILE_ID: &str = "default";
pub const DEFAULT_PROFILE_NAME: &str = "Default Save";
const SAVISTIC_FORMAT: &str = "savistic";
const SAVISTIC_VERSION: u32 = 1;
const TITLE_ID: &str = "4E4D0816";

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SaveProfileRecord {
    pub id: String,
    pub name: String,
    pub created_at: String,
    #[serde(default)]
    pub is_default: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SaveRegistry {
    pub active_profile_id: String,
    pub profiles: Vec<SaveProfileRecord>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SaveProfileInfo {
    pub id: String,
    pub name: String,
    pub is_default: bool,
    pub is_active: bool,
    pub created_at: String,
    pub last_modified: Option<String>,
    pub save_slot_count: usize,
    pub has_save_data: bool,
    pub has_achievements: bool,
    pub achievement_count: usize,
    pub size_bytes: u64,
    pub size_formatted: String,
    pub path: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SaveProfilesState {
    pub active_profile_id: String,
    pub profiles: Vec<SaveProfileInfo>,
    pub game_running: bool,
    pub saves_root_path: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SavisticManifest {
    pub format: String,
    pub version: u32,
    pub title_id: String,
    pub name: String,
    pub exported_at: String,
    #[serde(default)]
    pub includes_achievements: bool,
    #[serde(default)]
    pub includes_launcher_config: bool,
    #[serde(default)]
    pub save_slot_count: usize,
    #[serde(default)]
    pub achievement_count: usize,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct LauncherConfigSummary {
    pub window_size: String,
    pub fullscreen: bool,
    pub render_scale: u32,
    pub rendering_path: String,
    pub frame_rate_mode: String,
    pub input_mode: String,
    pub language: String,
    #[serde(default)]
    pub audio_language: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SavisticPreview {
    pub file_path: String,
    pub file_name: String,
    pub save_name: String,
    pub exported_at: Option<String>,
    pub save_slot_count: usize,
    pub has_achievements: bool,
    pub achievement_count: usize,
    pub has_launcher_config: bool,
    pub launcher_config_summary: Option<LauncherConfigSummary>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ImportSaveResult {
    pub state: SaveProfilesState,
    pub imported_profile_id: String,
    pub imported_profile_name: String,
    pub settings_updated: bool,
    pub settings: Option<Settings>,
}

fn format_bytes(bytes: u64) -> String {
    if bytes < 1024 {
        format!("{} B", bytes)
    } else if bytes < 1024 * 1024 {
        format!("{:.1} KB", bytes as f64 / 1024.0)
    } else {
        format!("{:.2} MB", bytes as f64 / (1024.0 * 1024.0))
    }
}

fn now_display_timestamp() -> String {
    Local::now().format("%Y-%m-%d %H:%M").to_string()
}

fn now_iso_timestamp() -> String {
    Local::now().format("%Y-%m-%d %H:%M:%S").to_string()
}

fn is_valid_profile_id(id: &str) -> bool {
    !id.is_empty()
        && id.len() <= 64
        && id
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '-')
}

fn resolve_runtime_root_from(root: &Path, settings: &Settings) -> PathBuf {
    let folder_name = if settings.game_data_root.trim().is_empty() {
        "game-data".to_string()
    } else {
        settings.game_data_root.trim().to_string()
    };

    let game_data_dir = if Path::new(&folder_name).is_absolute() {
        PathBuf::from(&folder_name)
    } else {
        root.join(&folder_name)
    };

    let primary = game_data_dir.join("runtime");
    if primary.exists() {
        return primary;
    }

    let legacy = root.join("private/runtime-usa");
    if legacy.exists() {
        return legacy;
    }

    primary
}

fn resolve_runtime_root() -> PathBuf {
    let root = get_project_root();
    let settings = load_settings().unwrap_or_default();
    resolve_runtime_root_from(&root, &settings)
}

fn saves_container_dir(runtime_root: &Path) -> PathBuf {
    runtime_root.join("saves")
}

fn registry_path(runtime_root: &Path) -> PathBuf {
    saves_container_dir(runtime_root).join("profiles.json")
}

fn legacy_user_dir(runtime_root: &Path) -> PathBuf {
    runtime_root.join("user")
}

fn dir_has_entries(dir: &Path) -> bool {
    fs::read_dir(dir)
        .ok()
        .and_then(|mut it| it.next())
        .is_some()
}

fn migrate_legacy_default_profile_inner(runtime_root: &Path) {
    let legacy_dir = legacy_user_dir(runtime_root);
    if !legacy_dir.exists() {
        return;
    }

    let target_dir = saves_container_dir(runtime_root).join(DEFAULT_PROFILE_ID);
    if !dir_has_entries(&legacy_dir) {
        let _ = fs::remove_dir(&legacy_dir);
        return;
    }

    if target_dir.exists() {
        if dir_has_entries(&target_dir) {
            return;
        }
        let _ = fs::remove_dir(&target_dir);
    }

    if let Some(parent) = target_dir.parent() {
        if fs::create_dir_all(parent).is_err() {
            return;
        }
    }

    if fs::rename(&legacy_dir, &target_dir).is_err() {
        if copy_dir_recursive(&legacy_dir, &target_dir).is_ok() {
            let _ = fs::remove_dir_all(&legacy_dir);
        }
    }
}

fn migrate_legacy_default_profile(runtime_root: &Path) {
    if is_majin_game_running() {
        return;
    }
    migrate_legacy_default_profile_inner(runtime_root);
}

fn profile_dir(runtime_root: &Path, profile_id: &str) -> Result<PathBuf, String> {
    if !is_valid_profile_id(profile_id) {
        return Err(format!("Invalid save profile ID: '{}'", profile_id));
    }
    Ok(saves_container_dir(runtime_root).join(profile_id))
}

fn generate_profile_id(existing: &[SaveProfileRecord]) -> String {
    let millis = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis())
        .unwrap_or(0);
    let base = format!("save_{}", millis);
    if !existing.iter().any(|p| p.id == base) {
        return base;
    }
    for suffix in 1..1000u32 {
        let candidate = format!("{}_{}", base, suffix);
        if !existing.iter().any(|p| p.id == candidate) {
            return candidate;
        }
    }
    format!("save_{}_x", millis)
}

fn sanitize_profile_name(raw: &str) -> String {
    let cleaned: String = raw
        .trim()
        .chars()
        .filter(|c| !c.is_control())
        .take(64)
        .collect();
    cleaned.trim().to_string()
}

fn next_default_profile_name(existing: &[SaveProfileRecord]) -> String {
    let names: HashSet<String> = existing.iter().map(|p| p.name.to_lowercase()).collect();
    for idx in 1..=9999u32 {
        let candidate = format!("Save {}", idx);
        if !names.contains(&candidate.to_lowercase()) {
            return candidate;
        }
    }
    format!("Save {}", existing.len() + 1)
}

fn next_duplicate_profile_name(source_name: &str, existing: &[SaveProfileRecord]) -> String {
    let names: HashSet<String> = existing.iter().map(|p| p.name.to_lowercase()).collect();
    let first = format!("{} (Copy)", source_name);
    if !names.contains(&first.to_lowercase()) {
        return sanitize_profile_name(&first);
    }
    for idx in 2..=999u32 {
        let candidate = format!("{} (Copy {})", source_name, idx);
        if !names.contains(&candidate.to_lowercase()) {
            return sanitize_profile_name(&candidate);
        }
    }
    next_default_profile_name(existing)
}

fn sanitize_filename_stem(name: &str) -> String {
    let mut out = String::new();
    for ch in name.trim().chars() {
        if matches!(ch, '<' | '>' | ':' | '"' | '/' | '\\' | '|' | '?' | '*') || ch.is_control() {
            out.push('_');
        } else {
            out.push(ch);
        }
    }
    let trimmed = out.trim().trim_matches('.').to_string();
    if trimmed.is_empty() {
        "MajisticSave".to_string()
    } else {
        trimmed.chars().take(60).collect()
    }
}

fn load_or_init_registry(runtime_root: &Path, preferred_active: Option<&str>) -> Result<SaveRegistry, String> {
    let reg_path = registry_path(runtime_root);
    let mut dirty = false;

    let mut registry = if reg_path.exists() {
        let raw = fs::read_to_string(&reg_path)
            .map_err(|e| format!("Failed to read save profiles registry: {}", e))?;
        match serde_json::from_str::<SaveRegistry>(&raw) {
            Ok(reg) => reg,
            Err(_) => {
                dirty = true;
                SaveRegistry {
                    active_profile_id: DEFAULT_PROFILE_ID.to_string(),
                    profiles: vec![],
                }
            }
        }
    } else {
        dirty = true;
        SaveRegistry {
            active_profile_id: DEFAULT_PROFILE_ID.to_string(),
            profiles: vec![SaveProfileRecord {
                id: DEFAULT_PROFILE_ID.to_string(),
                name: DEFAULT_PROFILE_NAME.to_string(),
                created_at: now_display_timestamp(),
                is_default: true,
            }],
        }
    };

    // Keep only valid IDs and deduplicate
    let mut seen = HashSet::new();
    let orig_len = registry.profiles.len();
    registry.profiles.retain(|p| is_valid_profile_id(&p.id) && seen.insert(p.id.clone()));
    if registry.profiles.len() != orig_len {
        dirty = true;
    }

    // Ensure at least the default profile exists if list became empty
    if registry.profiles.is_empty() {
        registry.profiles.push(SaveProfileRecord {
            id: DEFAULT_PROFILE_ID.to_string(),
            name: DEFAULT_PROFILE_NAME.to_string(),
            created_at: now_display_timestamp(),
            is_default: true,
        });
        dirty = true;
    }

    // If settings specifies an active profile that exists in registry, honor it if registry was just initialized
    if let Some(pref) = preferred_active {
        if !pref.trim().is_empty()
            && registry.profiles.iter().any(|p| p.id == pref)
            && registry.active_profile_id != pref
            && !reg_path.exists()
        {
            registry.active_profile_id = pref.to_string();
            dirty = true;
        }
    }

    // Ensure active_profile_id points to an existing profile
    if !registry.profiles.iter().any(|p| p.id == registry.active_profile_id) {
        registry.active_profile_id = registry.profiles[0].id.clone();
        dirty = true;
    }

    if dirty {
        save_registry(runtime_root, &registry)?;
    }

    if registry.profiles.iter().any(|p| p.id == DEFAULT_PROFILE_ID) {
        migrate_legacy_default_profile(runtime_root);
        let default_dir = saves_container_dir(runtime_root).join(DEFAULT_PROFILE_ID);
        let _ = fs::create_dir_all(&default_dir);
    }

    Ok(registry)
}

fn save_registry(runtime_root: &Path, registry: &SaveRegistry) -> Result<(), String> {
    let path = registry_path(runtime_root);
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)
            .map_err(|e| format!("Failed to create saves directory: {}", e))?;
    }
    let json = serde_json::to_string_pretty(registry)
        .map_err(|e| format!("Failed to serialize save registry: {}", e))?;
    fs::write(&path, json).map_err(|e| format!("Failed to write save registry: {}", e))?;
    Ok(())
}

fn sync_active_profile_to_settings(active_id: &str) {
    if let Ok(mut settings) = load_settings() {
        if settings.active_save_profile != active_id {
            settings.active_save_profile = active_id.to_string();
            let _ = save_settings(settings);
        }
    }
}

struct DirStats {
    size_bytes: u64,
    save_slot_count: usize,
    has_achievements: bool,
    achievement_count: usize,
    latest_modified: Option<SystemTime>,
}

fn count_unlocked_achievements_toml(content: &str) -> usize {
    content
        .lines()
        .map(|l| l.trim())
        .filter(|l| l.starts_with("[unlocked.") && l.ends_with(']'))
        .count()
}

fn inspect_profile_dir(dir: &Path) -> DirStats {
    let mut stats = DirStats {
        size_bytes: 0,
        save_slot_count: 0,
        has_achievements: false,
        achievement_count: 0,
        latest_modified: None,
    };

    if !dir.exists() {
        return stats;
    }

    fn walk(cur: &Path, stats: &mut DirStats) {
        let Ok(entries) = fs::read_dir(cur) else {
            return;
        };
        for entry in entries.flatten() {
            let path = entry.path();
            let Ok(meta) = entry.metadata() else {
                continue;
            };
            if meta.is_dir() {
                walk(&path, stats);
            } else if meta.is_file() {
                stats.size_bytes = stats.size_bytes.saturating_add(meta.len());

                if let Ok(mtime) = meta.modified() {
                    let is_newer = stats
                        .latest_modified
                        .map(|prev| mtime > prev)
                        .unwrap_or(true);
                    if is_newer {
                        stats.latest_modified = Some(mtime);
                    }
                }

                let file_name = path
                    .file_name()
                    .and_then(|n| n.to_str())
                    .unwrap_or("")
                    .to_ascii_lowercase();

                if file_name.ends_with(".dat") {
                    stats.save_slot_count += 1;
                } else if file_name == format!("{}.toml", TITLE_ID).to_ascii_lowercase() {
                    if let Ok(text) = fs::read_to_string(&path) {
                        let count = count_unlocked_achievements_toml(&text);
                        stats.achievement_count = count;
                        stats.has_achievements = count > 0 || !text.trim().is_empty();
                    }
                }
            }
        }
    }

    walk(dir, &mut stats);
    stats
}

fn build_profile_info(runtime_root: &Path, rec: &SaveProfileRecord, active_id: &str) -> SaveProfileInfo {
    let dir = profile_dir(runtime_root, &rec.id)
        .unwrap_or_else(|_| saves_container_dir(runtime_root).join(DEFAULT_PROFILE_ID));
    let stats = inspect_profile_dir(&dir);

    let last_modified = stats.latest_modified.map(|st| {
        let dt: DateTime<Local> = st.into();
        dt.format("%Y-%m-%d %H:%M").to_string()
    });

    SaveProfileInfo {
        id: rec.id.clone(),
        name: rec.name.clone(),
        is_default: rec.is_default || rec.id == DEFAULT_PROFILE_ID,
        is_active: rec.id == active_id,
        created_at: rec.created_at.clone(),
        last_modified,
        save_slot_count: stats.save_slot_count,
        has_save_data: stats.save_slot_count > 0,
        has_achievements: stats.has_achievements,
        achievement_count: stats.achievement_count,
        size_bytes: stats.size_bytes,
        size_formatted: format_bytes(stats.size_bytes),
        path: dir.to_string_lossy().to_string(),
    }
}

fn build_state(runtime_root: &Path, registry: &SaveRegistry) -> SaveProfilesState {
    let profiles: Vec<SaveProfileInfo> = registry
        .profiles
        .iter()
        .map(|p| build_profile_info(runtime_root, p, &registry.active_profile_id))
        .collect();

    SaveProfilesState {
        active_profile_id: registry.active_profile_id.clone(),
        profiles,
        game_running: is_majin_game_running(),
        saves_root_path: saves_container_dir(runtime_root).to_string_lossy().to_string(),
    }
}

pub fn is_majin_game_running() -> bool {
    #[cfg(windows)]
    {
        use windows_sys::Win32::Foundation::{CloseHandle, INVALID_HANDLE_VALUE};
        use windows_sys::Win32::System::Diagnostics::ToolHelp::{
            CreateToolhelp32Snapshot, Process32FirstW, Process32NextW, PROCESSENTRY32W,
            TH32CS_SNAPPROCESS,
        };

        unsafe {
            let snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if snapshot == INVALID_HANDLE_VALUE || snapshot.is_null() {
                return false;
            }

            let mut entry: PROCESSENTRY32W = std::mem::zeroed();
            entry.dwSize = std::mem::size_of::<PROCESSENTRY32W>() as u32;

            let mut found = false;
            if Process32FirstW(snapshot, &mut entry) != 0 {
                loop {
                    let len = entry
                        .szExeFile
                        .iter()
                        .position(|&c| c == 0)
                        .unwrap_or(entry.szExeFile.len());
                    let exe_name = String::from_utf16_lossy(&entry.szExeFile[..len])
                        .to_ascii_lowercase();
                    if exe_name == "majin.exe"
                        || exe_name == "majin-usa.exe"
                        || exe_name == "majin-pal.exe"
                    {
                        found = true;
                        break;
                    }
                    if Process32NextW(snapshot, &mut entry) == 0 {
                        break;
                    }
                }
            }

            CloseHandle(snapshot);
            found
        }
    }
    #[cfg(not(windows))]
    {
        false
    }
}

fn ensure_game_not_running(action_desc: &str) -> Result<(), String> {
    if is_majin_game_running() {
        return Err(format!(
            "Cannot {} while Majin is running. Please close the game first.",
            action_desc
        ));
    }
    Ok(())
}

fn copy_dir_recursive(src: &Path, dst: &Path) -> Result<(), String> {
    if !src.exists() {
        fs::create_dir_all(dst)
            .map_err(|e| format!("Failed to create directory {}: {}", dst.display(), e))?;
        return Ok(());
    }
    fs::create_dir_all(dst)
        .map_err(|e| format!("Failed to create directory {}: {}", dst.display(), e))?;

    for entry in fs::read_dir(src)
        .map_err(|e| format!("Failed to read directory {}: {}", src.display(), e))?
    {
        let entry = entry.map_err(|e| e.to_string())?;
        let src_path = entry.path();
        let dst_path = dst.join(entry.file_name());
        let file_type = entry.file_type().map_err(|e| e.to_string())?;
        if file_type.is_dir() {
            copy_dir_recursive(&src_path, &dst_path)?;
        } else if file_type.is_file() {
            fs::copy(&src_path, &dst_path).map_err(|e| {
                format!(
                    "Failed to copy {} to {}: {}",
                    src_path.display(),
                    dst_path.display(),
                    e
                )
            })?;
        }
    }
    Ok(())
}

fn add_dir_to_zip<W: Write + std::io::Seek>(
    zip: &mut ZipWriter<W>,
    base_dir: &Path,
    cur_dir: &Path,
    zip_prefix: &str,
    options: SimpleFileOptions,
) -> Result<(), String> {
    if !cur_dir.exists() {
        return Ok(());
    }

    for entry in fs::read_dir(cur_dir)
        .map_err(|e| format!("Failed to read {}: {}", cur_dir.display(), e))?
    {
        let entry = entry.map_err(|e| e.to_string())?;
        let path = entry.path();
        let meta = entry.metadata().map_err(|e| e.to_string())?;

        if meta.is_dir() {
            add_dir_to_zip(zip, base_dir, &path, zip_prefix, options)?;
        } else if meta.is_file() {
            let rel = path
                .strip_prefix(base_dir)
                .map_err(|e| e.to_string())?;
            let rel_str = rel
                .components()
                .map(|c| c.as_os_str().to_string_lossy())
                .collect::<Vec<_>>()
                .join("/");
            let archive_name = format!("{}/{}", zip_prefix.trim_end_matches('/'), rel_str);
            zip.start_file(&archive_name, options)
                .map_err(|e| format!("Failed to add {} to archive: {}", archive_name, e))?;
            let mut file = fs::File::open(&path)
                .map_err(|e| format!("Failed to open {}: {}", path.display(), e))?;
            std::io::copy(&mut file, zip)
                .map_err(|e| format!("Failed to write {} to archive: {}", archive_name, e))?;
        }
    }
    Ok(())
}

fn validate_safe_relative_path(raw: &str) -> Result<PathBuf, String> {
    let normalized = raw.replace('\\', "/");
    let path = Path::new(&normalized);
    let mut clean = PathBuf::new();
    for comp in path.components() {
        match comp {
            Component::Normal(part) => clean.push(part),
            Component::CurDir => {}
            Component::ParentDir | Component::RootDir | Component::Prefix(_) => {
                return Err(format!("Unsafe path inside archive: '{}'", raw));
            }
        }
    }
    if clean.as_os_str().is_empty() {
        return Err("Empty path inside archive".to_string());
    }
    Ok(clean)
}

fn export_profile_to_file(
    runtime_root: &Path,
    profile: &SaveProfileRecord,
    settings: &Settings,
    dest_path: &Path,
) -> Result<(), String> {
    let src_dir = profile_dir(runtime_root, &profile.id)?;
    let stats = inspect_profile_dir(&src_dir);

    let manifest = SavisticManifest {
        format: SAVISTIC_FORMAT.to_string(),
        version: SAVISTIC_VERSION,
        title_id: TITLE_ID.to_string(),
        name: profile.name.clone(),
        exported_at: now_iso_timestamp(),
        includes_achievements: stats.has_achievements,
        includes_launcher_config: true,
        save_slot_count: stats.save_slot_count,
        achievement_count: stats.achievement_count,
    };

    if let Some(parent) = dest_path.parent() {
        fs::create_dir_all(parent)
            .map_err(|e| format!("Failed to create destination folder: {}", e))?;
    }

    let file = fs::File::create(dest_path)
        .map_err(|e| format!("Failed to create {}: {}", dest_path.display(), e))?;
    let mut zip = ZipWriter::new(file);
    let options = SimpleFileOptions::default().compression_method(CompressionMethod::Deflated);

    // 1. manifest.json
    let manifest_json = serde_json::to_string_pretty(&manifest)
        .map_err(|e| format!("Failed to serialize manifest: {}", e))?;
    zip.start_file("manifest.json", options)
        .map_err(|e| format!("Failed to write manifest entry: {}", e))?;
    zip.write_all(manifest_json.as_bytes())
        .map_err(|e| format!("Failed to write manifest bytes: {}", e))?;

    // 2. launcher-config.json
    let config_json = serde_json::to_string_pretty(settings)
        .map_err(|e| format!("Failed to serialize launcher config: {}", e))?;
    zip.start_file("launcher-config.json", options)
        .map_err(|e| format!("Failed to write launcher config entry: {}", e))?;
    zip.write_all(config_json.as_bytes())
        .map_err(|e| format!("Failed to write launcher config bytes: {}", e))?;

    // 3. data/... (save files + achievements)
    if src_dir.exists() {
        add_dir_to_zip(&mut zip, &src_dir, &src_dir, "data", options)?;
    }

    zip.finish()
        .map_err(|e| format!("Failed to finalize .savistic archive: {}", e))?;

    Ok(())
}

fn inspect_savistic_file_inner(file_path: &Path) -> Result<SavisticPreview, String> {
    if !file_path.exists() {
        return Err(format!("File does not exist: {}", file_path.display()));
    }

    let file = fs::File::open(file_path)
        .map_err(|e| format!("Failed to open file: {}", e))?;
    let mut archive = ZipArchive::new(file)
        .map_err(|_| "Invalid .savistic file: archive is corrupted or not a valid package.".to_string())?;

    let mut manifest: Option<SavisticManifest> = None;
    let mut launcher_cfg: Option<Settings> = None;
    let mut detected_dat_count: usize = 0;
    let mut detected_has_title_data = false;
    let mut detected_achievements = false;
    let mut detected_achievement_count: usize = 0;

    for i in 0..archive.len() {
        let mut entry = archive
            .by_index(i)
            .map_err(|e| format!("Failed to read archive entry: {}", e))?;
        let raw_name = entry
            .name()
            .map_err(|e| format!("Invalid archive entry name: {}", e))?
            .into_owned();
        if raw_name.ends_with('/') {
            continue;
        }
        let clean = validate_safe_relative_path(&raw_name)?;
        let normalized = clean
            .components()
            .map(|c| c.as_os_str().to_string_lossy())
            .collect::<Vec<_>>()
            .join("/");
        let lower = normalized.to_ascii_lowercase();

        if lower == "manifest.json" {
            let mut text = String::new();
            entry
                .read_to_string(&mut text)
                .map_err(|e| format!("Failed to read manifest.json: {}", e))?;
            let parsed: SavisticManifest = serde_json::from_str(&text)
                .map_err(|e| format!("Invalid manifest.json in .savistic file: {}", e))?;
            if parsed.format != SAVISTIC_FORMAT {
                return Err(format!("Unsupported save package format: '{}'", parsed.format));
            }
            if !parsed.title_id.is_empty() && !parsed.title_id.eq_ignore_ascii_case(TITLE_ID) {
                return Err(format!(
                    "This save belongs to a different title ({}), expected {}.",
                    parsed.title_id, TITLE_ID
                ));
            }
            manifest = Some(parsed);
        } else if lower == "launcher-config.json" {
            let mut text = String::new();
            if entry.read_to_string(&mut text).is_ok() {
                if let Ok(parsed) = serde_json::from_str::<Settings>(&text) {
                    launcher_cfg = Some(parsed);
                }
            }
        } else {
            if lower.contains(&TITLE_ID.to_ascii_lowercase()) || lower.contains("kb_save_") {
                detected_has_title_data = true;
            }
            if lower.ends_with(".dat") {
                detected_dat_count += 1;
            }
            if lower.ends_with(&format!("{}.toml", TITLE_ID).to_ascii_lowercase()) {
                let mut text = String::new();
                if entry.read_to_string(&mut text).is_ok() {
                    detected_achievement_count = count_unlocked_achievements_toml(&text);
                    detected_achievements = detected_achievement_count > 0 || !text.trim().is_empty();
                }
            }
        }
    }

    if manifest.is_none() && !detected_has_title_data && detected_dat_count == 0 {
        return Err(
            "Invalid .savistic file: missing Majistic save manifest or game save data.".to_string(),
        );
    }

    let file_name = file_path
        .file_name()
        .and_then(|s| s.to_str())
        .unwrap_or("save.savistic")
        .to_string();

    let stem_name = file_path
        .file_stem()
        .and_then(|s| s.to_str())
        .map(sanitize_profile_name)
        .unwrap_or_default();

    let save_name = manifest
        .as_ref()
        .map(|m| sanitize_profile_name(&m.name))
        .filter(|n| !n.is_empty())
        .unwrap_or_else(|| {
            if stem_name.is_empty() {
                "Imported Save".to_string()
            } else {
                stem_name
            }
        });

    let launcher_config_summary = launcher_cfg.as_ref().map(|cfg| LauncherConfigSummary {
        window_size: cfg.window_size.clone(),
        fullscreen: cfg.fullscreen,
        render_scale: cfg.render_scale,
        rendering_path: cfg.rendering_path.clone(),
        frame_rate_mode: cfg.frame_rate_mode.clone(),
        input_mode: cfg.input_mode.clone(),
        language: cfg.language.clone(),
        audio_language: cfg.audio_language.clone(),
    });

    Ok(SavisticPreview {
        file_path: file_path.to_string_lossy().to_string(),
        file_name,
        save_name,
        exported_at: manifest.as_ref().map(|m| m.exported_at.clone()),
        save_slot_count: detected_dat_count,
        has_achievements: detected_achievements,
        achievement_count: detected_achievement_count,
        has_launcher_config: launcher_cfg.is_some(),
        launcher_config_summary,
    })
}

fn import_savistic_file_inner(
    runtime_root: &Path,
    file_path: &Path,
    custom_name: Option<&str>,
    apply_launcher_config: bool,
    set_active: bool,
    current_settings: &Settings,
) -> Result<(SaveRegistry, SaveProfileRecord, Option<Settings>), String> {
    let preview = inspect_savistic_file_inner(file_path)?;

    let mut registry = load_or_init_registry(runtime_root, Some(&current_settings.active_save_profile))?;

    let new_id = generate_profile_id(&registry.profiles);
    let dest_dir = profile_dir(runtime_root, &new_id)?;
    fs::create_dir_all(&dest_dir)
        .map_err(|e| format!("Failed to create target save profile folder: {}", e))?;

    let file = fs::File::open(file_path)
        .map_err(|e| format!("Failed to open .savistic file: {}", e))?;
    let mut archive = ZipArchive::new(file)
        .map_err(|e| format!("Failed to read .savistic archive: {}", e))?;

    let mut extracted_launcher_cfg: Option<Settings> = None;

    let extract_result: Result<(), String> = (|| {
        for i in 0..archive.len() {
            let mut entry = archive
                .by_index(i)
                .map_err(|e| format!("Failed to read archive entry: {}", e))?;
            let raw_name = entry
            .name()
            .map_err(|e| format!("Invalid archive entry name: {}", e))?
            .into_owned();
            if raw_name.ends_with('/') {
                continue;
            }

            let clean = validate_safe_relative_path(&raw_name)?;
            let normalized = clean
                .components()
                .map(|c| c.as_os_str().to_string_lossy())
                .collect::<Vec<_>>()
                .join("/");
            let lower = normalized.to_ascii_lowercase();

            if lower == "manifest.json" {
                continue;
            }

            if lower == "launcher-config.json" {
                let mut text = String::new();
                if entry.read_to_string(&mut text).is_ok() {
                    if let Ok(parsed) = serde_json::from_str::<Settings>(&text) {
                        extracted_launcher_cfg = Some(parsed);
                    }
                }
                continue;
            }

            // Strip leading "data/" if present (standard .savistic layout)
            let rel_target = if let Ok(stripped) = clean.strip_prefix("data") {
                stripped.to_path_buf()
            } else {
                clean.clone()
            };

            if rel_target.as_os_str().is_empty() {
                continue;
            }

            let target_path = dest_dir.join(&rel_target);
            if let Some(parent) = target_path.parent() {
                fs::create_dir_all(parent)
                    .map_err(|e| format!("Failed to create folder {}: {}", parent.display(), e))?;
            }
            let mut out = fs::File::create(&target_path)
                .map_err(|e| format!("Failed to write {}: {}", target_path.display(), e))?;
            std::io::copy(&mut entry, &mut out)
                .map_err(|e| format!("Failed to extract {}: {}", target_path.display(), e))?;
        }
        Ok(())
    })();

    if let Err(err) = extract_result {
        let _ = fs::remove_dir_all(&dest_dir);
        return Err(err);
    }

    let chosen_name = match custom_name {
        Some(n) => {
            let cleaned = sanitize_profile_name(n);
            if cleaned.is_empty() {
                next_default_profile_name(&registry.profiles)
            } else {
                cleaned
            }
        }
        None => {
            if !preview.save_name.is_empty() {
                preview.save_name.clone()
            } else {
                next_default_profile_name(&registry.profiles)
            }
        }
    };

    let record = SaveProfileRecord {
        id: new_id.clone(),
        name: chosen_name,
        created_at: now_display_timestamp(),
        is_default: false,
    };

    registry.profiles.push(record.clone());
    if set_active {
        registry.active_profile_id = new_id.clone();
    }

    save_registry(runtime_root, &registry)?;

    let updated_settings = if apply_launcher_config {
        if let Some(mut imported_cfg) = extracted_launcher_cfg {
            // Preserve local machine-specific fields
            imported_cfg.game_data_root = current_settings.game_data_root.clone();
            imported_cfg.iso_path = current_settings.iso_path.clone();
            imported_cfg.active_save_profile = registry.active_profile_id.clone();
            imported_cfg.dev_mode = current_settings.dev_mode;
            imported_cfg.configuration = current_settings.configuration.clone();
            Some(imported_cfg)
        } else {
            None
        }
    } else {
        None
    };

    Ok((registry, record, updated_settings))
}

fn open_save_file_dialog(default_filename: &str) -> Result<Option<PathBuf>, String> {
    let script = r#"
        Add-Type -AssemblyName System.Windows.Forms
        $dialog = New-Object System.Windows.Forms.SaveFileDialog
        $dialog.Filter = 'Majistic Save (*.savistic)|*.savistic|All Files (*.*)|*.*'
        $dialog.DefaultExt = 'savistic'
        $dialog.AddExtension = $true
        $dialog.OverwritePrompt = $true
        $dialog.Title = 'Export Majistic Save (.savistic)'
        $dialog.FileName = $env:SAVISTIC_DEFAULT_NAME
        if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
            Write-Output $dialog.FileName
        }
    "#;
    let mut cmd = Command::new("powershell.exe");
    cmd.args(["-NoProfile", "-Command", script]);
    cmd.env("SAVISTIC_DEFAULT_NAME", default_filename);
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        cmd.creation_flags(0x08000000); // CREATE_NO_WINDOW
    }
    let output = cmd
        .output()
        .map_err(|e| format!("Failed to open export file dialog: {}", e))?;

    let path = String::from_utf8_lossy(&output.stdout).trim().to_string();
    if path.is_empty() {
        Ok(None)
    } else {
        Ok(Some(PathBuf::from(path)))
    }
}

fn open_import_file_dialog() -> Result<Option<PathBuf>, String> {
    let script = r#"
        Add-Type -AssemblyName System.Windows.Forms
        $dialog = New-Object System.Windows.Forms.OpenFileDialog
        $dialog.Filter = 'Majistic Save (*.savistic)|*.savistic|All Files (*.*)|*.*'
        $dialog.Title = 'Import Majistic Save (.savistic)'
        if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
            Write-Output $dialog.FileName
        }
    "#;
    let mut cmd = Command::new("powershell.exe");
    cmd.args(["-NoProfile", "-Command", script]);
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        cmd.creation_flags(0x08000000); // CREATE_NO_WINDOW
    }
    let output = cmd
        .output()
        .map_err(|e| format!("Failed to open import file dialog: {}", e))?;

    let path = String::from_utf8_lossy(&output.stdout).trim().to_string();
    if path.is_empty() {
        Ok(None)
    } else {
        Ok(Some(PathBuf::from(path)))
    }
}

// ==========================================
// Tauri Commands
// ==========================================

#[tauri::command]
pub fn list_save_profiles() -> Result<SaveProfilesState, String> {
    let runtime_root = resolve_runtime_root();
    let settings = load_settings().unwrap_or_default();
    let registry = load_or_init_registry(&runtime_root, Some(&settings.active_save_profile))?;
    sync_active_profile_to_settings(&registry.active_profile_id);
    Ok(build_state(&runtime_root, &registry))
}

#[tauri::command]
pub fn create_save_profile(name: Option<String>, set_active: bool) -> Result<SaveProfilesState, String> {
    if set_active {
        ensure_game_not_running("switch the active save")?;
    }
    let runtime_root = resolve_runtime_root();
    let settings = load_settings().unwrap_or_default();
    let mut registry = load_or_init_registry(&runtime_root, Some(&settings.active_save_profile))?;

    let id = generate_profile_id(&registry.profiles);
    let dir = profile_dir(&runtime_root, &id)?;
    fs::create_dir_all(&dir)
        .map_err(|e| format!("Failed to create save directory: {}", e))?;

    let chosen_name = name
        .as_deref()
        .map(sanitize_profile_name)
        .filter(|n| !n.is_empty())
        .unwrap_or_else(|| next_default_profile_name(&registry.profiles));

    registry.profiles.push(SaveProfileRecord {
        id: id.clone(),
        name: chosen_name,
        created_at: now_display_timestamp(),
        is_default: false,
    });

    if set_active {
        registry.active_profile_id = id;
    }

    save_registry(&runtime_root, &registry)?;
    sync_active_profile_to_settings(&registry.active_profile_id);
    Ok(build_state(&runtime_root, &registry))
}

#[tauri::command]
pub fn duplicate_save_profile(
    source_id: String,
    name: Option<String>,
    set_active: bool,
) -> Result<SaveProfilesState, String> {
    ensure_game_not_running("duplicate a save profile")?;
    let runtime_root = resolve_runtime_root();
    let settings = load_settings().unwrap_or_default();
    let mut registry = load_or_init_registry(&runtime_root, Some(&settings.active_save_profile))?;

    let source = registry
        .profiles
        .iter()
        .find(|p| p.id == source_id)
        .cloned()
        .ok_or_else(|| format!("Source save profile '{}' not found.", source_id))?;

    let src_dir = profile_dir(&runtime_root, &source.id)?;
    let new_id = generate_profile_id(&registry.profiles);
    let dst_dir = profile_dir(&runtime_root, &new_id)?;

    copy_dir_recursive(&src_dir, &dst_dir)?;

    let chosen_name = name
        .as_deref()
        .map(sanitize_profile_name)
        .filter(|n| !n.is_empty())
        .unwrap_or_else(|| next_duplicate_profile_name(&source.name, &registry.profiles));

    registry.profiles.push(SaveProfileRecord {
        id: new_id.clone(),
        name: chosen_name,
        created_at: now_display_timestamp(),
        is_default: false,
    });

    if set_active {
        registry.active_profile_id = new_id;
    }

    save_registry(&runtime_root, &registry)?;
    sync_active_profile_to_settings(&registry.active_profile_id);
    Ok(build_state(&runtime_root, &registry))
}

#[tauri::command]
pub fn rename_save_profile(profile_id: String, new_name: String) -> Result<SaveProfilesState, String> {
    let runtime_root = resolve_runtime_root();
    let settings = load_settings().unwrap_or_default();
    let mut registry = load_or_init_registry(&runtime_root, Some(&settings.active_save_profile))?;

    let cleaned = sanitize_profile_name(&new_name);
    let fallback = if profile_id == DEFAULT_PROFILE_ID {
        DEFAULT_PROFILE_NAME.to_string()
    } else {
        let others: Vec<SaveProfileRecord> = registry
            .profiles
            .iter()
            .filter(|p| p.id != profile_id)
            .cloned()
            .collect();
        next_default_profile_name(&others)
    };

    let target = registry
        .profiles
        .iter_mut()
        .find(|p| p.id == profile_id)
        .ok_or_else(|| format!("Save profile '{}' not found.", profile_id))?;

    target.name = if cleaned.is_empty() { fallback } else { cleaned };

    save_registry(&runtime_root, &registry)?;
    Ok(build_state(&runtime_root, &registry))
}

#[tauri::command]
pub fn set_active_save_profile(profile_id: String) -> Result<SaveProfilesState, String> {
    ensure_game_not_running("switch the active save")?;
    let runtime_root = resolve_runtime_root();
    let settings = load_settings().unwrap_or_default();
    let mut registry = load_or_init_registry(&runtime_root, Some(&settings.active_save_profile))?;

    if !registry.profiles.iter().any(|p| p.id == profile_id) {
        return Err(format!("Save profile '{}' not found.", profile_id));
    }

    let dir = profile_dir(&runtime_root, &profile_id)?;
    let _ = fs::create_dir_all(&dir);

    registry.active_profile_id = profile_id;
    save_registry(&runtime_root, &registry)?;
    sync_active_profile_to_settings(&registry.active_profile_id);

    Ok(build_state(&runtime_root, &registry))
}

#[tauri::command]
pub fn delete_save_profile(profile_id: String) -> Result<SaveProfilesState, String> {
    ensure_game_not_running("delete a save profile")?;
    let runtime_root = resolve_runtime_root();
    let settings = load_settings().unwrap_or_default();
    let mut registry = load_or_init_registry(&runtime_root, Some(&settings.active_save_profile))?;

    if registry.profiles.len() <= 1 {
        return Err("Cannot delete the only remaining save profile.".to_string());
    }

    let idx = registry
        .profiles
        .iter()
        .position(|p| p.id == profile_id)
        .ok_or_else(|| format!("Save profile '{}' not found.", profile_id))?;

    let dir = profile_dir(&runtime_root, &profile_id)?;
    if dir.exists() {
        fs::remove_dir_all(&dir)
            .map_err(|e| format!("Failed to delete save files on disk: {}", e))?;
    }

    registry.profiles.remove(idx);

    if registry.active_profile_id == profile_id {
        registry.active_profile_id = registry.profiles[0].id.clone();
    }

    save_registry(&runtime_root, &registry)?;
    sync_active_profile_to_settings(&registry.active_profile_id);
    Ok(build_state(&runtime_root, &registry))
}

#[tauri::command]
pub fn export_save_profile(profile_id: String) -> Result<Option<String>, String> {
    ensure_game_not_running("export a save profile")?;
    let runtime_root = resolve_runtime_root();
    let settings = load_settings().unwrap_or_default();
    let registry = load_or_init_registry(&runtime_root, Some(&settings.active_save_profile))?;

    let profile = registry
        .profiles
        .iter()
        .find(|p| p.id == profile_id)
        .cloned()
        .ok_or_else(|| format!("Save profile '{}' not found.", profile_id))?;

    let default_filename = format!("{}.savistic", sanitize_filename_stem(&profile.name));
    let Some(mut dest_path) = open_save_file_dialog(&default_filename)? else {
        return Ok(None);
    };

    if dest_path.extension().is_none() {
        dest_path.set_extension("savistic");
    }

    export_profile_to_file(&runtime_root, &profile, &settings, &dest_path)?;
    Ok(Some(dest_path.to_string_lossy().to_string()))
}

#[tauri::command]
pub fn pick_and_inspect_savistic() -> Result<Option<SavisticPreview>, String> {
    let Some(path) = open_import_file_dialog()? else {
        return Ok(None);
    };
    let preview = inspect_savistic_file_inner(&path)?;
    Ok(Some(preview))
}

#[tauri::command]
pub fn import_save_profile(
    file_path: String,
    custom_name: Option<String>,
    apply_launcher_config: bool,
    set_active: bool,
) -> Result<ImportSaveResult, String> {
    if set_active {
        ensure_game_not_running("activate the imported save")?;
    }

    let runtime_root = resolve_runtime_root();
    let current_settings = load_settings().unwrap_or_default();
    let path = PathBuf::from(&file_path);

    let (registry, record, updated_settings) = import_savistic_file_inner(
        &runtime_root,
        &path,
        custom_name.as_deref(),
        apply_launcher_config,
        set_active,
        &current_settings,
    )?;

    let mut settings_updated = false;
    if let Some(ref new_settings) = updated_settings {
        save_settings(new_settings.clone())?;
        settings_updated = true;
    } else if set_active {
        sync_active_profile_to_settings(&registry.active_profile_id);
    }

    Ok(ImportSaveResult {
        state: build_state(&runtime_root, &registry),
        imported_profile_id: record.id,
        imported_profile_name: record.name,
        settings_updated,
        settings: updated_settings,
    })
}

#[tauri::command]
pub fn open_saves_folder(profile_id: Option<String>) -> Result<(), String> {
    let runtime_root = resolve_runtime_root();
    let target = match profile_id {
        Some(ref id) if !id.trim().is_empty() => profile_dir(&runtime_root, id)?,
        _ => {
            let settings = load_settings().unwrap_or_default();
            let registry = load_or_init_registry(&runtime_root, Some(&settings.active_save_profile))?;
            profile_dir(&runtime_root, &registry.active_profile_id)?
        }
    };

    fs::create_dir_all(&target)
        .map_err(|e| format!("Failed to create folder {}: {}", target.display(), e))?;

    #[cfg(windows)]
    {
        Command::new("explorer.exe")
            .arg(&target)
            .spawn()
            .map_err(|e| format!("Failed to open Explorer: {}", e))?;
    }
    #[cfg(not(windows))]
    {
        Command::new("xdg-open")
            .arg(&target)
            .spawn()
            .map_err(|e| format!("Failed to open folder: {}", e))?;
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp_test_dir(tag: &str) -> PathBuf {
        let nanos = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let dir = std::env::temp_dir().join(format!("majistic_savistic_test_{}_{}", tag, nanos));
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    #[test]
    fn test_savistic_roundtrip_with_and_without_launcher_config() {
        let runtime = temp_test_dir("roundtrip");
        let reg = load_or_init_registry(&runtime, None).unwrap();
        assert_eq!(reg.active_profile_id, DEFAULT_PROFILE_ID);
        assert_eq!(reg.profiles.len(), 1);

        // Populate fake save & achievements in default profile
        let default_dir = profile_dir(&runtime, DEFAULT_PROFILE_ID).unwrap();
        let slot_dir = default_dir.join("B13EBABEBABEBABE/4E4D0816/00000001/KB_SAVE_0");
        let header_dir = default_dir.join("B13EBABEBABEBABE/4E4D0816/Headers/00000001");
        let ach_dir = default_dir.join("achievements");
        fs::create_dir_all(&slot_dir).unwrap();
        fs::create_dir_all(&header_dir).unwrap();
        fs::create_dir_all(&ach_dir).unwrap();
        fs::write(slot_dir.join("save00.dat"), vec![0xAA; 128]).unwrap();
        fs::write(slot_dir.join("save01.dat"), vec![0xBB; 128]).unwrap();
        fs::write(header_dir.join("KB_SAVE_0.header"), vec![0xCC; 32]).unwrap();
        fs::write(
            ach_dir.join("4E4D0816.toml"),
            "[unlocked.1]\nfiletime = 1\n\n[unlocked.2]\nfiletime = 2\n",
        )
        .unwrap();

        let mut export_settings = Settings::default();
        export_settings.window_size = "1920x1080".into();
        export_settings.render_scale = 2;
        export_settings.frame_rate_mode = "60".into();
        export_settings.game_data_root = "C:/OtherPC/Majin/game-data".into();
        export_settings.dev_mode = true;

        let savistic_path = runtime.join("BossCheckpoint.savistic");
        export_profile_to_file(&runtime, &reg.profiles[0], &export_settings, &savistic_path).unwrap();

        // Inspect archive
        let preview = inspect_savistic_file_inner(&savistic_path).unwrap();
        assert_eq!(preview.save_name, DEFAULT_PROFILE_NAME);
        assert_eq!(preview.save_slot_count, 2);
        assert!(preview.has_achievements);
        assert_eq!(preview.achievement_count, 2);
        assert!(preview.has_launcher_config);
        let summary = preview.launcher_config_summary.unwrap();
        assert_eq!(summary.window_size, "1920x1080");
        assert_eq!(summary.render_scale, 2);
        assert_eq!(summary.frame_rate_mode, "60");

        // Import WITHOUT applying launcher config, with blank custom name -> auto default name
        let mut local_settings = Settings::default();
        local_settings.game_data_root = "my-local-game-data".into();
        local_settings.dev_mode = false;

        let (reg2, imported1, cfg1) = import_savistic_file_inner(
            &runtime,
            &savistic_path,
            Some("   "),
            false,
            false,
            &local_settings,
        )
        .unwrap();
        assert_eq!(imported1.name, "Save 1");
        assert!(cfg1.is_none());
        assert_eq!(reg2.active_profile_id, DEFAULT_PROFILE_ID);

        let imported1_dir = profile_dir(&runtime, &imported1.id).unwrap();
        assert!(imported1_dir
            .join("B13EBABEBABEBABE/4E4D0816/00000001/KB_SAVE_0/save00.dat")
            .exists());
        assert!(imported1_dir.join("achievements/4E4D0816.toml").exists());

        // Import WITH applying launcher config + custom name + set_active
        let (reg3, imported2, cfg2) = import_savistic_file_inner(
            &runtime,
            &savistic_path,
            Some("Player 2 - Boss"),
            true,
            true,
            &local_settings,
        )
        .unwrap();
        assert_eq!(imported2.name, "Player 2 - Boss");
        assert_eq!(reg3.active_profile_id, imported2.id);
        let applied = cfg2.unwrap();
        assert_eq!(applied.window_size, "1920x1080");
        assert_eq!(applied.render_scale, 2);
        // Machine-specific fields preserved!
        assert_eq!(applied.game_data_root, "my-local-game-data");
        assert!(!applied.dev_mode);
        assert_eq!(applied.active_save_profile, imported2.id);

        let _ = fs::remove_dir_all(&runtime);
    }

    #[test]
    fn test_zip_slip_rejected() {
        let dir = temp_test_dir("zipslip");
        let bad_zip = dir.join("evil.savistic");
        {
            let f = fs::File::create(&bad_zip).unwrap();
            let mut writer = ZipWriter::new(f);
            let opts = SimpleFileOptions::default().compression_method(CompressionMethod::Deflated);
            writer.start_file("../escape.txt", opts).unwrap();
            writer.write_all(b"evil").unwrap();
            writer.finish().unwrap();
        }
        let res = inspect_savistic_file_inner(&bad_zip);
        assert!(res.is_err());
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_default_profile_lives_in_saves_folder_from_start() {
        let runtime = temp_test_dir("default_in_saves");
        let reg = load_or_init_registry(&runtime, None).unwrap();
        assert_eq!(reg.active_profile_id, DEFAULT_PROFILE_ID);

        let default_dir = profile_dir(&runtime, DEFAULT_PROFILE_ID).unwrap();
        assert_eq!(default_dir, runtime.join("saves").join(DEFAULT_PROFILE_ID));
        assert!(default_dir.exists());
        assert!(!runtime.join("user").exists());

        let _ = fs::remove_dir_all(&runtime);
    }

    #[test]
    fn test_legacy_user_dir_migrates_to_saves_default() {
        let runtime = temp_test_dir("legacy_migrate");
        let legacy_slot = runtime.join("user/B13EBABEBABEBABE/4E4D0816/00000001/KB_SAVE_0");
        fs::create_dir_all(&legacy_slot).unwrap();
        fs::write(legacy_slot.join("save00.dat"), vec![0x42; 64]).unwrap();

        migrate_legacy_default_profile_inner(&runtime);
        let reg = load_or_init_registry(&runtime, None).unwrap();
        assert_eq!(reg.active_profile_id, DEFAULT_PROFILE_ID);

        let migrated_file = runtime
            .join("saves")
            .join(DEFAULT_PROFILE_ID)
            .join("B13EBABEBABEBABE/4E4D0816/00000001/KB_SAVE_0/save00.dat");
        assert!(migrated_file.exists());
        assert!(!runtime.join("user").exists());

        let _ = fs::remove_dir_all(&runtime);
    }
}
