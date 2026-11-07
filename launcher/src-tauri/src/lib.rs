use std::fs;
use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use serde::{Deserialize, Serialize};
use tauri::Emitter;

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
fn check_game_status() -> Result<GameStatus, String> {
    let root = get_project_root();
    let game_data_dir = root.join("game-data-usa");
    let has_game_data = game_data_dir.join("default.xex").exists();
    let has_unpacked_image = root.join("prototype-usa/private/loaded-image.bin").exists();
    let exe_path = root.join("prototype-usa/out/build/win-amd64-release/majin.exe");
    let has_executable = exe_path.exists();
    let is_ready = has_game_data && has_executable;

    Ok(GameStatus {
        has_game_data,
        has_unpacked_image,
        has_executable,
        is_ready,
        game_data_path: game_data_dir.to_string_lossy().to_string(),
        executable_path: exe_path.to_string_lossy().to_string(),
    })
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
    let output = Command::new("powershell.exe")
        .args(["-NoProfile", "-Command", script])
        .output()
        .map_err(|e| format!("Failed to open file picker: {}", e))?;

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

        let total_stages = if compile_binary { 3 } else { 2 };

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

            let mut child = Command::new("powershell.exe")
                .args(&cmd_args)
                .current_dir(&root)
                .stdout(Stdio::piped())
                .stderr(Stdio::piped())
                .spawn()
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
        emit_stage(1, total_stages, "Extracting ISO files to game-data-usa/...");
        emit_log(&format!("=== STAGE 1/{}: EXTRACTING ISO ===", total_stages));
        emit_log(&format!("Source ISO: {}", iso_path));
        let out_dir = root.join("game-data-usa");
        if let Err(e) = run_ps_script("extract-iso.ps1", &["-IsoPath", &iso_path, "-OutputDir", out_dir.to_str().unwrap_or("game-data-usa")]) {
            emit_log(&format!("ERROR in Stage 1: {}", e));
            let _ = app.emit("build-error", format!("Extraction failed: {}", e));
            return;
        }

        // Stage 2: Unpack XEX
        emit_stage(2, total_stages, "Unpacking default.xex executable...");
        emit_log(&format!("=== STAGE 2/{}: UNPACKING XEX ===", total_stages));
        if let Err(e) = run_ps_script("unpack-xex-usa.ps1", &[]) {
            emit_log(&format!("ERROR in Stage 2: {}", e));
            let _ = app.emit("build-error", format!("Unpacking XEX failed: {}", e));
            return;
        }

        // Stage 3: Compile Binary (if requested)
        if compile_binary {
            emit_stage(3, total_stages, "Compiling native game executable (majin.exe)...");
            emit_log(&format!("=== STAGE 3/{}: COMPILING GAME ===", total_stages));
            if let Err(e) = run_ps_script("build-usa.ps1", &["-Configuration", "Release"]) {
                emit_log(&format!("ERROR in Stage 3: {}", e));
                let _ = app.emit("build-error", format!("Compilation failed: {}", e));
                return;
            }
        }

        emit_log("=== BUILD PIPELINE FINISHED SUCCESSFULLY! ===");
        let _ = app.emit("build-complete", true);
    });

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
            check_game_status,
            select_iso_file,
            start_game_build,
            launch_game,
            exit_app
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
