use std::collections::HashSet;
use std::fs;
use std::path::{Path, PathBuf};

use chrono::Local;
use serde::{Deserialize, Serialize};

use crate::saves::{
    is_majin_game_running, SaveProfileRecord, SaveRegistry, DEFAULT_PROFILE_ID,
    DEFAULT_PROFILE_NAME,
};
use crate::{get_project_root, Settings};

pub const CLEAN_UPDATE_NOTICE_REL: &str = "private/.clean-update-notice.json";
const LEGACY_MIGRATED_PROFILE_ID: &str = "legacy_migrated";
const LEGACY_MIGRATED_PROFILE_NAME: &str = "Legacy Save (Migrated)";

/// Directories owned by Majistic Recomp that are wiped during a clean reinstall update.
/// Unlisted user files/folders sitting next to the launcher (e.g. a user's .iso file)
/// are intentionally left untouched.
const MANAGED_DIRS_TO_CLEAN: &[&str] = &[
    "game-data",
    "game-data-usa",
    "scripts",
    "prototype",
    "prototype-usa",
    "tools",
    "src",
    "branding",
    "LICENSES",
    "launcher",
    "reports",
    "private",
];

const MANAGED_ROOT_FILES_TO_CLEAN: &[&str] = &[
    "LICENSE",
    "README.md",
    "launcher-debug.log",
    "launcher-chromium.log",
];

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CleanUpdateNotice {
    pub version: String,
    pub saves_migrated: bool,
    pub migrated_profile_count: usize,
    pub migrated_legacy_user: bool,
    pub preserved_iso_path: String,
    pub timestamp: String,
}

#[derive(Debug, Clone)]
pub struct CleanUpdateSummary {
    pub notice: CleanUpdateNotice,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "PascalCase")]
struct OldSettingsProbe {
    #[serde(default)]
    game_data_root: Option<String>,
    #[serde(default)]
    iso_path: Option<String>,
    #[serde(default)]
    active_save_profile: Option<String>,
}

struct SaveBackupManifest {
    backup_root: PathBuf,
    has_modern_saves: bool,
    legacy_dirs: Vec<PathBuf>,
    preserved_iso_path: String,
    preferred_active_profile: Option<String>,
}

fn now_display_timestamp() -> String {
    Local::now().format("%Y-%m-%d %H:%M").to_string()
}

fn now_iso_timestamp() -> String {
    Local::now().format("%Y-%m-%d %H:%M:%S").to_string()
}

fn dir_has_entries(dir: &Path) -> bool {
    fs::read_dir(dir)
        .ok()
        .and_then(|mut it| it.next())
        .is_some()
}

fn copy_dir_recursive_strict(src: &Path, dst: &Path) -> Result<(), String> {
    fs::create_dir_all(dst)
        .map_err(|e| format!("Failed to create directory '{}': {}", dst.display(), e))?;

    for entry in fs::read_dir(src)
        .map_err(|e| format!("Failed to read directory '{}': {}", src.display(), e))?
    {
        let entry = entry.map_err(|e| format!("Directory entry error in '{}': {}", src.display(), e))?;
        let ft = entry
            .file_type()
            .map_err(|e| format!("Failed to inspect '{}': {}", entry.path().display(), e))?;
        let src_path = entry.path();
        let dst_path = dst.join(entry.file_name());

        if ft.is_dir() {
            copy_dir_recursive_strict(&src_path, &dst_path)?;
        } else if ft.is_file() {
            fs::copy(&src_path, &dst_path).map_err(|e| {
                format!(
                    "Failed to copy '{}' -> '{}': {}",
                    src_path.display(),
                    dst_path.display(),
                    e
                )
            })?;
        }
    }
    Ok(())
}

fn remove_dir_if_exists(path: &Path) -> Result<(), String> {
    if !path.exists() {
        return Ok(());
    }
    for attempt in 1..=5 {
        match fs::remove_dir_all(path) {
            Ok(()) => return Ok(()),
            Err(e) if attempt < 5 => {
                std::thread::sleep(std::time::Duration::from_millis(150));
                let _ = e;
            }
            Err(e) => {
                return Err(format!(
                    "Failed to remove old directory '{}': {}",
                    path.display(),
                    e
                ));
            }
        }
    }
    Ok(())
}

fn paths_overlap(a: &Path, b: &Path) -> bool {
    let ca = fs::canonicalize(a).unwrap_or_else(|_| a.to_path_buf());
    let cb = fs::canonicalize(b).unwrap_or_else(|_| b.to_path_buf());
    ca == cb || ca.starts_with(&cb) || cb.starts_with(&ca)
}

/// Verifies `target_dir` is genuinely an existing Majistic Recomp installation directory
/// so we never risk deleting files in an unrelated working directory (e.g. Desktop or System32).
pub fn validate_target_install_dir(
    temp_dir: &Path,
    target_dir: &Path,
    exe_file_name: &str,
) -> Result<(), String> {
    if exe_file_name.trim().is_empty() {
        return Err("Cannot determine launcher executable filename.".to_string());
    }
    if !target_dir.is_dir() {
        return Err(format!(
            "Target installation directory does not exist: {}",
            target_dir.display()
        ));
    }
    if paths_overlap(temp_dir, target_dir) {
        return Err(format!(
            "Updater source ({}) overlaps with target directory ({}).",
            temp_dir.display(),
            target_dir.display()
        ));
    }

    let has_exe = target_dir.join(exe_file_name).is_file();
    let has_project_marker = target_dir.join("scripts/run-usa.ps1").is_file()
        || target_dir.join("game-data").is_dir()
        || target_dir.join("game-data-usa").is_dir()
        || target_dir.join("prototype-usa").is_dir()
        || target_dir.join("prototype").is_dir();

    if !has_exe || !has_project_marker {
        return Err(format!(
            "Directory '{}' does not look like a valid Majistic Recomp installation (missing '{}' or installation folders). Aborting update to protect your files.",
            target_dir.display(),
            exe_file_name
        ));
    }

    Ok(())
}

fn read_old_settings_probe(target_dir: &Path) -> Option<OldSettingsProbe> {
    let settings_path = target_dir.join("private/launcher-usa.json");
    let raw = fs::read_to_string(&settings_path).ok()?;
    serde_json::from_str::<OldSettingsProbe>(&raw).ok()
}

fn collect_candidate_runtime_roots(
    target_dir: &Path,
    probe: Option<&OldSettingsProbe>,
) -> Vec<PathBuf> {
    let mut raw_candidates: Vec<PathBuf> = Vec::new();

    if let Some(p) = probe {
        if let Some(ref gdr) = p.game_data_root {
            let trimmed = gdr.trim();
            if !trimmed.is_empty() {
                let gdr_path = if Path::new(trimmed).is_absolute() {
                    PathBuf::from(trimmed)
                } else {
                    target_dir.join(trimmed)
                };
                raw_candidates.push(gdr_path.join("runtime"));
            }
        }
    }

    raw_candidates.push(target_dir.join("game-data/runtime"));
    raw_candidates.push(target_dir.join("game-data-usa/runtime"));
    raw_candidates.push(target_dir.join("private/runtime-usa"));

    let mut seen = HashSet::new();
    let mut unique = Vec::new();
    for cand in raw_candidates {
        if !cand.is_dir() {
            continue;
        }
        let key = fs::canonicalize(&cand)
            .unwrap_or_else(|_| cand.clone())
            .to_string_lossy()
            .to_ascii_lowercase();
        if seen.insert(key) {
            unique.push(cand);
        }
    }
    unique
}

fn backup_existing_saves(
    target_dir: &Path,
    log: &dyn Fn(&str),
) -> Result<SaveBackupManifest, String> {
    let probe = read_old_settings_probe(target_dir);

    let preserved_iso_path = probe
        .as_ref()
        .and_then(|p| p.iso_path.clone())
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty() && Path::new(s).is_file())
        .unwrap_or_default();

    let preferred_active_profile = probe
        .as_ref()
        .and_then(|p| p.active_save_profile.clone())
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty());

    let backup_root = target_dir.join(format!(".majistic-update-backup-{}", std::process::id()));
    if backup_root.exists() {
        remove_dir_if_exists(&backup_root)?;
    }
    fs::create_dir_all(&backup_root)
        .map_err(|e| format!("Failed to create save backup directory: {}", e))?;

    let runtime_roots = collect_candidate_runtime_roots(target_dir, probe.as_ref());
    let mut has_modern_saves = false;
    let mut legacy_dirs: Vec<PathBuf> = Vec::new();

    for runtime_root in &runtime_roots {
        log(&format!(
            "Scanning '{}' for existing saves...",
            runtime_root.display()
        ));

        let modern_saves = runtime_root.join("saves");
        if modern_saves.is_dir() && dir_has_entries(&modern_saves) {
            let dst_saves = backup_root.join("saves");
            if !has_modern_saves {
                log(&format!(
                    "Backing up Save Profiles from '{}'",
                    modern_saves.display()
                ));
                copy_dir_recursive_strict(&modern_saves, &dst_saves)?;
                has_modern_saves = true;
            } else {
                // Additional secondary saves dir: preserve any extra profiles under legacy buckets
                if let Ok(entries) = fs::read_dir(&modern_saves) {
                    for entry in entries.flatten() {
                        let p = entry.path();
                        if p.is_dir() && dir_has_entries(&p) {
                            let idx = legacy_dirs.len();
                            let bucket = backup_root.join(format!("legacy_user_{}", idx));
                            copy_dir_recursive_strict(&p, &bucket)?;
                            legacy_dirs.push(bucket);
                        }
                    }
                }
            }
        }

        let legacy_user = runtime_root.join("user");
        if legacy_user.is_dir() && dir_has_entries(&legacy_user) {
            let idx = legacy_dirs.len();
            let bucket = backup_root.join(format!("legacy_user_{}", idx));
            log(&format!(
                "Backing up legacy save folder '{}' -> '{}'",
                legacy_user.display(),
                bucket.display()
            ));
            copy_dir_recursive_strict(&legacy_user, &bucket)?;
            legacy_dirs.push(bucket);
        }
    }

    Ok(SaveBackupManifest {
        backup_root,
        has_modern_saves,
        legacy_dirs,
        preserved_iso_path,
        preferred_active_profile,
    })
}

fn is_valid_id(id: &str) -> bool {
    !id.is_empty()
        && id.len() <= 64
        && id
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '-')
}

fn load_or_reconstruct_registry(saves_dir: &Path) -> SaveRegistry {
    let reg_path = saves_dir.join("profiles.json");
    if let Ok(raw) = fs::read_to_string(&reg_path) {
        if let Ok(mut reg) = serde_json::from_str::<SaveRegistry>(&raw) {
            let mut seen = HashSet::new();
            reg.profiles
                .retain(|p| is_valid_id(&p.id) && seen.insert(p.id.clone()));
            if !reg.profiles.is_empty() {
                if !reg.profiles.iter().any(|p| p.id == reg.active_profile_id) {
                    reg.active_profile_id = reg.profiles[0].id.clone();
                }
                return reg;
            }
        }
    }

    // Reconstruct from existing subdirectories if profiles.json was absent or unreadable
    let mut profiles: Vec<SaveProfileRecord> = Vec::new();
    let ts = now_display_timestamp();

    if saves_dir.join(DEFAULT_PROFILE_ID).is_dir() {
        profiles.push(SaveProfileRecord {
            id: DEFAULT_PROFILE_ID.to_string(),
            name: DEFAULT_PROFILE_NAME.to_string(),
            created_at: ts.clone(),
            is_default: true,
        });
    }

    if let Ok(entries) = fs::read_dir(saves_dir) {
        let mut extra_ids: Vec<String> = entries
            .flatten()
            .filter_map(|e| {
                let p = e.path();
                if !p.is_dir() {
                    return None;
                }
                let id = e.file_name().to_string_lossy().to_string();
                if is_valid_id(&id) && id != DEFAULT_PROFILE_ID {
                    Some(id)
                } else {
                    None
                }
            })
            .collect();
        extra_ids.sort();
        for (i, id) in extra_ids.into_iter().enumerate() {
            profiles.push(SaveProfileRecord {
                id,
                name: format!("Restored Save {}", i + 1),
                created_at: ts.clone(),
                is_default: false,
            });
        }
    }

    if profiles.is_empty() {
        profiles.push(SaveProfileRecord {
            id: DEFAULT_PROFILE_ID.to_string(),
            name: DEFAULT_PROFILE_NAME.to_string(),
            created_at: ts,
            is_default: true,
        });
    }

    let active = profiles[0].id.clone();
    SaveRegistry {
        active_profile_id: active,
        profiles,
    }
}

fn restore_and_migrate_saves(
    target_dir: &Path,
    backup: &SaveBackupManifest,
    version: &str,
    log: &dyn Fn(&str),
) -> Result<CleanUpdateNotice, String> {
    let new_saves_dir = target_dir.join("game-data/runtime/saves");
    fs::create_dir_all(&new_saves_dir)
        .map_err(|e| format!("Failed to create new saves directory: {}", e))?;

    let mut saves_migrated = false;
    let mut migrated_legacy_user = false;

    if backup.has_modern_saves {
        let src_saves = backup.backup_root.join("saves");
        if src_saves.is_dir() {
            log("Restoring modern Save Profiles into game-data/runtime/saves...");
            copy_dir_recursive_strict(&src_saves, &new_saves_dir)?;
            saves_migrated = true;
        }
    }

    let mut registry = load_or_reconstruct_registry(&new_saves_dir);

    for (idx, legacy_src) in backup.legacy_dirs.iter().enumerate() {
        if !legacy_src.is_dir() || !dir_has_entries(legacy_src) {
            continue;
        }

        let default_dir = new_saves_dir.join(DEFAULT_PROFILE_ID);
        let default_is_empty = !default_dir.exists() || !dir_has_entries(&default_dir);

        if default_is_empty {
            log(&format!(
                "Migrating legacy save '{}' -> '{}'",
                legacy_src.display(),
                default_dir.display()
            ));
            copy_dir_recursive_strict(legacy_src, &default_dir)?;
            if !registry.profiles.iter().any(|p| p.id == DEFAULT_PROFILE_ID) {
                registry.profiles.insert(
                    0,
                    SaveProfileRecord {
                        id: DEFAULT_PROFILE_ID.to_string(),
                        name: DEFAULT_PROFILE_NAME.to_string(),
                        created_at: now_display_timestamp(),
                        is_default: true,
                    },
                );
            }
            if !backup.has_modern_saves {
                registry.active_profile_id = DEFAULT_PROFILE_ID.to_string();
            }
        } else {
            // Default already has save data: preserve legacy save as a separate profile so nothing is lost
            let mut candidate_id = if idx == 0 {
                LEGACY_MIGRATED_PROFILE_ID.to_string()
            } else {
                format!("{}_{}", LEGACY_MIGRATED_PROFILE_ID, idx + 1)
            };
            let mut suffix = 2u32;
            while registry.profiles.iter().any(|p| p.id == candidate_id)
                || new_saves_dir.join(&candidate_id).exists()
            {
                candidate_id = format!("{}_{}", LEGACY_MIGRATED_PROFILE_ID, suffix);
                suffix += 1;
            }
            let candidate_name = if idx == 0 && !registry.profiles.iter().any(|p| p.name == LEGACY_MIGRATED_PROFILE_NAME) {
                LEGACY_MIGRATED_PROFILE_NAME.to_string()
            } else {
                format!("{} {}", LEGACY_MIGRATED_PROFILE_NAME, idx + 1)
            };

            let dst_dir = new_saves_dir.join(&candidate_id);
            log(&format!(
                "Default profile already populated; preserving additional legacy save as '{}'",
                candidate_id
            ));
            copy_dir_recursive_strict(legacy_src, &dst_dir)?;
            registry.profiles.push(SaveProfileRecord {
                id: candidate_id,
                name: candidate_name,
                created_at: now_display_timestamp(),
                is_default: false,
            });
        }

        saves_migrated = true;
        migrated_legacy_user = true;
    }

    if let Some(ref pref) = backup.preferred_active_profile {
        if registry.profiles.iter().any(|p| p.id == *pref) {
            registry.active_profile_id = pref.clone();
        }
    }

    // Ensure default profile directory exists under runtime/saves/default
    if registry.profiles.iter().any(|p| p.id == DEFAULT_PROFILE_ID) {
        let _ = fs::create_dir_all(new_saves_dir.join(DEFAULT_PROFILE_ID));
    }

    // Persist clean profiles.json
    let reg_json = serde_json::to_string_pretty(&registry)
        .map_err(|e| format!("Failed to serialize migrated save registry: {}", e))?;
    fs::write(new_saves_dir.join("profiles.json"), reg_json)
        .map_err(|e| format!("Failed to write migrated profiles.json: {}", e))?;

    // Write clean default launcher settings carrying over active save profile & valid ISO path
    let mut fresh_settings = Settings::default();
    fresh_settings.active_save_profile = registry.active_profile_id.clone();
    if !backup.preserved_iso_path.is_empty() {
        fresh_settings.iso_path = backup.preserved_iso_path.clone();
    }
    let private_dir = target_dir.join("private");
    fs::create_dir_all(&private_dir)
        .map_err(|e| format!("Failed to create private directory: {}", e))?;
    let settings_json = serde_json::to_string_pretty(&fresh_settings)
        .map_err(|e| format!("Failed to serialize fresh launcher settings: {}", e))?;
    fs::write(private_dir.join("launcher-usa.json"), settings_json)
        .map_err(|e| format!("Failed to write fresh launcher-usa.json: {}", e))?;

    // Count profiles that actually have data (or total profiles if modern saves were restored)
    let populated_count = registry
        .profiles
        .iter()
        .filter(|p| {
            let d = new_saves_dir.join(&p.id);
            d.is_dir() && dir_has_entries(&d)
        })
        .count();

    let notice = CleanUpdateNotice {
        version: version.to_string(),
        saves_migrated,
        migrated_profile_count: if saves_migrated {
            populated_count.max(1)
        } else {
            0
        },
        migrated_legacy_user,
        preserved_iso_path: backup.preserved_iso_path.clone(),
        timestamp: now_iso_timestamp(),
    };

    let notice_json = serde_json::to_string_pretty(&notice)
        .map_err(|e| format!("Failed to serialize clean update notice: {}", e))?;
    fs::write(target_dir.join(CLEAN_UPDATE_NOTICE_REL), notice_json)
        .map_err(|e| format!("Failed to write clean update notice: {}", e))?;

    Ok(notice)
}

/// Core testable clean-reinstall update pipeline:
/// 1. Validates `target_dir` is a genuine Majistic Recomp install.
/// 2. Backs up all existing saves (modern `runtime/saves/` & legacy `runtime/user/`).
/// 3. Replaces the launcher executable (when `replace_exe` is true).
/// 4. Cleanly wipes managed installation directories (preserving user files like `.iso`).
/// 5. Dynamically copies every folder/file from `temp_dir` into `target_dir` (including `launcher/`).
/// 6. Restores & migrates all saves into `game-data/runtime/saves/` and writes a post-update notice.
pub fn perform_clean_update_core(
    temp_dir: &Path,
    target_dir: &Path,
    current_exe: &Path,
    version: &str,
    replace_exe: bool,
    log: &dyn Fn(&str),
) -> Result<CleanUpdateSummary, String> {
    let exe_name = current_exe
        .file_name()
        .and_then(|n| n.to_str())
        .ok_or_else(|| "Invalid launcher executable filename.".to_string())?;

    validate_target_install_dir(temp_dir, target_dir, exe_name)?;

    // Step 1: Rescue all existing saves BEFORE deleting a single file
    log("Step 1/5: Backing up existing save files...");
    let backup = backup_existing_saves(target_dir, log)?;

    // Step 2: Replace target executable first (ensures old process has released lock)
    if replace_exe {
        log("Step 2/5: Replacing launcher executable...");
        let target_exe = target_dir.join(exe_name);
        let mut replaced = false;
        let mut last_err = String::new();
        for attempt in 1..=35 {
            std::thread::sleep(std::time::Duration::from_millis(200));
            match fs::copy(current_exe, &target_exe) {
                Ok(_) => {
                    log(&format!(
                        "Replaced '{}' on attempt {}",
                        target_exe.display(),
                        attempt
                    ));
                    replaced = true;
                    break;
                }
                Err(e) => {
                    last_err = e.to_string();
                    if attempt % 5 == 0 {
                        log(&format!(
                            "Waiting for previous launcher process to exit (attempt {}): {}",
                            attempt, last_err
                        ));
                    }
                }
            }
        }
        if !replaced {
            let _ = remove_dir_if_exists(&backup.backup_root);
            return Err(format!(
                "Could not replace '{}' because it is still in use: {}",
                target_exe.display(),
                last_err
            ));
        }
    }

    // Step 3: Wipe managed old installation directories cleanly
    log("Step 3/5: Cleaning previous installation directories...");
    for dir_name in MANAGED_DIRS_TO_CLEAN {
        let dir_path = target_dir.join(dir_name);
        if dir_path.exists() {
            log(&format!("Removing old folder '{}'...", dir_name));
            remove_dir_if_exists(&dir_path)?;
        }
    }
    for file_name in MANAGED_ROOT_FILES_TO_CLEAN {
        let file_path = target_dir.join(file_name);
        if file_path.is_file() {
            let _ = fs::remove_file(&file_path);
        }
    }

    // Step 4: Dynamically copy ALL folders and files from temp_dir (never misses new folders like `launcher/`)
    log("Step 4/5: Deploying new release files...");
    for entry in fs::read_dir(temp_dir)
        .map_err(|e| format!("Failed to read update payload '{}': {}", temp_dir.display(), e))?
    {
        let entry = entry.map_err(|e| e.to_string())?;
        let name = entry.file_name();
        let name_str = name.to_string_lossy();
        if name_str.eq_ignore_ascii_case(exe_name) {
            continue;
        }
        let src_path = entry.path();
        let dst_path = target_dir.join(&name);
        let ft = entry.file_type().map_err(|e| e.to_string())?;

        if ft.is_dir() {
            log(&format!("Deploying folder '{}'...", name_str));
            copy_dir_recursive_strict(&src_path, &dst_path)?;
        } else if ft.is_file() {
            fs::copy(&src_path, &dst_path).map_err(|e| {
                format!(
                    "Failed to deploy file '{}' -> '{}': {}",
                    src_path.display(),
                    dst_path.display(),
                    e
                )
            })?;
        }
    }

    // Step 5: Restore and migrate saves into game-data/runtime/saves/
    log("Step 5/5: Restoring and migrating saves into game-data/runtime/saves...");
    let notice = match restore_and_migrate_saves(target_dir, &backup, version, log) {
        Ok(n) => n,
        Err(e) => {
            return Err(format!(
                "{}\n\nYour save backup was preserved safely at:\n{}",
                e,
                backup.backup_root.display()
            ));
        }
    };

    // Only delete the temporary backup directory AFTER saves were verified restored
    if let Err(e) = remove_dir_if_exists(&backup.backup_root) {
        log(&format!(
            "Warning: could not remove temporary save backup dir: {}",
            e
        ));
    }

    Ok(CleanUpdateSummary { notice })
}

#[cfg(windows)]
fn to_wide_null(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

#[cfg(windows)]
fn show_windows_message_box(title: &str, body: &str, flags: u32) -> i32 {
    use windows_sys::Win32::UI::WindowsAndMessaging::MessageBoxW;
    let w_title = to_wide_null(title);
    let w_body = to_wide_null(body);
    unsafe { MessageBoxW(std::ptr::null_mut(), w_body.as_ptr(), w_title.as_ptr(), flags) }
}

#[cfg(windows)]
fn prompt_confirm_clean_update(version: &str) -> bool {
    use windows_sys::Win32::UI::WindowsAndMessaging::{
        IDYES, MB_ICONWARNING, MB_SETFOREGROUND, MB_TOPMOST, MB_YESNO,
    };
    let title = format!("Majistic Recomp v{} — Clean Update Required", version);
    let body = format!(
        "Updating to Majistic Recomp v{version} will perform a clean installation.\n\n\
        • Your existing save files and achievements will be backed up and migrated automatically to the new Save Profiles folder (game-data\\runtime\\saves).\n\
        • Previous installation files and old shader caches will be replaced cleanly.\n\
        • After updating, you will need your Xbox 360 game ISO file again in Setup Game to re-extract assets and rebuild the shader cache.\n\n\
        Do you have your game ISO available and want to continue?\n\
        (Choose 'No' to cancel and keep your current version unchanged.)"
    );
    show_windows_message_box(
        &title,
        &body,
        MB_YESNO | MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST,
    ) == IDYES
}

#[cfg(not(windows))]
fn prompt_confirm_clean_update(_version: &str) -> bool {
    true
}

#[cfg(windows)]
fn prompt_close_running_game() -> bool {
    use windows_sys::Win32::UI::WindowsAndMessaging::{
        IDRETRY, MB_ICONWARNING, MB_RETRYCANCEL, MB_SETFOREGROUND, MB_TOPMOST,
    };
    show_windows_message_box(
        "Majistic Recomp — Game Running",
        "Majin (majin.exe) is currently running.\n\nPlease close the game before updating, then click Retry (or Cancel to abort the update safely).",
        MB_RETRYCANCEL | MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST,
    ) == IDRETRY
}

#[cfg(not(windows))]
fn prompt_close_running_game() -> bool {
    false
}

#[cfg(windows)]
fn show_update_error_dialog(message: &str) {
    use windows_sys::Win32::UI::WindowsAndMessaging::{
        MB_ICONERROR, MB_OK, MB_SETFOREGROUND, MB_TOPMOST,
    };
    show_windows_message_box(
        "Majistic Recomp — Update Cancelled",
        message,
        MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST,
    );
}

#[cfg(not(windows))]
fn show_update_error_dialog(_message: &str) {}

/// Entrypoint invoked from `main.rs` when launched by `tauri-plugin-updater` with `/UPDATE`.
/// Returns `true` if `/UPDATE` mode was handled (normally terminates via `std::process::exit(0)`).
pub fn handle_update_mode(log: &dyn Fn(&str)) -> bool {
    let args: Vec<String> = std::env::args().collect();
    if !args.iter().any(|a| a.eq_ignore_ascii_case("/UPDATE")) {
        return false;
    }

    let version = env!("CARGO_PKG_VERSION");
    log(&format!(
        "Clean update mode (/UPDATE) entered for Majistic Launcher v{}.",
        version
    ));

    let current_exe = match std::env::current_exe() {
        Ok(exe) => exe,
        Err(e) => {
            log(&format!("Failed to get current_exe: {}", e));
            return false;
        }
    };
    let temp_dir = match current_exe.parent() {
        Some(p) => p.to_path_buf(),
        None => return false,
    };
    let target_dir = match std::env::current_dir() {
        Ok(dir) => dir,
        Err(e) => {
            log(&format!("Failed to get current_dir: {}", e));
            return false;
        }
    };
    let exe_name = current_exe
        .file_name()
        .and_then(|n| n.to_str())
        .unwrap_or("Majistic Launcher.exe");

    log(&format!("Updater temp_dir: {:?}", temp_dir));
    log(&format!("Updater target_dir: {:?}", target_dir));

    if let Err(err) = validate_target_install_dir(&temp_dir, &target_dir, exe_name) {
        log(&format!("Target validation failed: {}", err));
        show_update_error_dialog(&err);
        std::process::exit(1);
    }

    let target_exe = target_dir.join(exe_name);

    // Ensure the game isn't running before touching game-data/bin
    while is_majin_game_running() {
        log("majin.exe is running; prompting user to close game before updating.");
        if !prompt_close_running_game() {
            log("User cancelled update while game was running. Relaunching previous launcher.");
            let _ = std::process::Command::new(&target_exe)
                .current_dir(&target_dir)
                .spawn();
            std::process::exit(0);
        }
    }

    // Explicit pre-wipe confirmation (protects v0.1.3 / v0.1.4 users who didn't read release notes)
    if !prompt_confirm_clean_update(version) {
        log("User declined clean update confirmation. Keeping existing installation untouched.");
        let _ = std::process::Command::new(&target_exe)
            .current_dir(&target_dir)
            .spawn();
        std::process::exit(0);
    }

    match perform_clean_update_core(&temp_dir, &target_dir, &current_exe, version, true, log) {
        Ok(summary) => {
            log(&format!(
                "Clean update to v{} completed successfully (saves_migrated={}, profiles={}). Relaunching...",
                summary.notice.version,
                summary.notice.saves_migrated,
                summary.notice.migrated_profile_count
            ));
            let _ = std::process::Command::new(&target_exe)
                .current_dir(&target_dir)
                .spawn();
            std::process::exit(0);
        }
        Err(err) => {
            log(&format!("Clean update failed: {}", err));
            show_update_error_dialog(&format!(
                "The clean update could not be completed:\n\n{}",
                err
            ));
            let _ = std::process::Command::new(&target_exe)
                .current_dir(&target_dir)
                .spawn();
            std::process::exit(1);
        }
    }
}

/// Tauri command invoked once on launcher startup to check whether a clean update just finished.
/// Consumes (deletes) the marker file so the modal is only displayed once.
#[tauri::command]
pub fn consume_clean_update_notice() -> Result<Option<CleanUpdateNotice>, String> {
    let root = get_project_root();
    let notice_path = root.join(CLEAN_UPDATE_NOTICE_REL);
    if !notice_path.is_file() {
        return Ok(None);
    }

    let raw = fs::read_to_string(&notice_path)
        .map_err(|e| format!("Failed to read clean update notice: {}", e))?;
    let _ = fs::remove_file(&notice_path);

    match serde_json::from_str::<CleanUpdateNotice>(&raw) {
        Ok(notice) => Ok(Some(notice)),
        Err(_) => Ok(None),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn make_temp_pair(label: &str) -> (PathBuf, PathBuf, PathBuf) {
        let nanos = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let base = std::env::temp_dir().join(format!(
            "majin_updater_test_{}_{}_{}",
            label,
            std::process::id(),
            nanos
        ));
        let temp_dir = base.join("payload");
        let target_dir = base.join("install");
        fs::create_dir_all(&temp_dir).unwrap();
        fs::create_dir_all(&target_dir).unwrap();
        (base, temp_dir, target_dir)
    }

    fn populate_new_release_payload(temp_dir: &Path) -> PathBuf {
        let new_exe = temp_dir.join("Majistic Launcher.exe");
        fs::write(&new_exe, b"NEW_LAUNCHER_EXE_V015").unwrap();
        fs::create_dir_all(temp_dir.join("game-data/bin")).unwrap();
        fs::write(temp_dir.join("game-data/bin/majin.exe"), b"NEW_MAJIN_PAL").unwrap();
        fs::write(temp_dir.join("game-data/bin/majin-pal.exe"), b"NEW_MAJIN_PAL").unwrap();
        fs::write(temp_dir.join("game-data/bin/majin-usa.exe"), b"NEW_MAJIN_USA").unwrap();
        fs::write(temp_dir.join("game-data/bin/rexgpu-xenos.dll"), b"NEW_GPU_DLL").unwrap();
        fs::write(temp_dir.join("game-data/bin/rexruntime.dll"), b"NEW_RT_DLL").unwrap();
        fs::create_dir_all(temp_dir.join("scripts")).unwrap();
        fs::write(temp_dir.join("scripts/run-usa.ps1"), b"# new run-usa.ps1").unwrap();
        fs::create_dir_all(temp_dir.join("launcher/resources/prebuild")).unwrap();
        fs::write(
            temp_dir.join("launcher/resources/prebuild/4E4D0816.fbo.vk.xpl"),
            b"XPL_DATA",
        )
        .unwrap();
        fs::write(
            temp_dir.join("launcher/resources/prebuild/shader_recipes.json"),
            b"{}",
        )
        .unwrap();
        fs::write(temp_dir.join("README.md"), b"NEW_README").unwrap();
        new_exe
    }

    #[test]
    fn test_clean_update_from_v013_migrates_legacy_user_and_deploys_launcher_folder() {
        let (base, temp_dir, target_dir) = make_temp_pair("v013_legacy");
        let new_exe = populate_new_release_payload(&temp_dir);

        // Simulate v0.1.3 install: old exe, old ISO assets, old shader cache, legacy runtime/user save, user's own .iso file
        fs::write(target_dir.join("Majistic Launcher.exe"), b"OLD_V013").unwrap();
        fs::create_dir_all(target_dir.join("scripts")).unwrap();
        fs::write(target_dir.join("scripts/run-usa.ps1"), b"# old v0.1.3").unwrap();
        fs::create_dir_all(target_dir.join("game-data/bin")).unwrap();
        fs::write(target_dir.join("game-data/bin/rexgpu-xenos.dll"), b"OLD_GPU_DLL").unwrap();
        fs::write(target_dir.join("game-data/default.xex"), b"OLD_XEX").unwrap();
        fs::create_dir_all(target_dir.join("game-data/runtime/cache/vulkan-vulkan/release/shaders/local")).unwrap();
        fs::write(
            target_dir.join("game-data/runtime/cache/vulkan-vulkan/release/shaders/local/old.vkpc"),
            b"STALE_CACHE",
        )
        .unwrap();

        // Legacy v0.1.3 save + achievements under game-data/runtime/user/
        let legacy_save_slot =
            target_dir.join("game-data/runtime/user/B13EBABEBABEBABE/4E4D0816/00000001/KB_SAVE_0");
        fs::create_dir_all(&legacy_save_slot).unwrap();
        fs::write(legacy_save_slot.join("save00.dat"), b"LEGACY_SAVE_BYTES").unwrap();

        // User's own ISO sitting in the install root should NOT be deleted
        let user_iso = target_dir.join("Majin.iso");
        fs::write(&user_iso, b"USER_ISO_STAYS_SAFE").unwrap();

        // Old settings referencing that ISO
        fs::create_dir_all(target_dir.join("private")).unwrap();
        fs::write(
            target_dir.join("private/launcher-usa.json"),
            serde_json::json!({
                "GameDataRoot": "game-data",
                "IsoPath": user_iso.to_string_lossy().to_string(),
                "RenderingPath": "Default"
            })
            .to_string(),
        )
        .unwrap();

        let summary = perform_clean_update_core(
            &temp_dir,
            &target_dir,
            &new_exe,
            "0.1.5",
            true,
            &|_| {},
        )
        .expect("clean update should succeed");

        assert!(summary.notice.saves_migrated);
        assert!(summary.notice.migrated_legacy_user);
        assert_eq!(summary.notice.migrated_profile_count, 1);
        assert_eq!(
            summary.notice.preserved_iso_path,
            user_iso.to_string_lossy().to_string()
        );

        // Old game assets & stale shader cache wiped cleanly
        assert!(!target_dir.join("game-data/default.xex").exists());
        assert!(!target_dir
            .join("game-data/runtime/cache/vulkan-vulkan/release/shaders/local/old.vkpc")
            .exists());
        assert!(!target_dir.join("game-data/runtime/user").exists());

        // New launcher, DLLs, and previously-missed `launcher/resources/prebuild` deployed
        assert_eq!(
            fs::read(target_dir.join("Majistic Launcher.exe")).unwrap(),
            b"NEW_LAUNCHER_EXE_V015"
        );
        assert_eq!(
            fs::read(target_dir.join("game-data/bin/rexgpu-xenos.dll")).unwrap(),
            b"NEW_GPU_DLL"
        );
        assert!(target_dir
            .join("launcher/resources/prebuild/4E4D0816.fbo.vk.xpl")
            .is_file());
        assert!(target_dir
            .join("launcher/resources/prebuild/shader_recipes.json")
            .is_file());

        // Legacy save allocated into game-data/runtime/saves/default/ + valid profiles.json
        let migrated_dat = target_dir.join(
            "game-data/runtime/saves/default/B13EBABEBABEBABE/4E4D0816/00000001/KB_SAVE_0/save00.dat",
        );
        assert_eq!(fs::read(&migrated_dat).unwrap(), b"LEGACY_SAVE_BYTES");
        let reg: SaveRegistry = serde_json::from_str(
            &fs::read_to_string(target_dir.join("game-data/runtime/saves/profiles.json")).unwrap(),
        )
        .unwrap();
        assert_eq!(reg.active_profile_id, DEFAULT_PROFILE_ID);
        assert_eq!(reg.profiles.len(), 1);

        // Fresh settings carried IsoPath over while resetting old RenderingPath to clean Vulkan default
        let fresh_settings: Settings = serde_json::from_str(
            &fs::read_to_string(target_dir.join("private/launcher-usa.json")).unwrap(),
        )
        .unwrap();
        assert_eq!(fresh_settings.rendering_path, "Vulkan");
        assert_eq!(fresh_settings.iso_path, user_iso.to_string_lossy().to_string());
        assert_eq!(fresh_settings.active_save_profile, DEFAULT_PROFILE_ID);

        // User's ISO untouched & notice file written
        assert_eq!(fs::read(&user_iso).unwrap(), b"USER_ISO_STAYS_SAFE");
        assert!(target_dir.join(CLEAN_UPDATE_NOTICE_REL).is_file());

        // Temporary backup dir cleaned up
        let leftover_backups: Vec<_> = fs::read_dir(&target_dir)
            .unwrap()
            .flatten()
            .filter(|e| e.file_name().to_string_lossy().starts_with(".majistic-update-backup"))
            .collect();
        assert!(leftover_backups.is_empty());

        let _ = fs::remove_dir_all(&base);
    }

    #[test]
    fn test_clean_update_preserves_both_modern_profiles_and_legacy_user_without_overwriting() {
        let (base, temp_dir, target_dir) = make_temp_pair("both_modern_and_legacy");
        let new_exe = populate_new_release_payload(&temp_dir);

        fs::write(target_dir.join("Majistic Launcher.exe"), b"OLD").unwrap();
        fs::create_dir_all(target_dir.join("scripts")).unwrap();
        fs::write(target_dir.join("scripts/run-usa.ps1"), b"# old").unwrap();

        // Existing modern saves with 2 profiles (default + save_custom)
        let saves_dir = target_dir.join("game-data/runtime/saves");
        fs::create_dir_all(saves_dir.join("default")).unwrap();
        fs::write(saves_dir.join("default/save00.dat"), b"MODERN_DEFAULT").unwrap();
        fs::create_dir_all(saves_dir.join("save_custom")).unwrap();
        fs::write(saves_dir.join("save_custom/save00.dat"), b"MODERN_CUSTOM").unwrap();
        let reg = SaveRegistry {
            active_profile_id: "save_custom".to_string(),
            profiles: vec![
                SaveProfileRecord {
                    id: "default".to_string(),
                    name: "Default Save".to_string(),
                    created_at: "2026-11-20 10:00".to_string(),
                    is_default: true,
                },
                SaveProfileRecord {
                    id: "save_custom".to_string(),
                    name: "NG+ Run".to_string(),
                    created_at: "2026-11-21 12:00".to_string(),
                    is_default: false,
                },
            ],
        };
        fs::write(
            saves_dir.join("profiles.json"),
            serde_json::to_string_pretty(&reg).unwrap(),
        )
        .unwrap();

        // Also a stray legacy user/ folder with different save data
        fs::create_dir_all(target_dir.join("game-data/runtime/user")).unwrap();
        fs::write(target_dir.join("game-data/runtime/user/save00.dat"), b"STRAY_LEGACY").unwrap();

        let summary = perform_clean_update_core(
            &temp_dir,
            &target_dir,
            &new_exe,
            "0.1.5",
            false,
            &|_| {},
        )
        .expect("clean update should preserve all profiles");

        assert!(summary.notice.saves_migrated);
        assert!(summary.notice.migrated_legacy_user);
        assert_eq!(summary.notice.migrated_profile_count, 3);

        let migrated_reg: SaveRegistry = serde_json::from_str(
            &fs::read_to_string(target_dir.join("game-data/runtime/saves/profiles.json")).unwrap(),
        )
        .unwrap();
        assert_eq!(migrated_reg.active_profile_id, "save_custom");
        assert_eq!(migrated_reg.profiles.len(), 3);
        assert_eq!(
            fs::read(target_dir.join("game-data/runtime/saves/default/save00.dat")).unwrap(),
            b"MODERN_DEFAULT"
        );
        assert_eq!(
            fs::read(target_dir.join("game-data/runtime/saves/save_custom/save00.dat")).unwrap(),
            b"MODERN_CUSTOM"
        );
        assert_eq!(
            fs::read(target_dir.join("game-data/runtime/saves/legacy_migrated/save00.dat")).unwrap(),
            b"STRAY_LEGACY"
        );

        let _ = fs::remove_dir_all(&base);
    }

    #[test]
    fn test_refuses_invalid_target_dir_without_deleting_files() {
        let (base, temp_dir, target_dir) = make_temp_pair("invalid_target");
        let new_exe = populate_new_release_payload(&temp_dir);

        // Unrelated directory (missing Majistic Launcher.exe & project markers)
        let precious_file = target_dir.join("important_document.txt");
        fs::write(&precious_file, b"DO_NOT_DELETE").unwrap();

        let res = perform_clean_update_core(
            &temp_dir,
            &target_dir,
            &new_exe,
            "0.1.5",
            true,
            &|_| {},
        );
        assert!(res.is_err());
        assert_eq!(fs::read(&precious_file).unwrap(), b"DO_NOT_DELETE");

        let _ = fs::remove_dir_all(&base);
    }
}
