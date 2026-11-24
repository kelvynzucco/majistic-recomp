use std::fs;
use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use serde::{Deserialize, Serialize};
use tauri::Emitter;

mod prebuild;
mod saves;

static DEV_MODE_RUNTIME: AtomicBool = AtomicBool::new(false);

fn default_window_size() -> String {
    "1280x720".into()
}

fn default_render_scale() -> u32 {
    1
}

fn default_rendering_path() -> String {
    "Vulkan".into()
}

fn default_frame_rate_mode() -> String {
    "30".into()
}

fn default_post_effect() -> String {
    "dedither_fxaa".into()
}

fn default_upscaler() -> String {
    "bilinear".into()
}

fn default_cutscene_speed() -> u32 {
    4
}

fn default_input_mode() -> String {
    "Controller".into()
}

fn default_game_data_root() -> String {
    "game-data".into()
}

fn default_configuration() -> String {
    "Release".into()
}

fn default_language() -> String {
    "English".into()
}

fn default_audio_language() -> String {
    "Auto".into()
}

fn deserialize_nullable_language<'de, D>(deserializer: D) -> Result<String, D::Error>
where
    D: serde::Deserializer<'de>,
{
    let opt = Option::<String>::deserialize(deserializer)?;
    let trimmed = opt.unwrap_or_default().trim().to_string();
    if trimmed.is_empty() {
        Ok("English".into())
    } else {
        Ok(trimmed)
    }
}

fn deserialize_nullable_audio_language<'de, D>(deserializer: D) -> Result<String, D::Error>
where
    D: serde::Deserializer<'de>,
{
    let opt = Option::<String>::deserialize(deserializer)?;
    let trimmed = opt.unwrap_or_default().trim().to_string();
    if trimmed.is_empty() {
        Ok("Auto".into())
    } else {
        Ok(trimmed)
    }
}

fn deserialize_nullable_empty_string<'de, D>(deserializer: D) -> Result<String, D::Error>
where
    D: serde::Deserializer<'de>,
{
    let opt = Option::<String>::deserialize(deserializer)?;
    Ok(opt.unwrap_or_default().trim().to_string())
}

fn default_true() -> bool {
    true
}

fn default_false() -> bool {
    false
}

fn default_record_performance() -> bool {
    false
}

fn default_fov_scale() -> f64 {
    1.0
}

fn default_draw_distance_scale() -> f64 {
    3.0
}

fn default_dedither_strength() -> f64 {
    0.85
}

fn default_bloom_intensity() -> f64 {
    1.0
}

fn default_cas_sharpness() -> f64 {
    0.0
}

fn default_fsr_reduction() -> f64 {
    0.0
}

fn deserialize_nullable_f64<'de, D>(deserializer: D) -> Result<f64, D::Error>
where
    D: serde::Deserializer<'de>,
{
    let opt = Option::<f64>::deserialize(deserializer)?;
    Ok(opt.unwrap_or(1.0))
}

fn deserialize_nullable_u32_cutscene<'de, D>(deserializer: D) -> Result<u32, D::Error>
where
    D: serde::Deserializer<'de>,
{
    let opt = Option::<u32>::deserialize(deserializer)?;
    Ok(opt.unwrap_or(4))
}

fn default_shadow_scale() -> u32 {
    2
}

fn default_shadow_slope_bias() -> f64 {
    0.30
}

fn default_shadow_constant_bias() -> f64 {
    0.0
}

fn default_shadow_bias_clamp() -> f64 {
    0.002
}

fn deserialize_nullable_f64_slope_bias<'de, D>(deserializer: D) -> Result<f64, D::Error>
where
    D: serde::Deserializer<'de>,
{
    let opt = Option::<f64>::deserialize(deserializer)?;
    Ok(opt.unwrap_or(0.30))
}

fn deserialize_nullable_f64_zero<'de, D>(deserializer: D) -> Result<f64, D::Error>
where
    D: serde::Deserializer<'de>,
{
    let opt = Option::<f64>::deserialize(deserializer)?;
    Ok(opt.unwrap_or(0.0))
}

fn deserialize_nullable_f64_bias_clamp<'de, D>(deserializer: D) -> Result<f64, D::Error>
where
    D: serde::Deserializer<'de>,
{
    let opt = Option::<f64>::deserialize(deserializer)?;
    Ok(opt.unwrap_or(0.002))
}

fn deserialize_nullable_u32_shadow_scale<'de, D>(deserializer: D) -> Result<u32, D::Error>
where
    D: serde::Deserializer<'de>,
{
    let opt = Option::<u32>::deserialize(deserializer)?;
    Ok(opt.unwrap_or(1))
}

fn deserialize_nullable_u32_render_scale<'de, D>(deserializer: D) -> Result<u32, D::Error>
where
    D: serde::Deserializer<'de>,
{
    let opt = Option::<u32>::deserialize(deserializer)?;
    Ok(opt.unwrap_or(1))
}

fn deserialize_nullable_bool_true<'de, D>(deserializer: D) -> Result<bool, D::Error>
where
    D: serde::Deserializer<'de>,
{
    let opt = Option::<bool>::deserialize(deserializer)?;
    Ok(opt.unwrap_or(true))
}

fn default_save_profile() -> String {
    saves::DEFAULT_PROFILE_ID.into()
}

fn deserialize_nullable_save_profile<'de, D>(deserializer: D) -> Result<String, D::Error>
where
    D: serde::Deserializer<'de>,
{
    let opt = Option::<String>::deserialize(deserializer)?;
    let trimmed = opt.unwrap_or_default().trim().to_string();
    if trimmed.is_empty() {
        Ok(saves::DEFAULT_PROFILE_ID.into())
    } else {
        Ok(trimmed)
    }
}

fn deserialize_nullable_bool_false<'de, D>(deserializer: D) -> Result<bool, D::Error>
where
    D: serde::Deserializer<'de>,
{
    let opt = Option::<bool>::deserialize(deserializer)?;
    Ok(opt.unwrap_or(false))
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct Settings {
    #[serde(default = "default_window_size")]
    pub window_size: String,
    #[serde(default = "default_false", deserialize_with = "deserialize_nullable_bool_false")]
    pub fullscreen: bool,
    #[serde(default = "default_render_scale", deserialize_with = "deserialize_nullable_u32_render_scale")]
    pub render_scale: u32,
    #[serde(rename = "ShadowScale", default = "default_shadow_scale", deserialize_with = "deserialize_nullable_u32_shadow_scale")]
    pub shadow_scale: u32,
    #[serde(rename = "ShadowSlopeBias", default = "default_shadow_slope_bias", deserialize_with = "deserialize_nullable_f64_slope_bias")]
    pub shadow_slope_bias: f64,
    #[serde(rename = "ShadowConstantBias", default = "default_shadow_constant_bias", deserialize_with = "deserialize_nullable_f64_zero")]
    pub shadow_constant_bias: f64,
    #[serde(rename = "ShadowBiasClamp", default = "default_shadow_bias_clamp", deserialize_with = "deserialize_nullable_f64_bias_clamp")]
    pub shadow_bias_clamp: f64,
    #[serde(default = "default_rendering_path")]
    pub rendering_path: String,
    #[serde(default = "default_frame_rate_mode")]
    pub frame_rate_mode: String,
    #[serde(rename = "VSync", default = "default_true", deserialize_with = "deserialize_nullable_bool_true")]
    pub vsync: bool,
    #[serde(default = "default_post_effect")]
    pub post_effect: String,
    #[serde(rename = "DeDitherStrength", default = "default_dedither_strength", deserialize_with = "deserialize_nullable_f64")]
    pub dedither_strength: f64,
    #[serde(default = "default_upscaler")]
    pub upscaler: String,
    #[serde(default = "default_false", deserialize_with = "deserialize_nullable_bool_false")]
    pub skip_intro_logos: bool,
    #[serde(default = "default_false", deserialize_with = "deserialize_nullable_bool_false")]
    pub skip_cutscenes: bool,
    #[serde(default = "default_cutscene_speed", deserialize_with = "deserialize_nullable_u32_cutscene")]
    pub cutscene_speed: u32,
    #[serde(default = "default_input_mode")]
    pub input_mode: String,
    #[serde(default = "default_fov_scale", deserialize_with = "deserialize_nullable_f64")]
    pub mouse_sensitivity: f64,
    #[serde(default = "default_record_performance", deserialize_with = "deserialize_nullable_bool_false")]
    pub record_performance: bool,
    #[serde(default = "default_game_data_root")]
    pub game_data_root: String,
    #[serde(rename = "IsoPath", default, deserialize_with = "deserialize_nullable_empty_string")]
    pub iso_path: String,
    #[serde(default = "default_configuration")]
    pub configuration: String,
    #[serde(default = "default_language", deserialize_with = "deserialize_nullable_language")]
    pub language: String,
    #[serde(rename = "AudioLanguage", default = "default_audio_language", deserialize_with = "deserialize_nullable_audio_language")]
    pub audio_language: String,
    #[serde(default = "default_false", deserialize_with = "deserialize_nullable_bool_false")]
    pub step_fix: bool,
    #[serde(default = "default_false", deserialize_with = "deserialize_nullable_bool_false")]
    pub anim_speed_fix: bool,
    #[serde(default = "default_true", deserialize_with = "deserialize_nullable_bool_true")]
    pub dt_smoothing: bool,
    #[serde(default = "default_fov_scale", deserialize_with = "deserialize_nullable_f64")]
    pub fov_scale: f64,
    #[serde(default = "default_draw_distance_scale", deserialize_with = "deserialize_nullable_f64")]
    pub draw_distance_scale: f64,
    #[serde(default = "default_false", deserialize_with = "deserialize_nullable_bool_false")]
    pub pause_blur: bool,
    #[serde(rename = "PauseDistortion", default = "default_true", deserialize_with = "deserialize_nullable_bool_true")]
    pub pause_distortion: bool,
    #[serde(default = "default_false", deserialize_with = "deserialize_nullable_bool_false")]
    pub depth_of_field: bool,
    #[serde(default = "default_true", deserialize_with = "deserialize_nullable_bool_true")]
    pub bloom: bool,
    #[serde(rename = "BloomIntensity", default = "default_bloom_intensity", deserialize_with = "deserialize_nullable_f64")]
    pub bloom_intensity: f64,
    #[serde(rename = "CasSharpness", default = "default_cas_sharpness", deserialize_with = "deserialize_nullable_f64")]
    pub cas_sharpness: f64,
    #[serde(rename = "FsrReduction", default = "default_fsr_reduction", deserialize_with = "deserialize_nullable_f64")]
    pub fsr_reduction: f64,
    #[serde(default = "default_false", deserialize_with = "deserialize_nullable_bool_false")]
    pub present_dither: bool,
    #[serde(rename = "WindowFps", default = "default_false", deserialize_with = "deserialize_nullable_bool_false")]
    pub window_fps: bool,
    #[serde(rename = "DevMode", default = "default_false", deserialize_with = "deserialize_nullable_bool_false")]
    pub dev_mode: bool,
    #[serde(rename = "ActiveSaveProfile", default = "default_save_profile", deserialize_with = "deserialize_nullable_save_profile")]
    pub active_save_profile: String,
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            window_size: "1280x720".into(),
            fullscreen: false,
            render_scale: 1,
            shadow_scale: 2,
            shadow_slope_bias: 0.30,
            shadow_constant_bias: 0.0,
            shadow_bias_clamp: 0.002,
            rendering_path: "Vulkan".into(),
            frame_rate_mode: "30".into(),
            vsync: true,
            post_effect: "dedither_fxaa".into(),
            dedither_strength: 0.85,
            upscaler: "bilinear".into(),
            skip_intro_logos: false,
            skip_cutscenes: false,
            cutscene_speed: 4,
            input_mode: "Controller".into(),
            mouse_sensitivity: 1.0,
            record_performance: false,
            game_data_root: "game-data".into(),
            iso_path: String::new(),
            configuration: "Release".into(),
            language: "English".into(),
            audio_language: "Auto".into(),
            step_fix: false,
            anim_speed_fix: false,
            dt_smoothing: true,
            fov_scale: 1.0,
            draw_distance_scale: 3.0,
            pause_blur: false,
            pause_distortion: true,
            depth_of_field: false,
            bloom: true,
            bloom_intensity: 1.0,
            cas_sharpness: 0.0,
            fsr_reduction: 0.0,
            present_dither: false,
            window_fps: false,
            dev_mode: false,
            active_save_profile: saves::DEFAULT_PROFILE_ID.into(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct GameStatus {
    pub has_game_data: bool,
    pub has_unpacked_image: bool,
    pub has_executable: bool,
    pub is_ready: bool,
    pub game_data_path: String,
    pub executable_path: String,
}

pub(crate) fn get_project_root() -> PathBuf {
    if let Ok(exe) = std::env::current_exe() {
        let mut cur = exe;
        for _ in 0..6 {
            if let Some(parent) = cur.parent() {
                if parent.join("scripts/run-usa.ps1").exists() {
                    return parent.to_path_buf();
                }
                cur = parent.to_path_buf();
            } else {
                break;
            }
        }
    }

    if let Ok(mut cur) = std::env::current_dir() {
        for _ in 0..6 {
            if cur.join("scripts/run-usa.ps1").exists() {
                return cur;
            }
            if let Some(parent) = cur.parent() {
                cur = parent.to_path_buf();
            } else {
                break;
            }
        }
    }

    PathBuf::from(".")
}

fn get_settings_path() -> PathBuf {
    get_project_root().join("private/launcher-usa.json")
}

pub(crate) fn load_settings_internal() -> Result<Settings, String> {
    let path = get_settings_path();
    if !path.exists() {
        return Ok(Settings::default());
    }
    let data = fs::read_to_string(&path).map_err(|e| e.to_string())?;
    let settings: Settings = serde_json::from_str(&data).unwrap_or_default();
    Ok(settings)
}

pub(crate) fn save_settings_internal(settings: Settings) -> Result<(), String> {
    DEV_MODE_RUNTIME.store(settings.dev_mode, Ordering::Relaxed);
    let path = get_settings_path();
    if let Some(parent) = path.parent() {
        let _ = fs::create_dir_all(parent);
    }
    let json = serde_json::to_string_pretty(&settings).map_err(|e| e.to_string())?;
    fs::write(&path, json).map_err(|e| e.to_string())?;
    Ok(())
}

#[tauri::command]
fn load_settings() -> Result<Settings, String> {
    load_settings_internal()
}

#[tauri::command]
fn save_settings(settings: Settings) -> Result<(), String> {
    save_settings_internal(settings)
}

#[tauri::command]
fn check_game_status() -> Result<GameStatus, String> {
    let root = get_project_root();
    let settings = load_settings().unwrap_or_default();
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
    let has_game_data = game_data_dir.join("default.xex").exists();
    let has_unpacked_image = game_data_dir.join("unpacked/loaded-image.bin").exists()
        || root.join("prototype-usa/private/loaded-image.bin").exists();
    let exe_unified = game_data_dir.join("bin/majin.exe");
    let exe_proto_usa = root.join("prototype-usa/out/build/win-amd64-release/majin.exe");
    let exe_proto_pal = root.join("prototype/out/build/win-amd64-release/majin.exe");
    let has_executable = exe_unified.exists() || exe_proto_usa.exists() || exe_proto_pal.exists();
    let is_ready = has_game_data && has_executable;

    let display_game_data_path = if let Ok(rel) = game_data_dir.strip_prefix(&root) {
        rel.to_string_lossy().to_string()
    } else {
        folder_name
    };

    let display_exe_path = if exe_unified.exists() {
        if let Ok(rel) = exe_unified.strip_prefix(&root) {
            rel.to_string_lossy().to_string()
        } else {
            "game-data/bin/majin.exe".to_string()
        }
    } else if exe_proto_pal.exists() {
        "prototype/out/build/win-amd64-release/majin.exe".to_string()
    } else {
        "prototype-usa/out/build/win-amd64-release/majin.exe".to_string()
    };

    Ok(GameStatus {
        has_game_data,
        has_unpacked_image,
        has_executable,
        is_ready,
        game_data_path: display_game_data_path,
        executable_path: display_exe_path,
    })
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct LanguageOption {
    pub value: String,
    pub label: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AvailableLanguagesInfo {
    pub edition_code: String,
    pub edition_name: String,
    pub detected_from_assets: bool,
    pub text_languages: Vec<LanguageOption>,
    pub audio_languages: Vec<LanguageOption>,
}

struct LanguageCatalogEntry {
    value: &'static str,
    label: &'static str,
    pak_name: &'static str,
    audio_suffix: Option<&'static str>,
    supported_pal: bool,
    supported_usa: bool,
}

const LANGUAGE_CATALOG: &[LanguageCatalogEntry] = &[
    LanguageCatalogEntry { value: "English",    label: "English",                 pak_name: "us", audio_suffix: Some("en"), supported_pal: true,  supported_usa: true  },
    LanguageCatalogEntry { value: "Portuguese", label: "Portuguese (Português)",  pak_name: "po", audio_suffix: Some("po"), supported_pal: true,  supported_usa: false },
    LanguageCatalogEntry { value: "Spanish",    label: "Spanish (Español)",       pak_name: "sp", audio_suffix: Some("sp"), supported_pal: true,  supported_usa: true  },
    LanguageCatalogEntry { value: "French",     label: "French (Français)",       pak_name: "fr", audio_suffix: Some("fr"), supported_pal: true,  supported_usa: true  },
    LanguageCatalogEntry { value: "German",     label: "German (Deutsch)",        pak_name: "ge", audio_suffix: Some("ge"), supported_pal: true,  supported_usa: false },
    LanguageCatalogEntry { value: "Italian",    label: "Italian (Italiano)",      pak_name: "it", audio_suffix: Some("it"), supported_pal: true,  supported_usa: false },
    LanguageCatalogEntry { value: "Dutch",      label: "Dutch (Nederlands)",      pak_name: "du", audio_suffix: Some("du"), supported_pal: true,  supported_usa: false },
    LanguageCatalogEntry { value: "Russian",    label: "Russian (Русский)",       pak_name: "ru", audio_suffix: Some("ru"), supported_pal: true,  supported_usa: false },
    LanguageCatalogEntry { value: "Korean",     label: "Korean (한국어)",          pak_name: "kr", audio_suffix: None,       supported_pal: false, supported_usa: true  },
    LanguageCatalogEntry { value: "Japanese",   label: "Japanese (日本語)",        pak_name: "jp", audio_suffix: Some("jp"), supported_pal: false, supported_usa: false },
];

fn detect_game_edition(game_data_dir: &Path) -> (&'static str, &'static str) {
    use std::io::Read;
    let xex_path = game_data_dir.join("default.xex");
    if !xex_path.exists() {
        return ("NotInstalled", "Not Installed");
    }
    if let Ok(file) = fs::File::open(&xex_path) {
        let mut header = Vec::with_capacity(16384);
        if file.take(16384).read_to_end(&mut header).is_ok() {
            for win in header.windows(4) {
                if win == [0x67, 0xBD, 0x87, 0xEF] {
                    return ("PAL", "Europe / PAL (0.0.0.3)");
                }
                if win == [0x76, 0xFD, 0xB6, 0x42] {
                    return ("USA", "USA / NTSC-U (0.0.0.2)");
                }
            }
        }
    }
    ("Unknown", "Unknown Edition")
}

fn dir_has_regular_file(dir: &Path) -> bool {
    if !dir.is_dir() {
        return false;
    }
    if let Ok(entries) = fs::read_dir(dir) {
        for entry in entries.flatten() {
            if entry.path().is_file() {
                return true;
            }
        }
    }
    false
}

fn detect_available_languages_in_dir(game_data_dir: &Path) -> AvailableLanguagesInfo {
    let (edition_code, edition_name) = detect_game_edition(game_data_dir);
    let pkg_dir = game_data_dir.join("finalizedXBox360/KB/package");
    let sound_dir = game_data_dir.join("finalizedXBox360/KB/sound/xbox360");

    let mut text_languages: Vec<LanguageOption> = Vec::new();
    let mut audio_languages: Vec<LanguageOption> = Vec::new();
    let mut detected_from_assets = false;

    if pkg_dir.is_dir() {
        for entry in LANGUAGE_CATALOG {
            let edition_allows = match edition_code {
                "PAL" => entry.supported_pal,
                "USA" => entry.supported_usa,
                _ => true,
            };
            if edition_allows && pkg_dir.join(format!("{}.pak", entry.pak_name)).is_file() {
                text_languages.push(LanguageOption {
                    value: entry.value.to_string(),
                    label: entry.label.to_string(),
                });
            }
        }
        if !text_languages.is_empty() {
            detected_from_assets = true;
        }
    }

    if sound_dir.is_dir() {
        for entry in LANGUAGE_CATALOG {
            if let Some(suffix) = entry.audio_suffix {
                let rtd_dir = sound_dir.join(format!("rtd_{}", suffix));
                if dir_has_regular_file(&rtd_dir) {
                    audio_languages.push(LanguageOption {
                        value: entry.value.to_string(),
                        label: entry.label.to_string(),
                    });
                }
            }
        }
        if !audio_languages.is_empty() {
            detected_from_assets = true;
        }
    }

    if text_languages.is_empty() {
        text_languages.push(LanguageOption {
            value: "English".to_string(),
            label: "English".to_string(),
        });
    }
    if audio_languages.is_empty() {
        audio_languages.push(LanguageOption {
            value: "English".to_string(),
            label: "English".to_string(),
        });
    }

    AvailableLanguagesInfo {
        edition_code: edition_code.to_string(),
        edition_name: edition_name.to_string(),
        detected_from_assets,
        text_languages,
        audio_languages,
    }
}

#[tauri::command]
fn get_available_languages() -> Result<AvailableLanguagesInfo, String> {
    let root = get_project_root();
    let settings = load_settings_internal().unwrap_or_default();
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
    Ok(detect_available_languages_in_dir(&game_data_dir))
}

#[tauri::command]
fn select_game_data_folder() -> Result<Option<String>, String> {
    let script = r#"
        Add-Type -AssemblyName System.Windows.Forms
        $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
        $dialog.Description = 'Select Game Data Folder'
        if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
            Write-Output $dialog.SelectedPath
        }
    "#;
    let mut cmd = Command::new("powershell.exe");
    cmd.args(["-NoProfile", "-Command", script]);
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        cmd.creation_flags(0x08000000); // CREATE_NO_WINDOW
    }
    let output = cmd.output().map_err(|e| format!("Failed to open folder picker: {}", e))?;

    let path = String::from_utf8_lossy(&output.stdout).trim().to_string();
    if path.is_empty() {
        Ok(None)
    } else {
        let p = PathBuf::from(&path);
        let root = get_project_root();
        if let Ok(rel) = p.strip_prefix(&root) {
            Ok(Some(rel.to_string_lossy().to_string()))
        } else {
            Ok(Some(path))
        }
    }
}

#[tauri::command]
fn select_iso_file() -> Result<Option<String>, String> {
    let script = r#"
        Add-Type -AssemblyName System.Windows.Forms
        $dialog = New-Object System.Windows.Forms.OpenFileDialog
        $dialog.Filter = 'Xbox 360 Game Image (*.iso)|*.iso|All Files (*.*)|*.*'
        $dialog.Title = 'Select Majin and the Forsaken Kingdom ISO'
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
    let output = cmd.output().map_err(|e| format!("Failed to open file picker: {}", e))?;

    let path = String::from_utf8_lossy(&output.stdout).trim().to_string();
    if path.is_empty() {
        Ok(None)
    } else {
        Ok(Some(path))
    }
}

#[tauri::command]
fn start_game_build(app: tauri::AppHandle, iso_path: String, compile_binary: bool) -> Result<(), String> {
    let root = get_project_root();
    let iso = PathBuf::from(&iso_path);
    if !iso.exists() {
        return Err(format!("Selected ISO file does not exist: {}", iso_path));
    }

    std::thread::spawn(move || {
        let emit_log = |msg: &str| {
            let _ = app.emit("build-log", msg.to_string());
        };
        let emit_stage = |stage: u32, total: u32, desc: &str| {
            #[derive(Serialize, Clone)]
            struct StagePayload<'a> {
                stage: u32,
                total: u32,
                description: &'a str,
            }
            let _ = app.emit("build-stage", StagePayload { stage, total, description: desc });
        };

        let total_stages = if compile_binary { 4 } else { 3 };

        let run_ps_script = |script_name: &str, extra_args: &[&str]| -> Result<(), String> {
            let script_path = root.join("scripts").join(script_name);
            if !script_path.exists() {
                return Err(format!("Script not found: {}", script_path.display()));
            }

            let mut cmd_args = vec![
                "-NoLogo",
                "-NoProfile",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                script_path.to_str().unwrap_or_default(),
            ];
            cmd_args.extend_from_slice(extra_args);

            let mut cmd = Command::new("powershell.exe");
            cmd.args(&cmd_args)
                .current_dir(&root)
                .stdout(Stdio::piped())
                .stderr(Stdio::piped());

            #[cfg(windows)]
            {
                use std::os::windows::process::CommandExt;
                cmd.creation_flags(0x08000000); // CREATE_NO_WINDOW
            }

            let mut child = cmd.spawn()
                .map_err(|e| format!("Failed to spawn {}: {}", script_name, e))?;

            let stdout = child.stdout.take();
            let stderr = child.stderr.take();

            let app_out = app.clone();
            let stdout_thread = std::thread::spawn(move || {
                if let Some(out) = stdout {
                    let reader = BufReader::new(out);
                    for line in reader.lines().flatten() {
                        let _ = app_out.emit("build-log", line);
                    }
                }
            });

            let app_err = app.clone();
            let stderr_thread = std::thread::spawn(move || {
                if let Some(err) = stderr {
                    let reader = BufReader::new(err);
                    for line in reader.lines().flatten() {
                        let _ = app_err.emit("build-log", format!("[stderr] {}", line));
                    }
                }
            });

            let _ = stdout_thread.join();
            let _ = stderr_thread.join();

            let status = child.wait().map_err(|e| format!("Process error: {}", e))?;
            if !status.success() {
                return Err(format!("{} exited with status: {}", script_name, status));
            }
            Ok(())
        };

        // Stage 1: Extract ISO
        let settings = load_settings().unwrap_or_default();
        let folder_name = if settings.game_data_root.trim().is_empty() {
            "game-data".to_string()
        } else {
            settings.game_data_root.trim().to_string()
        };
        let out_dir_rel = if Path::new(&folder_name).is_absolute() {
            if let Ok(rel) = PathBuf::from(&folder_name).strip_prefix(&root) {
                rel.to_string_lossy().to_string()
            } else {
                folder_name.clone()
            }
        } else {
            folder_name.clone()
        };

        emit_stage(1, total_stages, &format!("Extracting ISO files to {}...", out_dir_rel));
        emit_log(&format!("=== STAGE 1/{}: EXTRACTING ISO ===", total_stages));
        emit_log(&format!("Source ISO: {}", iso_path));
        emit_log(&format!("Target Folder: {}", out_dir_rel));
        if let Err(e) = run_ps_script("extract-iso.ps1", &["-IsoPath", &iso_path, "-OutputDir", &out_dir_rel]) {
            emit_log(&format!("ERROR in Stage 1: {}", e));
            let _ = app.emit("build-error", format!("Extraction failed: {}", e));
            return;
        }

        // Stage 2: Unpack XEX
        emit_stage(2, total_stages, "Unpacking default.xex executable...");
        emit_log(&format!("=== STAGE 2/{}: UNPACKING XEX ===", total_stages));
        if let Err(e) = run_ps_script("unpack-xex-usa.ps1", &["-GameDataRoot", &out_dir_rel]) {
            emit_log(&format!("ERROR in Stage 2: {}", e));
            let _ = app.emit("build-error", format!("Unpacking XEX failed: {}", e));
            return;
        }

        // Stage 3 (optional): Compile Binary
        if compile_binary {
            emit_stage(3, total_stages, "Compiling native game executable (majin.exe)...");
            emit_log(&format!("=== STAGE 3/{}: COMPILING GAME ===", total_stages));
            if let Err(e) = run_ps_script("build-usa.ps1", &["-Configuration", "Release", "-GameDataRoot", &out_dir_rel]) {
                emit_log(&format!("ERROR in Stage 3: {}", e));
                let _ = app.emit("build-error", format!("Compilation failed: {}", e));
                return;
            }
        }

        // Final Stage: Reconstruct Clean-Room .xsh + .xpl & Precompile Vulkan Shaders
        let final_stage = total_stages;
        emit_stage(
            final_stage,
            total_stages,
            "Rebuilding Clean-Room shaders & precompiling Vulkan pipelines...",
        );
        emit_log(&format!(
            "=== STAGE {}/{}: REBUILDING SHADERS & PRECOMPILING VULKAN PIPELINES ===",
            final_stage, total_stages
        ));
        if let Err(e) = run_vulkan_shader_precompile_pipeline(
            &root,
            &settings,
            &|msg| emit_log(msg),
            &|_curr, _total, status| {
                emit_stage(final_stage, total_stages, status);
            },
        ) {
            emit_log(&format!("ERROR in Stage {}: {}", final_stage, e));
            let _ = app.emit("build-error", format!("Shader precompilation failed: {}", e));
            return;
        }

        emit_log("=== BUILD PIPELINE FINISHED SUCCESSFULLY! ===");
        let _ = app.emit("build-complete", true);
    });

    Ok(())
}

fn run_vulkan_shader_precompile_pipeline(
    root: &Path,
    settings: &Settings,
    emit_log: &(dyn Fn(&str) + Send + Sync),
    emit_progress: &(dyn Fn(u32, u32, &str) + Send + Sync),
) -> Result<(), String> {
    let script = root.join("scripts/run-usa.ps1");
    if !script.exists() {
        return Err(format!("Runner script not found at: {}", script.display()));
    }

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
    let game_data_rel = if Path::new(&folder_name).is_absolute() {
        if let Ok(rel) = PathBuf::from(&folder_name).strip_prefix(root) {
            rel.to_string_lossy().to_string()
        } else {
            folder_name.clone()
        }
    } else {
        folder_name.clone()
    };

    // 1. Reconstruct Clean-Room .xsh + .xpl from shader_recipes.json and player's extracted ISO files
    emit_progress(2, 100, "Rebuilding Clean-Room Xenos shader cache from local game files...");
    prebuild::rebuild_prebuild_shader_cache(
        root,
        &game_data_dir,
        &settings.configuration,
        &|msg| emit_log(msg),
    )?;

    // 2. Run Vulkan precompiler (--shader_prebuild_only=true)
    emit_progress(5, 100, "Starting Vulkan shader precompiler...");

    let is_dev = settings.dev_mode || is_dev_or_debug();
    let dev_mode_arg = format!("-DevMode:${}", is_dev);
    let mut cmd_args = vec![
        "-NoLogo".to_string(),
        "-NoProfile".to_string(),
        "-ExecutionPolicy".to_string(),
        "Bypass".to_string(),
        "-File".to_string(),
        script.to_string_lossy().to_string(),
        "-RenderingPath".to_string(),
        "Vulkan".to_string(),
        "-GameDataRoot".to_string(),
        game_data_rel,
        "-Configuration".to_string(),
        settings.configuration.clone(),
        dev_mode_arg,
        "-ShaderPrebuildOnly".to_string(),
    ];
    if !is_dev {
        cmd_args.push("-DisableLogging".to_string());
    }

    let mut cmd = Command::new("powershell.exe");
    cmd.args(&cmd_args)
        .current_dir(root)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());

    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        cmd.creation_flags(0x08000000); // CREATE_NO_WINDOW
    }

    let mut child = cmd
        .spawn()
        .map_err(|e| format!("Failed to spawn precompile process: {}", e))?;

    let stdout = child.stdout.take();
    let stderr = child.stderr.take();

    let log_path_sync = std::sync::Arc::new(std::sync::Mutex::new(None::<PathBuf>));
    let log_path_writer = std::sync::Arc::clone(&log_path_sync);

    std::thread::scope(|s| {
        s.spawn(|| {
            if let Some(out) = stdout {
                let reader = BufReader::new(out);
                for line in reader.lines().flatten() {
                    emit_log(&line);
                    if line.starts_with("Runtime log: ") {
                        let path_str = line.trim_start_matches("Runtime log: ").trim();
                        if let Ok(mut lock) = log_path_writer.lock() {
                            *lock = Some(PathBuf::from(path_str));
                        }
                    }
                }
            }
        });

        s.spawn(|| {
            if let Some(err) = stderr {
                let reader = BufReader::new(err);
                for line in reader.lines().flatten() {
                    emit_log(&format!("[stderr] {}", line));
                }
            }
        });

        let is_running = std::sync::Arc::new(std::sync::atomic::AtomicBool::new(true));
        let is_running_tail = std::sync::Arc::clone(&is_running);
        let log_path_reader = std::sync::Arc::clone(&log_path_sync);

        let tail_handle = s.spawn(move || {
            let mut resolved_path = None::<PathBuf>;
            let mut file_offset = 0u64;

            while is_running_tail.load(std::sync::atomic::Ordering::Relaxed) {
                if resolved_path.is_none() {
                    if let Ok(lock) = log_path_reader.lock() {
                        if let Some(ref p) = *lock {
                            if p.exists() {
                                resolved_path = Some(p.clone());
                            }
                        }
                    }
                }

                if let Some(ref p) = resolved_path {
                    if let Ok(f) = std::fs::File::open(p) {
                        use std::io::Seek;
                        let mut file = f;
                        if file.seek(std::io::SeekFrom::Start(file_offset)).is_ok() {
                            let reader = BufReader::new(file);
                            for line in reader.lines().flatten() {
                                file_offset += (line.len() + 2) as u64;
                                emit_log(&line);

                                if let Some(idx) = line.find("SHADER_WARMUP: Vulkan progress ") {
                                    let rest = &line[idx + "SHADER_WARMUP: Vulkan progress ".len()..];
                                    let parts: Vec<&str> = rest.split('/').collect();
                                    if parts.len() == 2 {
                                        let curr = parts[0].trim().parse::<u32>().unwrap_or(0);
                                        let total = parts[1].trim().parse::<u32>().unwrap_or(288);
                                        let status_text = format!(
                                            "Compiling Vulkan pipelines ({}/{})...",
                                            curr, total
                                        );
                                        emit_progress(curr, total, &status_text);
                                    }
                                } else if line.contains("Vulkan monolithic driver pipeline cache: saved")
                                    || line.contains("SHADER_PREBUILD: complete")
                                {
                                    emit_progress(
                                        100,
                                        100,
                                        "Monolithic driver cache saved to disk",
                                    );
                                }
                            }
                        }
                    }
                }

                std::thread::sleep(std::time::Duration::from_millis(60));
            }
        });

        let wait_res = child.wait();
        is_running.store(false, std::sync::atomic::Ordering::Relaxed);
        let _ = tail_handle.join();

        match wait_res {
            Ok(status) if status.success() => Ok(()),
            Ok(status) => Err(format!("Precompile exited with error status: {}", status)),
            Err(e) => Err(format!("Failed to wait on precompile process: {}", e)),
        }
    })
}

#[tauri::command]
fn precompile_shaders(app: tauri::AppHandle, settings: Settings) -> Result<(), String> {
    save_settings(settings.clone())?;

    let root = get_project_root();
    let script = root.join("scripts/run-usa.ps1");
    if !script.exists() {
        return Err(format!("Runner script not found at: {}", script.display()));
    }

    std::thread::spawn(move || {
        let app_log = app.clone();
        let emit_log = move |msg: &str| {
            let _ = app_log.emit("precompile-log", msg.to_string());
        };
        let app_prog = app.clone();
        let emit_progress = move |current: u32, total: u32, status: &str| {
            #[derive(Serialize, Clone)]
            struct ProgressPayload<'a> {
                current: u32,
                total: u32,
                status: &'a str,
            }
            let _ = app_prog.emit(
                "precompile-progress",
                ProgressPayload {
                    current,
                    total,
                    status,
                },
            );
        };

        emit_log("=== STARTING VULKAN SHADER PRECOMPILATION ===");

        match run_vulkan_shader_precompile_pipeline(&root, &settings, &emit_log, &emit_progress) {
            Ok(()) => {
                emit_progress(100, 100, "Vulkan pipeline cache successfully created!");
                emit_log("=== VULKAN SHADERS PRECOMPILED SUCCESSFULLY ===");
                let _ = app.emit("precompile-complete", true);
            }
            Err(err_msg) => {
                emit_log(&err_msg);
                let _ = app.emit("precompile-error", err_msg);
            }
        }
    });

    Ok(())
}

pub const BASELINE_PIPELINE_COUNT: usize = 288;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ShaderCacheInfo {
    pub exists: bool,
    pub is_precompiled: bool,
    pub xsh_bytes: u64,
    pub xsh_formatted: String,
    pub xpso_bytes: u64,
    pub xpso_formatted: String,
    pub pipeline_count: usize,
    pub baseline_pipeline_count: usize,
    pub delta_pipelines: i64,
    pub vkpc_bytes: u64,
    pub vkpc_formatted: String,
    pub total_bytes: u64,
    pub total_formatted: String,
    pub last_updated: String,
}

fn format_bytes(bytes: u64) -> String {
    if bytes >= 1024 * 1024 * 1024 {
        format!("{:.2} GB", bytes as f64 / (1024.0 * 1024.0 * 1024.0))
    } else if bytes >= 1024 * 1024 {
        format!("{:.2} MB", bytes as f64 / (1024.0 * 1024.0))
    } else if bytes >= 1024 {
        format!("{:.1} KB", bytes as f64 / 1024.0)
    } else {
        format!("{} B", bytes)
    }
}

#[tauri::command]
fn get_shader_cache_info() -> Result<ShaderCacheInfo, String> {
    let root = get_project_root();
    let settings = load_settings().unwrap_or_default();
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

    let shaders_dir = game_data_dir.join("runtime/cache/vulkan-vulkan/release/shaders");
    let shareable_dir = shaders_dir.join("shareable");
    let prebuild_dir = shaders_dir.join("prebuild");
    let local_dir = shaders_dir.join("local");

    let xsh_path = shareable_dir.join("4E4D0816.xsh");
    let xpso_path = shareable_dir.join("4E4D0816.fbo.vk.xpso");
    let prebuild_xsh_path = prebuild_dir.join("4E4D0816.xsh");
    let prebuild_xpl_path = prebuild_dir.join("4E4D0816.fbo.vk.xpl");

    let mut xsh_bytes = 0u64;
    let mut xpso_bytes = 0u64;
    let mut pipeline_count = 0usize;
    let mut vkpc_bytes = 0u64;
    let mut latest_time: Option<std::time::SystemTime> = None;

    if let Ok(meta) = fs::metadata(&xsh_path) {
        xsh_bytes = meta.len();
        if let Ok(mod_time) = meta.modified() {
            latest_time = Some(mod_time);
        }
    }
    if xsh_bytes <= 8 {
        if let Ok(meta) = fs::metadata(&prebuild_xsh_path) {
            xsh_bytes = meta.len();
            if let Ok(mod_time) = meta.modified() {
                if latest_time.is_none() || mod_time > latest_time.unwrap() {
                    latest_time = Some(mod_time);
                }
            }
        }
    }

    if let Ok(meta) = fs::metadata(&xpso_path) {
        xpso_bytes = meta.len();
        if xpso_bytes >= 20 {
            pipeline_count = ((xpso_bytes - 20) / 68) as usize;
        }
        if let Ok(mod_time) = meta.modified() {
            if latest_time.is_none() || mod_time > latest_time.unwrap() {
                latest_time = Some(mod_time);
            }
        }
    }
    if pipeline_count == 0 {
        if let Ok(meta) = fs::metadata(&prebuild_xpl_path) {
            xpso_bytes = meta.len();
            if xpso_bytes >= 40 {
                pipeline_count = ((xpso_bytes - 40) / 96) as usize;
            }
            if let Ok(mod_time) = meta.modified() {
                if latest_time.is_none() || mod_time > latest_time.unwrap() {
                    latest_time = Some(mod_time);
                }
            }
        }
    }

    if let Ok(entries) = fs::read_dir(&local_dir) {
        for entry in entries.flatten() {
            let path = entry.path();
            if path.extension().and_then(|e| e.to_str()) == Some("vkpc")
                || path.to_string_lossy().ends_with(".monolithic.vkpc")
            {
                if let Ok(meta) = entry.metadata() {
                    vkpc_bytes += meta.len();
                    if let Ok(mod_time) = meta.modified() {
                        if latest_time.is_none() || mod_time > latest_time.unwrap() {
                            latest_time = Some(mod_time);
                        }
                    }
                }
            }
        }
    }

    let total_bytes = xsh_bytes + xpso_bytes + vkpc_bytes;
    let exists = total_bytes > 0;
    let is_precompiled = vkpc_bytes > 0;

    let last_updated = if let Some(time) = latest_time {
        let datetime: chrono::DateTime<chrono::Local> = time.into();
        datetime.format("%Y-%m-%d %H:%M:%S").to_string()
    } else {
        "No cache files found".to_string()
    };

    let delta_pipelines = if pipeline_count > 0 {
        pipeline_count as i64 - BASELINE_PIPELINE_COUNT as i64
    } else {
        0
    };

    Ok(ShaderCacheInfo {
        exists,
        is_precompiled,
        xsh_bytes,
        xsh_formatted: format_bytes(xsh_bytes),
        xpso_bytes,
        xpso_formatted: format_bytes(xpso_bytes),
        pipeline_count,
        baseline_pipeline_count: BASELINE_PIPELINE_COUNT,
        delta_pipelines,
        vkpc_bytes,
        vkpc_formatted: format_bytes(vkpc_bytes),
        total_bytes,
        total_formatted: format_bytes(total_bytes),
        last_updated,
    })
}

#[tauri::command]
fn clean_shader_cache() -> Result<bool, String> {
    let root = get_project_root();
    let settings = load_settings().unwrap_or_default();
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

    let cache_dir = game_data_dir.join("runtime/cache/vulkan-vulkan");
    let mut deleted_count = 0;

    if cache_dir.exists() {
        let local_dirs = [
            cache_dir.join("release/shaders/local"),
            cache_dir.join("debug/shaders/local"),
        ];

        for local_dir in &local_dirs {
            if local_dir.exists() {
                if let Ok(entries) = fs::read_dir(local_dir) {
                    for entry in entries.flatten() {
                        let path = entry.path();
                        if path.extension().and_then(|e| e.to_str()) == Some("vkpc")
                            || path.to_string_lossy().ends_with(".monolithic.vkpc")
                        {
                            if fs::remove_file(&path).is_ok() {
                                deleted_count += 1;
                            }
                        }
                    }
                }
            }
        }
    }

    Ok(deleted_count > 0)
}

#[tauri::command]
fn launch_game(settings: Settings) -> Result<(), String> {
    save_settings(settings.clone())?;

    let root = get_project_root();
    let script = root.join("scripts/run-usa.ps1");
    if !script.exists() {
        return Err(format!("Runner script not found at: {}", script.display()));
    }

    let game_data_rel = if Path::new(&settings.game_data_root).is_absolute() {
        if let Ok(rel) = PathBuf::from(&settings.game_data_root).strip_prefix(&root) {
            rel.to_string_lossy().to_string()
        } else {
            settings.game_data_root.clone()
        }
    } else if settings.game_data_root.trim().is_empty() {
        "game-data".to_string()
    } else {
        settings.game_data_root.trim().to_string()
    };

    let is_dev = settings.dev_mode || is_dev_or_debug();
    let log_file_path = if is_dev {
        let p = root.join("private/launcher-launch.log");
        if let Some(parent) = p.parent() {
            let _ = fs::create_dir_all(parent);
        }
        Some(p)
    } else {
        None
    };

    let mut args = vec![
        "-NoLogo".to_string(),
        "-NoProfile".to_string(),
        "-ExecutionPolicy".to_string(),
        "Bypass".to_string(),
        "-File".to_string(),
        script.to_string_lossy().to_string(),
        "-WindowSize".to_string(),
        settings.window_size,
        format!("-Fullscreen:${}", settings.fullscreen),
        format!("-VSync:${}", settings.vsync),
        "-RenderScale".to_string(),
        settings.render_scale.to_string(),
        "-ShadowScale".to_string(),
        settings.shadow_scale.to_string(),
        "-ShadowSlopeBias".to_string(),
        format!("{:.2}", settings.shadow_slope_bias),
        "-ShadowConstantBias".to_string(),
        format!("{:.3}", settings.shadow_constant_bias),
        "-ShadowBiasClamp".to_string(),
        format!("{:.3}", settings.shadow_bias_clamp),
        "-PostEffect".to_string(),
        settings.post_effect,
        "-DeDitherStrength".to_string(),
        settings.dedither_strength.to_string(),
        "-Upscaler".to_string(),
        settings.upscaler,
        "-RenderingPath".to_string(),
        settings.rendering_path,
        "-FrameRateMode".to_string(),
        settings.frame_rate_mode,
        "-InputMode".to_string(),
        settings.input_mode,
        "-MouseSensitivity".to_string(),
        settings.mouse_sensitivity.to_string(),
        format!("-SkipIntroLogos:${}", settings.skip_intro_logos),
        format!("-SkipCutscenes:${}", settings.skip_cutscenes),
        "-CutsceneSpeed".to_string(),
        settings.cutscene_speed.to_string(),
        "-GameDataRoot".to_string(),
        game_data_rel,
        "-Language".to_string(),
        settings.language,
        "-AudioLanguage".to_string(),
        if settings.audio_language.trim().is_empty() {
            "Auto".to_string()
        } else {
            settings.audio_language
        },
        format!("-StepFix:${}", settings.step_fix),
        format!("-AnimSpeedFix:${}", settings.anim_speed_fix),
        format!("-DtSmoothing:${}", settings.dt_smoothing),
        "-FovScale".to_string(),
        settings.fov_scale.to_string(),
        "-DrawDistanceScale".to_string(),
        settings.draw_distance_scale.to_string(),
        format!("-PauseDistortion:${}", settings.pause_distortion),
        format!("-DepthOfField:${}", settings.depth_of_field),
        format!("-Bloom:${}", settings.bloom_intensity),
        "-BloomIntensity".to_string(),
        settings.bloom_intensity.to_string(),
        "-CasSharpness".to_string(),
        settings.cas_sharpness.to_string(),
        "-FsrReduction".to_string(),
        settings.fsr_reduction.to_string(),
        format!("-WindowFps:${}", settings.window_fps),
        format!("-PresentDither:${}", settings.present_dither),
        format!("-DevMode:${}", is_dev),
        "-Configuration".to_string(),
        settings.configuration,
        "-ActiveSaveProfile".to_string(),
        if settings.active_save_profile.trim().is_empty() {
            saves::DEFAULT_PROFILE_ID.to_string()
        } else {
            settings.active_save_profile
        },
    ];

    if !is_dev {
        args.push("-DisableLogging".to_string());
    }

    let mut cmd = Command::new("powershell.exe");
    cmd.args(&args).current_dir(&root);

    if let Some(ref p) = log_file_path {
        let out_file = fs::File::create(p)
            .map_err(|e| format!("Failed to create launch log: {}", e))?;
        let err_file = out_file
            .try_clone()
            .map_err(|e| format!("Failed to clone file handle: {}", e))?;
        cmd.stdout(out_file).stderr(err_file);
    } else {
        cmd.stdout(Stdio::null()).stderr(Stdio::piped());
    }

    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        const CREATE_NEW_PROCESS_GROUP: u32 = 0x00000200;
        const CREATE_NO_WINDOW: u32 = 0x08000000;
        cmd.creation_flags(CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW);
    }

    let mut child = cmd.spawn().map_err(|e| format!("Failed to spawn runner process: {}", e))?;

    // Allow PowerShell 1.5s to validate parameters and launch majin.exe
    std::thread::sleep(std::time::Duration::from_millis(1500));

    match child.try_wait() {
        Ok(Some(status)) => {
            let detail = if let Some(ref p) = log_file_path {
                fs::read_to_string(p).unwrap_or_default()
            } else if let Some(mut stderr) = child.stderr.take() {
                use std::io::Read;
                let mut buf = String::new();
                let _ = stderr.read_to_string(&mut buf);
                buf
            } else {
                String::new()
            };
            let trimmed = detail.trim();
            let msg = if !trimmed.is_empty() {
                trimmed
            } else {
                "PowerShell process terminated unexpectedly."
            };
            Err(format!("Game failed to start (exit status {}):\n\n{}", status, msg))
        }
        Ok(None) => {
            std::thread::spawn(|| {
                std::thread::sleep(std::time::Duration::from_millis(300));
                std::process::exit(0);
            });
            Ok(())
        }
        Err(e) => {
            Err(format!("Failed to monitor game process: {}", e))
        }
    }
}

#[tauri::command]
fn minimize_window(window: tauri::Window) {
    let _ = window.minimize();
}

#[tauri::command]
fn toggle_maximize_window(window: tauri::Window) {
    if let Ok(is_max) = window.is_maximized() {
        if is_max {
            let _ = window.unmaximize();
        } else {
            let _ = window.maximize();
        }
    }
}

#[tauri::command]
fn close_window(window: tauri::Window) {
    let _ = window.close();
    std::process::exit(0);
}

#[tauri::command]
fn is_window_maximized(window: tauri::Window) -> bool {
    window.is_maximized().unwrap_or(false)
}

/// Moves the window while the left mouse button is held. Tauri's native start_dragging()
/// returns Ok but does not move the window here (WebView2 keeps the mouse capture), so the
/// drag is driven manually: poll the cursor and move the window until the button is released.
#[tauri::command]
fn start_drag_window(window: tauri::Window) {
    #[cfg(windows)]
    {
        #[repr(C)]
        struct Pt { x: i32, y: i32 }
        #[repr(C)]
        struct Rc { l: i32, t: i32, r: i32, b: i32 }
        #[link(name = "user32")]
        extern "system" {
            fn GetCursorPos(p: *mut Pt) -> i32;
            fn GetAsyncKeyState(vk: i32) -> i16;
            fn GetWindowRect(h: isize, r: *mut Rc) -> i32;
            fn SetWindowPos(h: isize, after: isize, x: i32, y: i32, cx: i32, cy: i32, flags: u32) -> i32;
        }
        // Maximized windows are not dragged.
        if window.is_maximized().unwrap_or(false) {
            return;
        }
        let hwnd = match window.hwnd() {
            Ok(h) => h.0 as isize,
            Err(_) => return,
        };
        std::thread::spawn(move || unsafe {
            const VK_LBUTTON: i32 = 0x01;
            const SWP_NOSIZE_NOZORDER_NOACTIVATE: u32 = 0x0001 | 0x0004 | 0x0010;
            let mut start = Pt { x: 0, y: 0 };
            let mut rect = Rc { l: 0, t: 0, r: 0, b: 0 };
            if GetCursorPos(&mut start) == 0 || GetWindowRect(hwnd, &mut rect) == 0 {
                return;
            }
            let (off_x, off_y) = (rect.l - start.x, rect.t - start.y);
            // Only drag while the left button is actually down.
            while (GetAsyncKeyState(VK_LBUTTON) as u16 & 0x8000) != 0 {
                let mut cur = Pt { x: 0, y: 0 };
                if GetCursorPos(&mut cur) != 0 {
                    SetWindowPos(hwnd, 0, cur.x + off_x, cur.y + off_y, 0, 0, SWP_NOSIZE_NOZORDER_NOACTIVATE);
                }
                std::thread::sleep(std::time::Duration::from_millis(8));
            }
        });
    }
    #[cfg(not(windows))]
    {
        let _ = window.start_dragging();
    }
}

fn process_failed_kind_to_str(kind: i32) -> &'static str {
    match kind {
        0 => "BROWSER_PROCESS_EXITED",
        1 => "RENDER_PROCESS_EXITED",
        2 => "RENDER_PROCESS_UNRESPONSIVE",
        3 => "FRAME_RENDER_PROCESS_EXITED",
        4 => "UTILITY_PROCESS_EXITED",
        5 => "SANDBOX_HELPER_PROCESS_EXITED",
        6 => "GPU_PROCESS_EXITED",
        7 => "PPAPI_PLUGIN_PROCESS_EXITED",
        8 => "PPAPI_BROKER_PROCESS_EXITED",
        9 => "UNKNOWN_PROCESS_EXITED",
        _ => "OTHER",
    }
}

pub fn is_dev_or_debug() -> bool {
    DEV_MODE_RUNTIME.load(Ordering::Relaxed)
        || cfg!(debug_assertions)
        || cfg!(feature = "dev-mode")
        || std::env::args().any(|a| {
            a.eq_ignore_ascii_case("--debug")
                || a.eq_ignore_ascii_case("--dev")
                || a.eq_ignore_ascii_case("-debug")
                || a.eq_ignore_ascii_case("-dev")
        })
}

#[tauri::command]
fn is_dev_mode() -> bool {
    is_dev_or_debug()
}

#[tauri::command]
fn set_dev_mode(enabled: bool) {
    DEV_MODE_RUNTIME.store(enabled, Ordering::Relaxed);
}

pub fn write_debug_log(msg: &str) {
    if !is_dev_or_debug() {
        return;
    }
    let path = std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(|d| d.join("launcher-debug.log")))
        .unwrap_or_else(|| std::path::PathBuf::from("launcher-debug.log"));
    if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(&path) {
        use std::io::Write;
        let ts = chrono::Local::now().format("%Y-%m-%d %H:%M:%S%.3f");
        let _ = writeln!(f, "[{}] [RUST] {}", ts, msg);
        let _ = f.flush();
    }
}

/// Queries recent Application Error / WER events (last 2 minutes) and writes the
/// faulting module info into launcher-debug.log. Also records where WebView2 logs live.
fn collect_crash_events() {
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        let query = "*[System[(Level=2) and TimeCreated[timediff(@SystemTime) <= 120000]]]";
        match Command::new("wevtutil")
            .args(["qe", "Application", &format!("/q:{}", query), "/c:6", "/rd:true", "/f:text"])
            .creation_flags(0x08000000)
            .output()
        {
            Ok(out) => {
                let text = String::from_utf8_lossy(&out.stdout);
                let mut kept = 0;
                for line in text.lines() {
                    let l = line.trim();
                    if l.is_empty() { continue; }
                    write_debug_log(&format!("[CRASH_EVENT] {}", l));
                    kept += 1;
                    if kept > 120 { break; }
                }
                if kept == 0 {
                    write_debug_log("[CRASH_EVENT] No Application error events found in last 2 minutes");
                }
            }
            Err(e) => write_debug_log(&format!("[CRASH_EVENT] wevtutil failed: {}", e)),
        }
        if let Ok(local) = std::env::var("LOCALAPPDATA") {
            write_debug_log(&format!("[CRASH_EVENT] LOCALAPPDATA={} (check <identifier>\\EBWebView for logs/Crashpad dumps)", local));
        }
    }
}

/// Issue #6: overlay hooks (MSI Afterburner / RTSS) crash the WebView2 browser process
/// right after startup. The WebView cannot be recovered in-process, so either tell the
/// user how to fix it (overlay detected) or relaunch the launcher a limited number of times.
fn handle_browser_process_exit() {
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        std::thread::sleep(std::time::Duration::from_millis(500));

        let overlay = ["RTSS.exe", "MSIAfterburner.exe", "RTSSHooksLoader64.exe"].iter().find(|name| {
            Command::new("tasklist")
                .args(["/FI", &format!("IMAGENAME eq {}", name), "/NH"])
                .creation_flags(0x08000000)
                .output()
                .map(|o| String::from_utf8_lossy(&o.stdout).to_lowercase().contains(&name.to_lowercase()))
                .unwrap_or(false)
        });

        if let Some(name) = overlay {
            write_debug_log(&format!("[RECOVERY] Overlay process detected: {}. Showing guidance.", name));
            let text = r#"Majistic Launcher was closed because MSI Afterburner / RTSS (RivaTuner Statistics Server) is running and conflicts with the launcher's web UI.

To fix this (one time only):
1. Open RTSS (tray icon) and click 'Add' to add these two programs:
   - Majistic Launcher.exe (in the launcher folder)
   - msedgewebview2.exe (usually at: C:\Program Files (x86)\Microsoft\EdgeWebView\Application\<version>\msedgewebview2.exe)
2. For each one, set 'Application detection level' to 'None'.
3. Open the launcher again.

RTSS will still work normally in the game itself; these two exclusions only affect the launcher window.

Alternatively, close MSI Afterburner / RTSS before opening the launcher."#;
            message_box("Majistic Launcher - RTSS / MSI Afterburner", text);
            std::process::exit(1);
        }

        let attempt: u32 = std::env::var("MAJISTIC_RELAUNCH").ok().and_then(|v| v.parse().ok()).unwrap_or(0);
        if attempt >= 2 {
            write_debug_log("[RECOVERY] Browser process exited again; giving up after 2 relaunches.");
            message_box(
                "Majistic Launcher",
                "The launcher UI failed repeatedly (WebView2 process exited). Close overlay / monitoring software (MSI Afterburner, RTSS, etc.) and try again. See launcher-debug.log for details.",
            );
            std::process::exit(1);
        }
        write_debug_log(&format!("[RECOVERY] Relaunching launcher (attempt {}/2)...", attempt + 1));
        if let Ok(exe) = std::env::current_exe() {
            let _ = Command::new(exe).env("MAJISTIC_RELAUNCH", (attempt + 1).to_string()).spawn();
        }
        std::process::exit(0);
    }
}

#[cfg(windows)]
fn message_box(title: &str, text: &str) {
    use windows_sys::Win32::UI::WindowsAndMessaging::{MessageBoxW, MB_ICONWARNING, MB_OK, MB_TOPMOST};
    let t: Vec<u16> = title.encode_utf16().chain(std::iter::once(0)).collect();
    let m: Vec<u16> = text.encode_utf16().chain(std::iter::once(0)).collect();
    unsafe {
        MessageBoxW(std::ptr::null_mut(), m.as_ptr(), t.as_ptr(), MB_ICONWARNING | MB_OK | MB_TOPMOST);
    }
}

#[tauri::command]
fn log_message(msg: String) {
    if !is_dev_or_debug() {
        return;
    }
    let path = std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(|d| d.join("launcher-debug.log")))
        .unwrap_or_else(|| std::path::PathBuf::from("launcher-debug.log"));
    if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(path) {
        use std::io::Write;
        let ts = chrono::Local::now().format("%Y-%m-%d %H:%M:%S%.3f");
        let _ = writeln!(f, "[{}] [JS] {}", ts, msg);
        let _ = f.flush();
    }
}

#[tauri::command]
fn exit_app() {
    std::process::exit(0);
}

#[tauri::command]
fn open_external_url(url: String) -> Result<(), String> {
    if !url.starts_with("https://") && !url.starts_with("http://") {
        return Err("Invalid URL protocol".to_string());
    }
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        const CREATE_NO_WINDOW: u32 = 0x08000000;
        std::process::Command::new("cmd")
            .args(["/C", "start", "", &url])
            .creation_flags(CREATE_NO_WINDOW)
            .spawn()
            .map_err(|e| format!("Failed to open URL: {}", e))?;
    }
    #[cfg(not(windows))]
    {
        std::process::Command::new("xdg-open")
            .arg(&url)
            .spawn()
            .map_err(|e| format!("Failed to open URL: {}", e))?;
    }
    Ok(())
}

#[tauri::command]
fn get_connected_gamepads() -> Vec<String> {
    let mut gamepads: Vec<String> = Vec::new();

    #[cfg(windows)]
    {
        use std::ffi::c_void;

        #[repr(C)]
        struct RAWINPUTDEVICELIST {
            h_device: *mut c_void,
            dw_type: u32,
            _pad: u32,
        }

        #[repr(C)]
        struct RID_DEVICE_INFO_HID {
            dw_vendor_id: u32,
            dw_product_id: u32,
            dw_version_number: u32,
            us_usage_page: u16,
            us_usage: u16,
        }

        #[repr(C)]
        struct RID_DEVICE_INFO {
            cb_size: u32,
            dw_type: u32,
            hid: RID_DEVICE_INFO_HID,
            _pad: [u8; 8],
        }

        extern "system" {
            fn GetRawInputDeviceList(
                p_raw_input_device_list: *mut RAWINPUTDEVICELIST,
                pui_num_devices: *mut u32,
                cb_size: u32,
            ) -> u32;

            fn GetRawInputDeviceInfoW(
                h_device: *mut c_void,
                ui_command: u32,
                p_data: *mut c_void,
                pcb_size: *mut u32,
            ) -> u32;
        }

        let mut num_devices: u32 = 0;
        let list_item_size = std::mem::size_of::<RAWINPUTDEVICELIST>() as u32;
        let mut xinput_raw_count: usize = 0;

        unsafe {
            if GetRawInputDeviceList(std::ptr::null_mut(), &mut num_devices, list_item_size) != u32::MAX
                && num_devices > 0
            {
                let mut list: Vec<RAWINPUTDEVICELIST> = Vec::with_capacity(num_devices as usize);
                list.set_len(num_devices as usize);
                let count = GetRawInputDeviceList(list.as_mut_ptr(), &mut num_devices, list_item_size);
                if count != u32::MAX {
                    for dev in list.iter().take(count as usize) {
                        if dev.dw_type == 2 {
                            // RIM_TYPEHID
                            let mut info: RID_DEVICE_INFO = std::mem::zeroed();
                            info.cb_size = std::mem::size_of::<RID_DEVICE_INFO>() as u32;
                            let mut info_size = info.cb_size;

                            let res = GetRawInputDeviceInfoW(
                                dev.h_device,
                                0x2000000b, // RIDI_DEVICEINFO
                                &mut info as *mut _ as *mut c_void,
                                &mut info_size,
                            );

                            if res != u32::MAX {
                                let page = info.hid.us_usage_page;
                                let usage = info.hid.us_usage;
                                if page == 1 && (usage == 4 || usage == 5 || usage == 8) {
                                    let mut name_size: u32 = 0;
                                    let mut is_xinput = false;
                                    GetRawInputDeviceInfoW(
                                        dev.h_device,
                                        0x20000007, // RIDI_DEVICENAME
                                        std::ptr::null_mut(),
                                        &mut name_size,
                                    );
                                    if name_size > 0 {
                                        let mut name_buf: Vec<u16> = Vec::with_capacity(name_size as usize);
                                        name_buf.set_len(name_size as usize);
                                        if GetRawInputDeviceInfoW(
                                            dev.h_device,
                                            0x20000007,
                                            name_buf.as_mut_ptr() as *mut c_void,
                                            &mut name_size,
                                        ) != u32::MAX
                                        {
                                            let path_str = String::from_utf16_lossy(&name_buf);
                                            let upper = path_str.to_uppercase();
                                            if upper.contains("&IG_") || info.hid.dw_vendor_id == 0x045E {
                                                is_xinput = true;
                                                xinput_raw_count += 1;
                                            }
                                        }
                                    }

                                    let vid = info.hid.dw_vendor_id;
                                    let pid = info.hid.dw_product_id;
                                    let name = match vid {
                                        0x3537 => match pid {
                                            0x1040 => "GameSir T4 Nova Lite".to_string(),
                                            0x1005 => "GameSir G7".to_string(),
                                            0x1008 => "GameSir G7 SE".to_string(),
                                            0x1010..=0x1012 => "GameSir T4 Pro".to_string(),
                                            0x1025 => "GameSir Cyclone".to_string(),
                                            _ => "GameSir Controller".to_string(),
                                        },
                                        0x054C => match pid {
                                            0x09CC | 0x05C4 | 0x0BA0 => {
                                                "Sony DualShock 4 Wireless Controller".to_string()
                                            }
                                            0x0CE6 | 0x0DF2 => {
                                                "Sony DualSense Wireless Controller (PS5)".to_string()
                                            }
                                            0x0268 => "Sony DualShock 3 Controller".to_string(),
                                            _ => "PlayStation Controller".to_string(),
                                        },
                                        0x045E => match pid {
                                            0x028E | 0x028F | 0x0288 => "Xbox 360 Controller".to_string(),
                                            0x02A1 | 0x0719 | 0x0291 | 0x02A9 | 0x0219 => {
                                                "Xbox 360 Wireless Controller".to_string()
                                            }
                                            0x02D1 | 0x02DD | 0x02EA | 0x02FD | 0x0B12 | 0x0B13
                                            | 0x0B20 | 0x0B22 => {
                                                "Xbox Wireless Controller".to_string()
                                            }
                                            0x02E3 | 0x0B00 | 0x0B05 => {
                                                "Xbox Elite Wireless Controller".to_string()
                                            }
                                            0x0B0A | 0x0B21 => {
                                                "Xbox Adaptive Controller".to_string()
                                            }
                                            _ => "Xbox Controller".to_string(),
                                        },
                                        0x057E => match pid {
                                            0x2009 => "Nintendo Switch Pro Controller".to_string(),
                                            0x2006 | 0x2007 => {
                                                "Nintendo Switch Joy-Con".to_string()
                                            }
                                            _ => "Nintendo Controller".to_string(),
                                        },
                                        0x2DC8 | 0x05A0 => "8BitDo Controller".to_string(),
                                        0x0F0D => "Hori Controller".to_string(),
                                        0x1532 => "Razer Controller".to_string(),
                                        0x1038 => "SteelSeries Controller".to_string(),
                                        0x24C6 | 0x0E6F => "PowerA / PDP Controller".to_string(),
                                        0x2563 | 0x11C0 => "Flydigi Controller".to_string(),
                                        0x20BC => "EasySMX Controller".to_string(),
                                        0x28DE => "Steam Controller".to_string(),
                                        _ => {
                                            if is_xinput {
                                                "Xbox Controller".to_string()
                                            } else if usage == 5 {
                                                "Wireless / USB Controller".to_string()
                                            } else {
                                                "Joystick Controller".to_string()
                                            }
                                        }
                                    };
                                    if !gamepads.contains(&name) {
                                        gamepads.push(name);
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Also check XInput controllers (ports 0..3)
            // Only add an Xbox Controller entry if there are active XInput ports that
            // were not already mapped/accounted for by the RawInput &IG_ enumeration.
            type PfnXInputGetState = unsafe extern "system" fn(u32, *mut u8) -> u32;

            for dll_name in [
                b"xinput1_4.dll\0".as_ptr(),
                b"xinput1_3.dll\0".as_ptr(),
                b"xinput9_1_0.dll\0".as_ptr(),
            ] {
                let h_mod = windows_sys::Win32::System::LibraryLoader::LoadLibraryA(dll_name);
                if !h_mod.is_null() {
                    let proc = windows_sys::Win32::System::LibraryLoader::GetProcAddress(
                        h_mod,
                        b"XInputGetState\0".as_ptr(),
                    );
                    if let Some(pfn) = proc {
                        let get_state: PfnXInputGetState = std::mem::transmute(pfn);
                        let mut state = [0u8; 16];
                        let mut xinput_active_ports: usize = 0;
                        for port in 0..4 {
                            if get_state(port, state.as_mut_ptr()) == 0 {
                                xinput_active_ports += 1;
                            }
                        }

                        if xinput_active_ports > xinput_raw_count {
                            let unmapped = xinput_active_ports - xinput_raw_count;
                            for _ in 0..unmapped {
                                let name = "Xbox Controller".to_string();
                                if !gamepads.contains(&name) {
                                    gamepads.push(name);
                                }
                            }
                        }
                    }
                    windows_sys::Win32::Foundation::FreeLibrary(h_mod);
                    break;
                }
            }
        }

        // Post-processing deduplication:
        // 1. If specific Xbox models are present, remove generic "Xbox Controller"
        if gamepads.iter().any(|g| g.starts_with("Xbox ") && g != "Xbox Controller") {
            gamepads.retain(|g| g != "Xbox Controller");
        }
        // 2. If specific Sony models are present, remove generic "PlayStation Controller"
        if gamepads.iter().any(|g| g.starts_with("Sony ") && g != "PlayStation Controller") {
            gamepads.retain(|g| g != "PlayStation Controller");
        }
        // 3. If specific GameSir models are present, remove generic "GameSir Controller"
        if gamepads.iter().any(|g| g.starts_with("GameSir ") && g != "GameSir Controller") {
            gamepads.retain(|g| g != "GameSir Controller");
        }
        // 4. If any named controller is present, remove generic fallbacks
        if gamepads.iter().any(|g| g != "Wireless / USB Controller" && g != "Joystick Controller") {
            gamepads.retain(|g| g != "Wireless / USB Controller" && g != "Joystick Controller");
        }
    }

    gamepads
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_updater::Builder::new().build())
        .on_window_event(|window, event| {
            if let tauri::WindowEvent::CloseRequested { .. } | tauri::WindowEvent::Destroyed = event {
                if window.label() == "main" {
                    std::process::exit(0);
                }
            }
        })
        .setup(|app| {
            if let Ok(loaded) = load_settings() {
                if loaded.dev_mode {
                    DEV_MODE_RUNTIME.store(true, Ordering::Relaxed);
                }
            }
            write_debug_log("=== Setup Hook Entered ===");
            write_debug_log(&format!("OS Architecture: {}", std::env::consts::ARCH));

            #[cfg(windows)]
            {
                use tauri::Manager;
                if let Some(main_window) = app.get_webview_window("main") {
                    write_debug_log("Main WebviewWindow found. Initializing telemetry and listeners...");

                    if let Ok(hwnd) = main_window.hwnd() {
                        unsafe {
                            use windows_sys::Win32::Graphics::Dwm::{
                                DwmSetWindowAttribute,
                                DWMWA_WINDOW_CORNER_PREFERENCE, DWMWCP_DONOTROUND,
                                DWMWA_BORDER_COLOR, DWMWA_COLOR_NONE,
                                DWMWA_USE_IMMERSIVE_DARK_MODE,
                            };

                            // 1. Tell DWM not to draw any native rounded system corners
                            let do_not_round = DWMWCP_DONOTROUND;
                            let _ = DwmSetWindowAttribute(
                                hwnd.0 as _,
                                DWMWA_WINDOW_CORNER_PREFERENCE as u32,
                                &do_not_round as *const _ as *const _,
                                std::mem::size_of_val(&do_not_round) as u32,
                            );

                            // 2. Remove the Windows 11 1px native outer system border entirely
                            let border_none = DWMWA_COLOR_NONE;
                            let res_border = DwmSetWindowAttribute(
                                hwnd.0 as _,
                                DWMWA_BORDER_COLOR as u32,
                                &border_none as *const _ as *const _,
                                std::mem::size_of_val(&border_none) as u32,
                            );
                            write_debug_log(&format!("DwmSetWindowAttribute BORDER_COLOR (NONE) result: 0x{:X}", res_border));

                            // 3. Enable immersive dark mode
                            let dark_mode: i32 = 1;
                            let _ = DwmSetWindowAttribute(
                                hwnd.0 as _,
                                DWMWA_USE_IMMERSIVE_DARK_MODE as u32,
                                &dark_mode as *const _ as *const _,
                                std::mem::size_of_val(&dark_mode) as u32,
                            );
                        }
                    }

                    // Watchdog thread: checks window state periodically (dev-mode only)
                    if is_dev_or_debug() {
                        let window_for_watchdog = main_window.clone();
                        std::thread::spawn(move || {
                            let mut tick: u64 = 0;
                            loop {
                                std::thread::sleep(std::time::Duration::from_secs(3));
                                tick += 1;
                                let is_visible = window_for_watchdog.is_visible().unwrap_or(false);
                                let is_minimized = window_for_watchdog.is_minimized().unwrap_or(false);
                                let is_focused = window_for_watchdog.is_focused().unwrap_or(false);
                                if tick % 4 == 0 || !is_visible || is_minimized {
                                    write_debug_log(&format!(
                                        "[Watchdog Heartbeat #{} ({}s)] window: visible={} minimized={} focused={}",
                                        tick, tick * 3, is_visible, is_minimized, is_focused
                                    ));
                                }
                            }
                        });
                    }

                    let _ = main_window.with_webview(move |webview| {
                        unsafe {
                            use webview2_com::Microsoft::Web::WebView2::Win32::*;
                            use webview2_com::{ProcessFailedEventHandler, AcceleratorKeyPressedEventHandler, NavigationCompletedEventHandler};
                            use windows_core::Interface;

                            let controller = webview.controller();
                            let environment = webview.environment();

                            if let Ok(c2) = controller.cast::<ICoreWebView2Controller2>() {
                                let _ = c2.SetDefaultBackgroundColor(COREWEBVIEW2_COLOR { A: 0, R: 0, G: 0, B: 0 });
                            }

                            let mut ver = windows_core::PWSTR::null();
                            if environment.BrowserVersionString(&mut ver).is_ok() && !ver.is_null() {
                                write_debug_log(&format!("CoreWebView2 Runtime Version: {}", ver.to_string().unwrap_or_default()));
                            }

                            if let Ok(core) = controller.CoreWebView2() {
                                let core_for_reload = core.clone();
                                let core_for_f5 = core.clone();

                                // 1. ProcessFailed Listener (covers all processes: GPU, Renderer, Utility, etc.)
                                let handler = ProcessFailedEventHandler::create(Box::new(move |_sender, args| {
                                    if let Some(args) = args {
                                        let mut kind = COREWEBVIEW2_PROCESS_FAILED_KIND(0);
                                        let _ = args.ProcessFailedKind(&mut kind);
                                        let kind_str = process_failed_kind_to_str(kind.0);
                                        let mut exit_code = 0i32;
                                        let mut desc_str = String::new();
                                        if let Ok(args2) = args.cast::<ICoreWebView2ProcessFailedEventArgs2>() {
                                            let mut code = 0i32;
                                            let _ = args2.ExitCode(&mut code);
                                            exit_code = code;
                                            let mut desc = windows_core::PWSTR::null();
                                            if args2.ProcessDescription(&mut desc).is_ok() && !desc.is_null() {
                                                desc_str = desc.to_string().unwrap_or_default();
                                            }
                                        }
                                        write_debug_log(&format!(
                                            "[PROCESS_FAILED] kind={} ({}) exit_code={} desc='{}'",
                                            kind.0, kind_str, exit_code, desc_str
                                        ));
                                        // Issue #6: capture the Windows Application event log
                                        // (faulting module / WER data) for this crash in dev mode.
                                        if is_dev_or_debug() {
                                            std::thread::spawn(|| {
                                                std::thread::sleep(std::time::Duration::from_secs(3));
                                                collect_crash_events();
                                            });
                                        }
                                        // Browser process died (kind 0): the WebView is unrecoverable.
                                        if kind.0 == 0 {
                                            std::thread::spawn(handle_browser_process_exit);
                                        }
                                        // Auto-recovery: If renderer (1/2/3) or GPU (6) exited, trigger reload
                                        if kind.0 == 1 || kind.0 == 2 || kind.0 == 3 || kind.0 == 6 {
                                            write_debug_log("[PROCESS_FAILED] Triggering CoreWebView2.Reload() auto-recovery...");
                                            let res = core_for_reload.Reload();
                                            write_debug_log(&format!("[PROCESS_FAILED] Reload() returned: {:?}", res));
                                        } else {
                                            write_debug_log(&format!("[PROCESS_FAILED] Informational notification for kind={} (no action taken)", kind.0));
                                        }
                                    }
                                    Ok(())
                                }));
                                let mut token = 0i64;
                                let _ = core.add_ProcessFailed(&handler, &mut token);

                                // 2. AcceleratorKeyPressed (Native Win32 key interception inside webview)
                                let acc_handler = AcceleratorKeyPressedEventHandler::create(Box::new(move |_ctrl, args| {
                                    if let Some(args) = args {
                                        let mut vk = 0u32;
                                        let _ = args.VirtualKey(&mut vk);
                                        let mut key_event = COREWEBVIEW2_KEY_EVENT_KIND(0);
                                        let _ = args.KeyEventKind(&mut key_event);
                                        // KeyDown is 0, SysKeyDown is 2
                                        if key_event.0 == 0 || key_event.0 == 2 {
                                            write_debug_log(&format!("[NATIVE_KEY] KeyDown vk=0x{:X} ({})", vk, vk));
                                            if vk == 0x74 { // VK_F5
                                                write_debug_log("[NATIVE_KEY] F5 key detected! Forcing native CoreWebView2.Reload()...");
                                                let res = core_for_f5.Reload();
                                                write_debug_log(&format!("[NATIVE_KEY] Reload() returned: {:?}", res));
                                                let _ = args.SetHandled(true);
                                            }
                                        }
                                    }
                                    Ok(())
                                }));
                                let mut acc_token = 0i64;
                                let _ = controller.add_AcceleratorKeyPressed(&acc_handler, &mut acc_token);

                                // 3. NavigationCompleted Listener
                                let controller_for_nav = controller.clone();
                                let nav_handler = NavigationCompletedEventHandler::create(Box::new(move |_sender, args| {
                                    if let Some(args) = args {
                                        let mut is_success = windows_core::BOOL(0);
                                        let _ = args.IsSuccess(&mut is_success);
                                        let mut status = COREWEBVIEW2_WEB_ERROR_STATUS(0);
                                        let _ = args.WebErrorStatus(&mut status);
                                        write_debug_log(&format!(
                                            "[NAVIGATION_COMPLETED] is_success={} status={}",
                                            is_success.as_bool(), status.0
                                        ));
                                        // Synchronize controller bounds and visibility with DWM swapchain
                                        let _ = controller_for_nav.NotifyParentWindowPositionChanged();
                                        let _ = controller_for_nav.SetIsVisible(true);
                                    }
                                    Ok(())
                                }));
                                let mut nav_token = 0i64;
                                let _ = core.add_NavigationCompleted(&nav_handler, &mut nav_token);
                            }
                        }
                    });
                }
            }

            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            load_settings,
            save_settings,
            check_game_status,
            get_available_languages,
            select_iso_file,
            select_game_data_folder,
            start_game_build,
            precompile_shaders,
            get_shader_cache_info,
            clean_shader_cache,
            launch_game,
            minimize_window,
            toggle_maximize_window,
            close_window,
            is_window_maximized,
            start_drag_window,
            log_message,
            is_dev_mode,
            set_dev_mode,
            exit_app,
            open_external_url,
            get_connected_gamepads,
            saves::list_save_profiles,
            saves::create_save_profile,
            saves::duplicate_save_profile,
            saves::rename_save_profile,
            saves::set_active_save_profile,
            saves::delete_save_profile,
            saves::export_save_profile,
            saves::pick_and_inspect_savistic,
            saves::import_save_profile,
            saves::open_saves_folder
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_get_connected_gamepads() {
        let pads = get_connected_gamepads();
        println!("Connected gamepads: {:?}", pads);
    }

    #[test]
    fn test_deserialize_settings_with_null_fov_scale() {
        let json = r#"{
            "WindowSize": "1920x1080",
            "Fullscreen": false,
            "RenderScale": 2,
            "RenderingPath": "Default",
            "FrameRateMode": "60",
            "VSync": true,
            "PostEffect": "fxaa",
            "Upscaler": "fsr",
            "SkipIntroLogos": true,
            "SkipCutscenes": true,
            "CutsceneSpeed": 4,
            "InputMode": "Controller",
            "MouseSensitivity": null,
            "RecordPerformance": true,
            "GameDataRoot": "game-data",
            "Configuration": "Release",
            "Language": "English",
            "StepFix": false,
            "AnimSpeedFix": true,
            "DtSmoothing": false,
            "FovScale": null,
            "DrawDistanceScale": null,
            "PauseBlur": null,
            "DepthOfField": true,
            "Bloom": true
        }"#;

        let parsed: Result<Settings, _> = serde_json::from_str(json);
        assert!(parsed.is_ok(), "Failed to deserialize settings with null fields: {:?}", parsed.err());
        let settings = parsed.unwrap();
        assert_eq!(settings.fov_scale, 1.0);
        assert_eq!(settings.draw_distance_scale, 1.0);
        assert_eq!(settings.mouse_sensitivity, 1.0);
        assert!(!settings.pause_blur);
        assert!(settings.pause_distortion);
        assert_eq!(settings.bloom_intensity, 1.0);
        assert_eq!(settings.cas_sharpness, 0.0);
        assert_eq!(settings.fsr_reduction, 0.0);
        assert!(!settings.window_fps);
        assert_eq!(settings.shadow_slope_bias, 0.30);
        assert_eq!(settings.shadow_constant_bias, 0.0);
        assert_eq!(settings.shadow_bias_clamp, 0.002);
        assert!(!settings.dev_mode);
    }

    #[test]
    fn test_deserialize_settings_with_missing_fields() {
        let json = r#"{
            "WindowSize": "1280x720",
            "Fullscreen": true
        }"#;

        let parsed: Result<Settings, _> = serde_json::from_str(json);
        assert!(parsed.is_ok(), "Failed to deserialize partial settings: {:?}", parsed.err());
        let settings = parsed.unwrap();
        assert_eq!(settings.window_size, "1280x720");
        assert!(settings.fullscreen);
        assert_eq!(settings.render_scale, 1);
        assert_eq!(settings.fov_scale, 1.0);
        assert_eq!(settings.draw_distance_scale, 3.0);
        assert_eq!(settings.cutscene_speed, 4);
        assert_eq!(settings.language, "English");
        assert!(!settings.step_fix);
        assert!(!settings.anim_speed_fix);
        assert!(!settings.present_dither);
    }

    #[test]
    fn test_zip_deflate_support() {
        use std::io::Write;
        let mut buf = Vec::new();
        {
            let mut writer = zip::ZipWriter::new(std::io::Cursor::new(&mut buf));
            let options = zip::write::SimpleFileOptions::default()
                .compression_method(zip::CompressionMethod::Deflated);
            writer.start_file("test.txt", options).unwrap();
            writer.write_all(b"deflate test data").unwrap();
            writer.finish().unwrap();
        }

        let cursor = std::io::Cursor::new(buf);
        let mut archive = zip::ZipArchive::new(cursor).unwrap();
        let mut file = archive.by_name("test.txt").unwrap();
        let mut content = String::new();
        std::io::Read::read_to_string(&mut file, &mut content).unwrap();
        assert_eq!(content, "deflate test data");
    }

    #[test]
    fn test_deserialize_valid_settings() {
        let json = r#"{
            "WindowSize": "2560x1440",
            "Fullscreen": true,
            "RenderScale": 3,
            "ShadowScale": 4,
            "ShadowSlopeBias": 0.45,
            "ShadowConstantBias": 0.005,
            "ShadowBiasClamp": 0.004,
            "RenderingPath": "Vulkan",
            "FrameRateMode": "120",
            "VSync": false,
            "PostEffect": "cas",
            "Upscaler": "none",
            "SkipIntroLogos": false,
            "SkipCutscenes": false,
            "CutsceneSpeed": 2,
            "InputMode": "KeyboardMouse",
            "MouseSensitivity": 1.5,
            "RecordPerformance": false,
            "GameDataRoot": "game-data",
            "Configuration": "Release",
            "Language": "Japanese",
            "StepFix": true,
            "AnimSpeedFix": false,
            "DtSmoothing": true,
            "FovScale": 1.2,
            "DrawDistanceScale": 2.25,
            "PauseBlur": false,
            "PauseDistortion": true,
            "DepthOfField": false,
            "Bloom": true,
            "BloomIntensity": 1.5,
            "CasSharpness": 0.35,
            "FsrReduction": 0.45,
            "WindowFps": true,
            "PresentDither": true,
            "DevMode": true
        }"#;

        let parsed: Result<Settings, _> = serde_json::from_str(json);
        assert!(parsed.is_ok(), "Failed to deserialize valid settings: {:?}", parsed.err());
        let settings = parsed.unwrap();
        assert_eq!(settings.window_size, "2560x1440");
        assert!(settings.fullscreen);
        assert_eq!(settings.render_scale, 3);
        assert_eq!(settings.shadow_scale, 4);
        assert_eq!(settings.shadow_slope_bias, 0.45);
        assert_eq!(settings.shadow_constant_bias, 0.005);
        assert_eq!(settings.shadow_bias_clamp, 0.004);
        assert_eq!(settings.rendering_path, "Vulkan");
        assert_eq!(settings.frame_rate_mode, "120");
        assert!(!settings.vsync);
        assert_eq!(settings.fov_scale, 1.2);
        assert_eq!(settings.draw_distance_scale, 2.25);
        assert_eq!(settings.mouse_sensitivity, 1.5);
        assert_eq!(settings.language, "Japanese");
        assert!(settings.step_fix);
        assert!(!settings.anim_speed_fix);
        assert!(settings.dt_smoothing);
        assert!(!settings.pause_blur);
        assert!(settings.pause_distortion);
        assert_eq!(settings.bloom_intensity, 1.5);
        assert_eq!(settings.cas_sharpness, 0.35);
        assert_eq!(settings.fsr_reduction, 0.45);
        assert!(settings.window_fps);
        assert!(settings.present_dither);
        assert!(settings.dev_mode);
    }

    #[test]
    fn test_format_bytes() {
        assert_eq!(format_bytes(500), "500 B");
        assert_eq!(format_bytes(1024), "1.0 KB");
        assert_eq!(format_bytes(1536), "1.5 KB");
        assert_eq!(format_bytes(1048576), "1.00 MB");
        assert_eq!(format_bytes(21551962), "20.55 MB");
    }

    #[test]
    fn test_deserialize_language_audio_and_iso_path() {
        let json_missing = r#"{ "Language": "Portuguese" }"#;
        let s1: Settings = serde_json::from_str(json_missing).unwrap();
        assert_eq!(s1.language, "Portuguese");
        assert_eq!(s1.audio_language, "Auto");
        assert_eq!(s1.iso_path, "");

        let json_nulls = r#"{ "Language": null, "AudioLanguage": null, "IsoPath": null }"#;
        let s2: Settings = serde_json::from_str(json_nulls).unwrap();
        assert_eq!(s2.language, "English");
        assert_eq!(s2.audio_language, "Auto");
        assert_eq!(s2.iso_path, "");

        let json_explicit = r#"{ "Language": "Portuguese", "AudioLanguage": "English", "IsoPath": "D:\\Games\\Majin.iso" }"#;
        let s3: Settings = serde_json::from_str(json_explicit).unwrap();
        assert_eq!(s3.language, "Portuguese");
        assert_eq!(s3.audio_language, "English");
        assert_eq!(s3.iso_path, "D:\\Games\\Majin.iso");
    }

    #[test]
    fn test_detect_available_languages_pal_and_usa() {
        let base = std::env::temp_dir().join(format!("majin_lang_test_{}", std::process::id()));
        let _ = fs::remove_dir_all(&base);

        // Mock PAL edition: has kr.pak leftover on disc, plus us/po/du/ru, no jp
        let pal_dir = base.join("pal");
        let pal_pkg = pal_dir.join("finalizedXBox360/KB/package");
        let pal_snd = pal_dir.join("finalizedXBox360/KB/sound/xbox360");
        fs::create_dir_all(&pal_pkg).unwrap();
        fs::write(pal_dir.join("default.xex"), [0u8, 0, 0x67, 0xBD, 0x87, 0xEF, 0, 0]).unwrap();
        for p in ["us", "po", "sp", "fr", "ge", "it", "du", "ru", "kr"] {
            fs::write(pal_pkg.join(format!("{}.pak", p)), b"pak").unwrap();
        }
        for s in ["en", "po", "sp", "fr", "ge", "it", "du", "ru"] {
            let d = pal_snd.join(format!("rtd_{}", s));
            fs::create_dir_all(&d).unwrap();
            fs::write(d.join("000.gtd"), b"gtd").unwrap();
        }

        let pal_info = detect_available_languages_in_dir(&pal_dir);
        assert_eq!(pal_info.edition_code, "PAL");
        assert!(pal_info.detected_from_assets);
        let pal_texts: Vec<&str> = pal_info.text_languages.iter().map(|l| l.value.as_str()).collect();
        assert_eq!(pal_texts, vec!["English", "Portuguese", "Spanish", "French", "German", "Italian", "Dutch", "Russian"]);
        assert!(!pal_texts.contains(&"Korean"));
        assert!(!pal_texts.contains(&"Japanese"));
        let pal_audios: Vec<&str> = pal_info.audio_languages.iter().map(|l| l.value.as_str()).collect();
        assert_eq!(pal_audios, vec!["English", "Portuguese", "Spanish", "French", "German", "Italian", "Dutch", "Russian"]);

        // Mock USA edition: only EN/FR/SP/KR text and EN/FR/SP audio
        let usa_dir = base.join("usa");
        let usa_pkg = usa_dir.join("finalizedXBox360/KB/package");
        let usa_snd = usa_dir.join("finalizedXBox360/KB/sound/xbox360");
        fs::create_dir_all(&usa_pkg).unwrap();
        fs::write(usa_dir.join("default.xex"), [0u8, 0, 0x76, 0xFD, 0xB6, 0x42, 0, 0]).unwrap();
        for p in ["us", "fr", "sp", "kr", "po"] {
            fs::write(usa_pkg.join(format!("{}.pak", p)), b"pak").unwrap();
        }
        for s in ["en", "fr", "sp"] {
            let d = usa_snd.join(format!("rtd_{}", s));
            fs::create_dir_all(&d).unwrap();
            fs::write(d.join("000.gtd"), b"gtd").unwrap();
        }

        let usa_info = detect_available_languages_in_dir(&usa_dir);
        assert_eq!(usa_info.edition_code, "USA");
        let usa_texts: Vec<&str> = usa_info.text_languages.iter().map(|l| l.value.as_str()).collect();
        assert_eq!(usa_texts, vec!["English", "Spanish", "French", "Korean"]);
        let usa_audios: Vec<&str> = usa_info.audio_languages.iter().map(|l| l.value.as_str()).collect();
        assert_eq!(usa_audios, vec!["English", "Spanish", "French"]);

        // Uninstalled fallback
        let empty_info = detect_available_languages_in_dir(&base.join("missing"));
        assert_eq!(empty_info.edition_code, "NotInstalled");
        assert!(!empty_info.detected_from_assets);
        assert_eq!(empty_info.text_languages.len(), 1);
        assert_eq!(empty_info.text_languages[0].value, "English");

        let _ = fs::remove_dir_all(&base);
    }
}
