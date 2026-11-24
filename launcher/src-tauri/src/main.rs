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

fn handle_update_mode() -> bool {
    majistic_launcher_lib::updater::handle_update_mode(&write_log)
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
