#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::fs::OpenOptions;
use std::io::Write;
use std::path::{Path, PathBuf};

pub fn is_dev_or_debug() -> bool {
    cfg!(feature = "dev-mode")
        || std::env::args().any(|a| {
            a.eq_ignore_ascii_case("--debug")
                || a.eq_ignore_ascii_case("--dev")
                || a.eq_ignore_ascii_case("-debug")
                || a.eq_ignore_ascii_case("-dev")
        })
}

pub fn write_log(msg: &str) {
    if !is_dev_or_debug() {
        return;
    }
    let path = std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(|d| d.join("launcher-debug.log")))
        .unwrap_or_else(|| PathBuf::from("launcher-debug.log"));
    if let Ok(mut f) = OpenOptions::new()
        .create(true)
        .append(true)
        .open(&path)
    {
        let ts = chrono::Local::now().format("%Y-%m-%d %H:%M:%S%.3f");
        let _ = writeln!(f, "[{}] [MAIN] {}", ts, msg);
        let _ = f.flush();
    }
}

fn copy_dir_recursive(src: &Path, dst: &Path) -> std::io::Result<()> {
    std::fs::create_dir_all(dst)?;
    for entry in std::fs::read_dir(src)? {
        let entry = entry?;
        let ft = entry.file_type()?;
        let src_path = entry.path();
        let dst_path = dst.join(entry.file_name());
        if ft.is_dir() {
            copy_dir_recursive(&src_path, &dst_path)?;
        } else {
            let _ = std::fs::copy(&src_path, &dst_path);
        }
    }
    Ok(())
}

fn handle_update_mode() -> bool {
    let args: Vec<String> = std::env::args().collect();
    let is_update = args.iter().any(|a| a.eq_ignore_ascii_case("/UPDATE"));
    if !is_update {
        return false;
    }

    write_log("Update mode detected via /UPDATE flag.");
    let current_exe = match std::env::current_exe() {
        Ok(exe) => exe,
        Err(e) => {
            write_log(&format!("Failed to get current_exe: {}", e));
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
            write_log(&format!("Failed to get current_dir: {}", e));
            return false;
        }
    };

    write_log(&format!("Updater temp_dir: {:?}", temp_dir));
    write_log(&format!("Updater target_dir: {:?}", target_dir));

    if target_dir == temp_dir {
        write_log("Target dir is identical to temp dir, aborting update self-copy.");
        return false;
    }

    let target_exe = target_dir.join(current_exe.file_name().unwrap_or_default());

    // Give the previous process time to fully terminate and release the file handle
    let mut exe_replaced = false;
    for attempt in 1..=30 {
        std::thread::sleep(std::time::Duration::from_millis(200));
        match std::fs::copy(&current_exe, &target_exe) {
            Ok(_) => {
                write_log(&format!("Successfully copied new executable on attempt {}", attempt));
                exe_replaced = true;
                break;
            }
            Err(e) => {
                if attempt % 5 == 0 {
                    write_log(&format!("Waiting for old process lock (attempt {}): {}", attempt, e));
                }
            }
        }
    }

    if !exe_replaced {
        write_log("Failed to replace target executable within timeout.");
        return false;
    }

    // Update project folders if present in temp_dir (merges cleanly into target_dir)
    let folders_to_update = [
        "game-data",
        "scripts",
        "prototype-usa",
        "prototype",
        "tools",
        "src",
        "branding",
        "LICENSES",
    ];
    for folder in &folders_to_update {
        let src_folder = temp_dir.join(folder);
        if src_folder.exists() {
            write_log(&format!("Updating folder '{}'...", folder));
            let _ = copy_dir_recursive(&src_folder, &target_dir.join(folder));
        }
    }

    let files_to_update = ["LICENSE", "README.md"];
    for file in &files_to_update {
        let src_file = temp_dir.join(file);
        if src_file.exists() {
            let _ = std::fs::copy(&src_file, target_dir.join(file));
        }
    }

    write_log("Files updated successfully. Relaunching updated launcher...");
    let _ = std::process::Command::new(&target_exe)
        .current_dir(&target_dir)
        .spawn();

    std::process::exit(0);
}

fn main() {
    let is_dev = is_dev_or_debug();
    let exe_path = std::env::current_exe().unwrap_or_default();
    let exe_dir = exe_path.parent().unwrap_or(Path::new("."));
    let chromium_log = exe_dir.join("launcher-chromium.log");

    if is_dev {
        write_log("==================================================");
        write_log("=== Majistic Launcher Diagnostic Session Start ===");
        write_log(&format!("Exe Path: {}", exe_path.display()));
        write_log(&format!("Working Dir: {}", std::env::current_dir().map(|d| d.display().to_string()).unwrap_or_default()));
        write_log(&format!("Chromium Log Destination: {}", chromium_log.display()));
    }

    #[cfg(windows)]
    {
        // Use Microsoft WARP (Windows Advanced Rasterization Platform) for the ANGLE backend.
        // This ensures DirectComposition remains fully functional with DWM, while bypassing physical GPU
        // driver crashes, MPO plane drops, and idle renderer freezes on AMD Radeon GPUs.
        let existing = std::env::var("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS").unwrap_or_default();

        let full = "--use-angle=warp --disable-direct-composition-video-overlays --disable-gpu-watchdog --disable-renderer-code-integrity --disable-webgpu --disable-features=CalculateNativeWinOcclusion,OptimizationGuideModelDownloading,OptimizationHints,Dawn,Graphite --disable-backgrounding-occluded-windows --disable-background-timer-throttling --disable-renderer-backgrounding";

        // Chromium engine logging is only enabled if explicitly requested via
        // MAJISTIC_CHROMIUM_LOG=1 or --chromium-log CLI arg. Passing --enable-logging
        // causes Edge/Chromium to call AllocConsole(), which spawns an annoying popup
        // console window titled 'msedgewebview2.exe'.
        let enable_chromium_log = std::env::var("MAJISTIC_CHROMIUM_LOG").map(|v| v == "1").unwrap_or(false)
            || std::env::args().any(|a| a.eq_ignore_ascii_case("--chromium-log"));

        let logging = if enable_chromium_log {
            let log_path_normalized = chromium_log.to_string_lossy().replace('\\', "/");
            format!(" --enable-logging --log-file={}", log_path_normalized)
        } else {
            String::new()
        };

        // Diagnostic mode (issue #6): env MAJISTIC_WEBVIEW_MODE or first line of
        // "webview-mode.txt" next to the exe. Modes:
        //   full    (default) all custom flags
        //   minimal only CalculateNativeWinOcclusion + logging
        //   none    no custom flags except logging
        //   noa11y  full + accessibility/UIA disabled
        //   nogpu   full + --disable-gpu
        let mode_file = std::fs::read_to_string(exe_dir.join("webview-mode.txt")).unwrap_or_default();
        let mode = std::env::var("MAJISTIC_WEBVIEW_MODE")
            .ok()
            .filter(|v| !v.trim().is_empty())
            .unwrap_or_else(|| mode_file.lines().next().unwrap_or("").to_string())
            .trim()
            .to_lowercase();
        let mode = if mode.is_empty() { "full".to_string() } else { mode };
        if is_dev {
            write_log(&format!("WebView mode: {}", mode));
        }

        let flags = match mode.as_str() {
            "none" => logging.trim().to_string(),
            "minimal" => format!("--disable-features=CalculateNativeWinOcclusion{}", logging),
            "noa11y" => format!("{} --disable-renderer-accessibility --disable-features=UiaProvider{}", full, logging),
            "nogpu" => format!("{} --disable-gpu{}", full, logging),
            _ => format!("{}{}", full, logging),
        };
        let combined = if existing.is_empty() {
            flags
        } else {
            format!("{} {}", flags, existing)
        };
        std::env::set_var("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", &combined);
        if is_dev {
            write_log(&format!("Set WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS: {}", combined));
        }
    }

    if handle_update_mode() {
        return;
    }

    std::panic::set_hook(Box::new(|info| {
        let msg = format!("PANIC: {}", info);
        write_log(&msg);

        #[cfg(windows)]
        unsafe {
            use std::ffi::CString;
            let title = CString::new("Majistic Launcher Error").unwrap_or_default();
            let body = CString::new(msg.clone()).unwrap_or_default();
            windows_sys::Win32::UI::WindowsAndMessaging::MessageBoxA(
                std::ptr::null_mut(),
                body.as_ptr() as *const u8,
                title.as_ptr() as *const u8,
                windows_sys::Win32::UI::WindowsAndMessaging::MB_ICONERROR | windows_sys::Win32::UI::WindowsAndMessaging::MB_OK,
            );
        }
    }));

    if is_dev {
        write_log("Calling majistic_launcher_lib::run()...");
    }
    majistic_launcher_lib::run();
    if is_dev {
        write_log("majistic_launcher_lib::run() finished.");
    }
}
