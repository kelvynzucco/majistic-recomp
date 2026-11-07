use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct Settings {
    pub window_size: String,
    pub fullscreen: bool,
    pub render_scale: u32,
    pub rendering_path: String,
    pub frame_rate_mode: String,
    #[serde(rename = "VSync")]
    pub vsync: bool,
    pub post_effect: String,
    pub upscaler: String,
    pub skip_intro_logos: bool,
    pub skip_cutscenes: bool,
    pub cutscene_speed: u32,
    pub input_mode: String,
    pub mouse_sensitivity: f64,
    pub record_performance: bool,
    pub game_data_root: String,
    pub configuration: String,
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            window_size: "1920x1080".into(),
            fullscreen: false,
            render_scale: 2,
            rendering_path: "Default".into(),
            frame_rate_mode: "60".into(),
            vsync: true,
            post_effect: "fxaa".into(),
            upscaler: "fsr".into(),
            skip_intro_logos: true,
            skip_cutscenes: true,
            cutscene_speed: 4,
            input_mode: "Controller".into(),
            mouse_sensitivity: 1.0,
            record_performance: true,
            game_data_root: "game-data-usa".into(),
            configuration: "Release".into(),
        }
    }
}

fn get_project_root() -> PathBuf {
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

    PathBuf::from(".")
}

fn get_settings_path() -> PathBuf {
    get_project_root().join("private/launcher-usa.json")
}

#[tauri::command]
fn load_settings() -> Result<Settings, String> {
    let path = get_settings_path();
    if !path.exists() {
        return Ok(Settings::default());
    }
    let data = fs::read_to_string(&path).map_err(|e| e.to_string())?;
    let settings: Settings = serde_json::from_str(&data).unwrap_or_default();
    Ok(settings)
}

#[tauri::command]
fn save_settings(settings: Settings) -> Result<(), String> {
    let path = get_settings_path();
    if let Some(parent) = path.parent() {
        let _ = fs::create_dir_all(parent);
    }
    let json = serde_json::to_string_pretty(&settings).map_err(|e| e.to_string())?;
    fs::write(&path, json).map_err(|e| e.to_string())?;
    Ok(())
}

#[tauri::command]
fn launch_game(settings: Settings) -> Result<(), String> {
    save_settings(settings.clone())?;

    let root = get_project_root();
    let script = root.join("scripts/run-usa.ps1");
    if !script.exists() {
        return Err(format!("Runner script not found at: {}", script.display()));
    }

    let game_data_path = if Path::new(&settings.game_data_root).is_absolute() {
        PathBuf::from(&settings.game_data_root)
    } else {
        root.join(&settings.game_data_root)
    };

    let args = [
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
        "-PostEffect".to_string(),
        settings.post_effect,
        "-Upscaler".to_string(),
        settings.upscaler,
        "-RenderingPath".to_string(),
        settings.rendering_path,
        "-FrameRateMode".to_string(),
        settings.frame_rate_mode,
        "-InputMode".to_string(),
        settings.input_mode,
        format!("-SkipIntroLogos:${}", settings.skip_intro_logos),
        format!("-SkipCutscenes:${}", settings.skip_cutscenes),
        "-CutsceneSpeed".to_string(),
        settings.cutscene_speed.to_string(),
        "-GameDataRoot".to_string(),
        game_data_path.to_string_lossy().to_string(),
        "-Configuration".to_string(),
        settings.configuration,
    ];

    Command::new("powershell.exe")
        .args(&args)
        .current_dir(&root)
        .spawn()
        .map_err(|e| format!("Failed to spawn process: {}", e))?;

    std::thread::spawn(|| {
        std::thread::sleep(std::time::Duration::from_millis(500));
        std::process::exit(0);
    });

    Ok(())
}

#[tauri::command]
fn exit_app() {
    std::process::exit(0);
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .setup(|_app| {
            let path = std::env::current_exe()
                .ok()
                .and_then(|p| p.parent().map(|d| d.join("launcher.log")))
                .unwrap_or_else(|| std::path::PathBuf::from("C:\\Users\\kelvy\\Desktop\\Majin Recomp\\launcher.log"));
            if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(path) {
                use std::io::Write;
                let _ = writeln!(f, "setup() hook reached successfully!");
            }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            load_settings,
            save_settings,
            launch_game,
            exit_app
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
