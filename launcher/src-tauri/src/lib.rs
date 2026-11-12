use std::fs;
use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use serde::{Deserialize, Serialize};
use tauri::Emitter;

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
    "none".into()
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

fn default_true() -> bool {
    true
}

fn default_false() -> bool {
    false
}

fn default_record_performance() -> bool {
    cfg!(feature = "dev-mode")
}

fn default_fov_scale() -> f64 {
    1.0
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
    #[serde(default = "default_rendering_path")]
    pub rendering_path: String,
    #[serde(default = "default_frame_rate_mode")]
    pub frame_rate_mode: String,
    #[serde(rename = "VSync", default = "default_false", deserialize_with = "deserialize_nullable_bool_false")]
    pub vsync: bool,
    #[serde(default = "default_post_effect")]
    pub post_effect: String,
    #[serde(default = "default_upscaler")]
    pub upscaler: String,
    #[serde(default = "default_true", deserialize_with = "deserialize_nullable_bool_true")]
    pub skip_intro_logos: bool,
    #[serde(default = "default_true", deserialize_with = "deserialize_nullable_bool_true")]
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
    #[serde(default = "default_configuration")]
    pub configuration: String,
    #[serde(default = "default_language")]
    pub language: String,
    #[serde(default = "default_false", deserialize_with = "deserialize_nullable_bool_false")]
    pub step_fix: bool,
    #[serde(default = "default_false", deserialize_with = "deserialize_nullable_bool_false")]
    pub anim_speed_fix: bool,
    #[serde(default = "default_false", deserialize_with = "deserialize_nullable_bool_false")]
    pub dt_smoothing: bool,
    #[serde(default = "default_fov_scale", deserialize_with = "deserialize_nullable_f64")]
    pub fov_scale: f64,
    #[serde(default = "default_false", deserialize_with = "deserialize_nullable_bool_false")]
    pub pause_blur: bool,
    #[serde(default = "default_false", deserialize_with = "deserialize_nullable_bool_false")]
    pub depth_of_field: bool,
    #[serde(default = "default_false", deserialize_with = "deserialize_nullable_bool_false")]
    pub bloom: bool,
    #[serde(default = "default_false", deserialize_with = "deserialize_nullable_bool_false")]
    pub present_dither: bool,
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            window_size: "1280x720".into(),
            fullscreen: false,
            render_scale: 1,
            rendering_path: "Vulkan".into(),
            frame_rate_mode: "30".into(),
            vsync: false,
            post_effect: "none".into(),
            upscaler: "bilinear".into(),
            skip_intro_logos: true,
            skip_cutscenes: true,
            cutscene_speed: 4,
            input_mode: "Controller".into(),
            mouse_sensitivity: 1.0,
            record_performance: cfg!(feature = "dev-mode"),
            game_data_root: "game-data".into(),
            configuration: "Release".into(),
            language: "English".into(),
            step_fix: false,
            anim_speed_fix: false,
            dt_smoothing: false,
            fov_scale: 1.0,
            pause_blur: false,
            depth_of_field: false,
            bloom: false,
            present_dither: false,
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

        // Stage 3: Compile Binary (if requested)
        if compile_binary {
            emit_stage(3, total_stages, "Compiling native game executable (majin.exe)...");
            emit_log(&format!("=== STAGE 3/{}: COMPILING GAME ===", total_stages));
            if let Err(e) = run_ps_script("build-usa.ps1", &["-Configuration", "Release", "-GameDataRoot", &out_dir_rel]) {
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
fn precompile_shaders(app: tauri::AppHandle, settings: Settings) -> Result<(), String> {
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

    std::thread::spawn(move || {
        let emit_log = |msg: &str| {
            let _ = app.emit("precompile-log", msg.to_string());
        };
        let emit_progress = |current: u32, total: u32, status: &str| {
            #[derive(Serialize, Clone)]
            struct ProgressPayload<'a> {
                current: u32,
                total: u32,
                status: &'a str,
            }
            let _ = app.emit("precompile-progress", ProgressPayload { current, total, status });
        };

        emit_log("=== STARTING VULKAN SHADER PRECOMPILATION ===");
        emit_progress(0, 100, "Starting Vulkan shader precompiler...");

        let cmd_args = [
            "-NoLogo",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            script.to_str().unwrap_or_default(),
            "-RenderingPath",
            "Vulkan",
            "-GameDataRoot",
            &game_data_rel,
            "-Configuration",
            &settings.configuration,
            "-ShaderPrebuildOnly",
        ];

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

        let mut child = match cmd.spawn() {
            Ok(c) => c,
            Err(e) => {
                let err_msg = format!("Failed to spawn precompile process: {}", e);
                emit_log(&err_msg);
                let _ = app.emit("precompile-error", err_msg);
                return;
            }
        };

        let stdout = child.stdout.take();
        let stderr = child.stderr.take();

        let log_path_sync = std::sync::Arc::new(std::sync::Mutex::new(None::<PathBuf>));
        let log_path_writer = std::sync::Arc::clone(&log_path_sync);

        let app_out = app.clone();
        let stdout_thread = std::thread::spawn(move || {
            if let Some(out) = stdout {
                let reader = BufReader::new(out);
                for line in reader.lines().flatten() {
                    let _ = app_out.emit("precompile-log", line.clone());
                    if line.starts_with("Runtime log: ") {
                        let path_str = line.trim_start_matches("Runtime log: ").trim();
                        if let Ok(mut lock) = log_path_writer.lock() {
                            *lock = Some(PathBuf::from(path_str));
                        }
                    }
                }
            }
        });

        let app_err = app.clone();
        let stderr_thread = std::thread::spawn(move || {
            if let Some(err) = stderr {
                let reader = BufReader::new(err);
                for line in reader.lines().flatten() {
                    let _ = app_err.emit("precompile-log", format!("[stderr] {}", line));
                }
            }
        });

        let is_running = std::sync::Arc::new(std::sync::atomic::AtomicBool::new(true));
        let is_running_tail = std::sync::Arc::clone(&is_running);
        let log_path_reader = std::sync::Arc::clone(&log_path_sync);
        let app_tail = app.clone();

        let tail_thread = std::thread::spawn(move || {
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
                                let _ = app_tail.emit("precompile-log", line.clone());

                                if let Some(idx) = line.find("SHADER_WARMUP: Vulkan progress ") {
                                    let rest = &line[idx + "SHADER_WARMUP: Vulkan progress ".len()..];
                                    let parts: Vec<&str> = rest.split('/').collect();
                                    if parts.len() == 2 {
                                        let curr = parts[0].trim().parse::<u32>().unwrap_or(0);
                                        let total = parts[1].trim().parse::<u32>().unwrap_or(171);
                                        let status_text = format!("Compiling Vulkan pipelines ({}/{})...", curr, total);
                                        #[derive(Serialize, Clone)]
                                        struct ProgressPayload<'a> {
                                            current: u32,
                                            total: u32,
                                            status: &'a str,
                                        }
                                        let _ = app_tail.emit("precompile-progress", ProgressPayload {
                                            current: curr,
                                            total,
                                            status: &status_text,
                                        });
                                    }
                                } else if line.contains("Vulkan monolithic driver pipeline cache: saved") {
                                    #[derive(Serialize, Clone)]
                                    struct ProgressPayload<'a> {
                                        current: u32,
                                        total: u32,
                                        status: &'a str,
                                    }
                                    let _ = app_tail.emit("precompile-progress", ProgressPayload {
                                        current: 100,
                                        total: 100,
                                        status: "Monolithic driver cache saved to disk",
                                    });
                                }
                            }
                        }
                    }
                }

                std::thread::sleep(std::time::Duration::from_millis(60));
            }
        });

        let _ = stdout_thread.join();
        let _ = stderr_thread.join();
        is_running.store(false, std::sync::atomic::Ordering::Relaxed);
        let _ = tail_thread.join();

        match child.wait() {
            Ok(status) if status.success() => {
                emit_progress(100, 100, "Vulkan pipeline cache successfully created!");
                emit_log("=== VULKAN SHADERS PRECOMPILED SUCCESSFULLY ===");
                let _ = app.emit("precompile-complete", true);
            }
            Ok(status) => {
                let err_msg = format!("Precompile exited with error status: {}", status);
                emit_log(&err_msg);
                let _ = app.emit("precompile-error", err_msg);
            }
            Err(e) => {
                let err_msg = format!("Failed to wait on precompile process: {}", e);
                emit_log(&err_msg);
                let _ = app.emit("precompile-error", err_msg);
            }
        }
    });

    Ok(())
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ShaderCacheInfo {
    pub exists: bool,
    pub is_precompiled: bool,
    pub xsh_bytes: u64,
    pub xsh_formatted: String,
    pub xpso_bytes: u64,
    pub xpso_formatted: String,
    pub pipeline_count: usize,
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
    let local_dir = shaders_dir.join("local");

    let xsh_path = shareable_dir.join("4E4D0816.xsh");
    let xpso_path = shareable_dir.join("4E4D0816.fbo.vk.xpso");

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

    Ok(ShaderCacheInfo {
        exists,
        is_precompiled,
        xsh_bytes,
        xsh_formatted: format_bytes(xsh_bytes),
        xpso_bytes,
        xpso_formatted: format_bytes(xpso_bytes),
        pipeline_count,
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

    let is_dev = is_dev_or_debug();
    let log_file_path = if is_dev {
        let p = root.join("private/launcher-launch.log");
        if let Some(parent) = p.parent() {
            let _ = fs::create_dir_all(parent);
        }
        Some(p)
    } else {
        None
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
        format!("-StepFix:${}", settings.step_fix),
        format!("-AnimSpeedFix:${}", settings.anim_speed_fix),
        format!("-DtSmoothing:${}", settings.dt_smoothing),
        "-FovScale".to_string(),
        settings.fov_scale.to_string(),
        format!("-PauseBlur:${}", settings.pause_blur),
        format!("-DepthOfField:${}", settings.depth_of_field),
        format!("-Bloom:${}", settings.bloom),
        format!("-PresentDither:${}", settings.present_dither),
        "-Configuration".to_string(),
        settings.configuration,
    ];

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
    cfg!(feature = "dev-mode")
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
            write_debug_log("=== Setup Hook Entered ===");
            write_debug_log(&format!("OS Architecture: {}", std::env::consts::ARCH));

            #[cfg(windows)]
            {
                use tauri::Manager;
                if let Some(main_window) = app.get_webview_window("main") {
                    write_debug_log("Main WebviewWindow found. Initializing telemetry and listeners...");

                    // Watchdog thread: checks window state periodically
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

                    let _ = main_window.with_webview(move |webview| {
                        unsafe {
                            use webview2_com::Microsoft::Web::WebView2::Win32::*;
                            use webview2_com::{ProcessFailedEventHandler, AcceleratorKeyPressedEventHandler, NavigationCompletedEventHandler};
                            use windows_core::Interface;

                            let controller = webview.controller();
                            let environment = webview.environment();

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
                                        // (faulting module / WER data) for this crash.
                                        std::thread::spawn(|| {
                                            std::thread::sleep(std::time::Duration::from_secs(3));
                                            collect_crash_events();
                                        });
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
            exit_app
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}

#[cfg(test)]
mod tests {
    use super::*;

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
            "PauseBlur": null,
            "DepthOfField": true,
            "Bloom": true
        }"#;

        let parsed: Result<Settings, _> = serde_json::from_str(json);
        assert!(parsed.is_ok(), "Failed to deserialize settings with null fields: {:?}", parsed.err());
        let settings = parsed.unwrap();
        assert_eq!(settings.fov_scale, 1.0);
        assert_eq!(settings.mouse_sensitivity, 1.0);
        assert!(!settings.pause_blur);
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
            "PauseBlur": false,
            "DepthOfField": false,
            "Bloom": false,
            "PresentDither": true
        }"#;

        let parsed: Result<Settings, _> = serde_json::from_str(json);
        assert!(parsed.is_ok(), "Failed to deserialize valid settings: {:?}", parsed.err());
        let settings = parsed.unwrap();
        assert_eq!(settings.window_size, "2560x1440");
        assert!(settings.fullscreen);
        assert_eq!(settings.render_scale, 3);
        assert_eq!(settings.rendering_path, "Vulkan");
        assert_eq!(settings.frame_rate_mode, "120");
        assert!(!settings.vsync);
        assert_eq!(settings.fov_scale, 1.2);
        assert_eq!(settings.mouse_sensitivity, 1.5);
        assert_eq!(settings.language, "Japanese");
        assert!(settings.step_fix);
        assert!(!settings.anim_speed_fix);
        assert!(settings.dt_smoothing);
        assert!(!settings.pause_blur);
        assert!(settings.present_dither);
    }

    #[test]
    fn test_format_bytes() {
        assert_eq!(format_bytes(500), "500 B");
        assert_eq!(format_bytes(1024), "1.0 KB");
        assert_eq!(format_bytes(1536), "1.5 KB");
        assert_eq!(format_bytes(1048576), "1.00 MB");
        assert_eq!(format_bytes(21551962), "20.55 MB");
    }
}
