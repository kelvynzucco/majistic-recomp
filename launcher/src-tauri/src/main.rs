#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::fs::OpenOptions;
use std::io::Write;

pub fn write_log(msg: &str) {
    let path = std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(|d| d.join("launcher.log")))
        .unwrap_or_else(|| std::path::PathBuf::from("C:\\Users\\kelvy\\Desktop\\Majin Recomp\\launcher.log"));
    if let Ok(mut f) = OpenOptions::new()
        .create(true)
        .append(true)
        .open(&path)
    {
        let _ = writeln!(f, "{}", msg);
    }
}

fn main() {
    write_log("=== Launcher Started ===");

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

    write_log("Calling majistic_launcher_lib::run()...");
    majistic_launcher_lib::run();
    write_log("majistic_launcher_lib::run() finished.");
}
