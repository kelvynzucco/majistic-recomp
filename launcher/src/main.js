import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { check } from "@tauri-apps/plugin-updater";

function logToBackend(msg) {
  invoke("log_message", { msg: String(msg) }).catch(() => {});
}

// Global JS error & rejection logging
window.addEventListener("error", (e) => {
  logToBackend(`GLOBAL_ERROR: ${e.message} at ${e.filename}:${e.lineno}:${e.colno}`);
});
window.addEventListener("unhandledrejection", (e) => {
  logToBackend(`UNHANDLED_REJECTION: ${e.reason?.stack || e.reason}`);
});

// Compositor Keep-Alive: keeps WebView2 swapchain active on idle to prevent DWM/MPO blank screen
function compositorHeartbeat() {
  requestAnimationFrame(compositorHeartbeat);
}
requestAnimationFrame(compositorHeartbeat);

// Default configuration structure
const defaultSettings = {
  WindowSize: "1280x720",
  Fullscreen: false,
  RenderScale: 1,
  RenderingPath: "Default",
  FrameRateMode: "30",
  VSync: true,
  PostEffect: "none",
  Upscaler: "bilinear",
  SkipIntroLogos: true,
  SkipCutscenes: true,
  CutsceneSpeed: 4,
  InputMode: "Controller",
  MouseSensitivity: 1.0,
  RecordPerformance: true,
  GameDataRoot: "game-data",
  Configuration: "Release",
  Language: "English",
  StepFix: false,
  AnimSpeedFix: true,
  DtSmoothing: false,
  FovScale: 1.0,
  PauseBlur: true,
  DepthOfField: true,
  Bloom: true,
};

let currentSettings = { ...defaultSettings };
let gameStatus = {
  hasGameData: false,
  hasUnpackedImage: false,
  hasExecutable: false,
  isReady: false,
};

// DOM Elements
const elements = {
  // Navigation Tabs
  tabBtnSettings: document.getElementById("tabBtnSettings"),
  tabBtnFixes: document.getElementById("tabBtnFixes"),
  tabBtnBuild: document.getElementById("tabBtnBuild"),
  tabSettings: document.getElementById("tabSettings"),
  tabFixes: document.getElementById("tabFixes"),
  tabBuild: document.getElementById("tabBuild"),
  tabBuildIndicator: document.getElementById("tabBuildIndicator"),

  // Header & Status
  appVersionBadge: document.getElementById("appVersionBadge"),
  btnCheckUpdates: document.getElementById("btnCheckUpdates"),
  btnCheckUpdatesText: document.getElementById("btnCheckUpdatesText"),
  updateBtnIcon: document.getElementById("updateBtnIcon"),
  gameStatusBadge: document.getElementById("gameStatusBadge"),
  gameStatusBadgeText: document.getElementById("gameStatusBadgeText"),
  setupWarningBanner: document.getElementById("setupWarningBanner"),
  btnGoToSetup: document.getElementById("btnGoToSetup"),

  // Software Update Modal
  updateModal: document.getElementById("updateModal"),
  updateModalVersion: document.getElementById("updateModalVersion"),
  updateModalNotesContainer: document.getElementById("updateModalNotesContainer"),
  updateProgressContainer: document.getElementById("updateProgressContainer"),
  updateProgressStatus: document.getElementById("updateProgressStatus"),
  updateProgressPercent: document.getElementById("updateProgressPercent"),
  updateProgressBar: document.getElementById("updateProgressBar"),
  updateModalActions: document.getElementById("updateModalActions"),
  btnUpdateLater: document.getElementById("btnUpdateLater"),
  btnUpdateNow: document.getElementById("btnUpdateNow"),
  btnCloseUpdateModal: document.getElementById("btnCloseUpdateModal"),

  // Settings form elements
  windowSize: document.getElementById("windowSize"),
  fullscreen: document.getElementById("fullscreen"),
  renderScale: document.getElementById("renderScale"),
  frameRateMode: document.getElementById("frameRateMode"),
  renderingPath: document.getElementById("renderingPath"),
  upscaler: document.getElementById("upscaler"),
  postEffect: document.getElementById("postEffect"),
  vsync: document.getElementById("vsync"),
  inputMode: document.getElementById("inputMode"),
  cutsceneSpeed: document.getElementById("cutsceneSpeed"),
  skipCutscenes: document.getElementById("skipCutscenes"),
  skipIntroLogos: document.getElementById("skipIntroLogos"),
  gameDataRoot: document.getElementById("gameDataRoot"),
  language: document.getElementById("language"),

  // Fixes form elements
  stepFix: document.getElementById("stepFix"),
  dtSmoothing: document.getElementById("dtSmoothing"),
  animSpeedFix: document.getElementById("animSpeedFix"),
  fovScale: document.getElementById("fovScale"),
  depthOfField: document.getElementById("depthOfField"),
  bloom: document.getElementById("bloom"),
  pauseBlur: document.getElementById("pauseBlur"),

  // Build Tab Elements
  btnBrowseGameData: document.getElementById("btnBrowseGameData"),
  statusAssets: document.getElementById("statusAssets"),
  statusImage: document.getElementById("statusImage"),
  statusBinary: document.getElementById("statusBinary"),
  txtIsoPath: document.getElementById("txtIsoPath"),
  btnBrowseIso: document.getElementById("btnBrowseIso"),
  chkCompileBinary: document.getElementById("chkCompileBinary"),
  btnStartBuild: document.getElementById("btnStartBuild"),
  buildProgressSection: document.getElementById("buildProgressSection"),
  buildSpinner: document.getElementById("buildSpinner"),
  buildStageText: document.getElementById("buildStageText"),
  buildPercentText: document.getElementById("buildPercentText"),
  buildProgressBar: document.getElementById("buildProgressBar"),
  buildConsole: document.getElementById("buildConsole"),
  btnCopyLog: document.getElementById("btnCopyLog"),
  btnCopyLogText: document.getElementById("btnCopyLogText"),
  btnClearLog: document.getElementById("btnClearLog"),

  // Window Control Buttons
  btnWindowMinimize: document.getElementById("btnWindowMinimize"),
  btnWindowMaximize: document.getElementById("btnWindowMaximize"),
  btnWindowClose: document.getElementById("btnWindowClose"),
  iconMaximize: document.getElementById("iconMaximize"),
  iconRestore: document.getElementById("iconRestore"),

  // Footer Buttons
  btnLaunch: document.getElementById("btnLaunch"),
  btnLaunchText: document.getElementById("btnLaunchText"),
};

// ==========================================
// Tab Switching
// ==========================================
function switchTab(tab) {
  const activeClass =
    "px-5 py-2 text-xs font-semibold rounded-t-lg border-b-2 border-accent text-accent bg-darkCard transition-all flex items-center gap-2";
  const inactiveClass =
    "px-5 py-2 text-xs font-semibold rounded-t-lg border-b-2 border-transparent text-slate-400 hover:text-slate-200 hover:bg-darkCard/50 transition-all flex items-center gap-2";

  if (tab === "settings") {
    elements.tabSettings.classList.remove("hidden");
    if (elements.tabFixes) elements.tabFixes.classList.add("hidden");
    elements.tabBuild.classList.add("hidden");

    elements.tabBtnSettings.className = activeClass;
    if (elements.tabBtnFixes) elements.tabBtnFixes.className = inactiveClass;
    elements.tabBtnBuild.className = inactiveClass;
  } else if (tab === "fixes") {
    elements.tabSettings.classList.add("hidden");
    if (elements.tabFixes) elements.tabFixes.classList.remove("hidden");
    elements.tabBuild.classList.add("hidden");

    elements.tabBtnSettings.className = inactiveClass;
    if (elements.tabBtnFixes) elements.tabBtnFixes.className = activeClass;
    elements.tabBtnBuild.className = inactiveClass;
  } else {
    elements.tabSettings.classList.add("hidden");
    if (elements.tabFixes) elements.tabFixes.classList.add("hidden");
    elements.tabBuild.classList.remove("hidden");

    elements.tabBtnSettings.className = inactiveClass;
    if (elements.tabBtnFixes) elements.tabBtnFixes.className = inactiveClass;
    elements.tabBtnBuild.className = activeClass;
  }
}

elements.tabBtnSettings.addEventListener("click", () => switchTab("settings"));
if (elements.tabBtnFixes) elements.tabBtnFixes.addEventListener("click", () => switchTab("fixes"));
elements.tabBtnBuild.addEventListener("click", () => switchTab("build"));
elements.btnGoToSetup.addEventListener("click", () => switchTab("build"));

function safeFloat(val, fallback) {
  const num = typeof val === "number" ? val : parseFloat(val);
  return Number.isFinite(num) ? num : fallback;
}

function safeInt(val, fallback) {
  const num = typeof val === "number" ? val : parseInt(val, 10);
  return Number.isFinite(num) ? num : fallback;
}

// ==========================================
// Settings Management
// ==========================================
function populateForm(settings) {
  if (elements.windowSize) elements.windowSize.value = settings.WindowSize || "1280x720";
  if (elements.fullscreen) elements.fullscreen.value = String(!!settings.Fullscreen);
  if (elements.renderScale) elements.renderScale.value = String(settings.RenderScale || 1);
  if (elements.frameRateMode) elements.frameRateMode.value = settings.FrameRateMode || "30";
  if (elements.renderingPath) {
    const rp = settings.RenderingPath || "Default";
    elements.renderingPath.value = (rp === "Vulkan_FSI") ? "Vulkan" : rp;
  }
  if (elements.upscaler) elements.upscaler.value = settings.Upscaler || "bilinear";
  if (elements.postEffect) elements.postEffect.value = settings.PostEffect || "none";
  if (elements.vsync) elements.vsync.checked = settings.VSync ?? true;
  if (elements.inputMode) elements.inputMode.value = settings.InputMode || "Controller";
  if (elements.cutsceneSpeed) elements.cutsceneSpeed.value = String(settings.CutsceneSpeed || 4);
  if (elements.skipCutscenes) elements.skipCutscenes.checked = settings.SkipCutscenes ?? true;
  if (elements.skipIntroLogos) elements.skipIntroLogos.checked = settings.SkipIntroLogos ?? true;
  if (elements.gameDataRoot) elements.gameDataRoot.value = settings.GameDataRoot || "game-data";
  if (elements.language) elements.language.value = settings.Language || "English";

  // Fixes form
  if (elements.stepFix) elements.stepFix.checked = settings.StepFix ?? false;
  if (elements.animSpeedFix) elements.animSpeedFix.checked = settings.AnimSpeedFix ?? true;
  if (elements.dtSmoothing) elements.dtSmoothing.checked = settings.DtSmoothing ?? false;
  if (elements.fovScale) {
    const targetFov = safeFloat(settings.FovScale, 1.0);
    let matched = false;
    for (let i = 0; i < elements.fovScale.options.length; i++) {
      const optVal = parseFloat(elements.fovScale.options[i].value);
      if (Math.abs(optVal - targetFov) < 0.001) {
        elements.fovScale.selectedIndex = i;
        matched = true;
        break;
      }
    }
    if (!matched) {
      elements.fovScale.value = "1.0";
    }
  }
  if (elements.depthOfField) elements.depthOfField.checked = settings.DepthOfField ?? true;
  if (elements.bloom) elements.bloom.checked = settings.Bloom ?? true;
  if (elements.pauseBlur) elements.pauseBlur.checked = settings.PauseBlur ?? true;
}

function readForm() {
  return {
    WindowSize: elements.windowSize?.value || "1280x720",
    Fullscreen: elements.fullscreen ? elements.fullscreen.value === "true" : false,
    RenderScale: safeInt(elements.renderScale?.value, 1),
    RenderingPath: elements.renderingPath?.value || "Default",
    FrameRateMode: elements.frameRateMode?.value || "30",
    VSync: elements.vsync ? elements.vsync.checked : true,
    PostEffect: elements.postEffect?.value || "none",
    Upscaler: elements.upscaler?.value || "bilinear",
    SkipIntroLogos: elements.skipIntroLogos ? elements.skipIntroLogos.checked : true,
    SkipCutscenes: elements.skipCutscenes ? elements.skipCutscenes.checked : true,
    CutsceneSpeed: safeInt(elements.cutsceneSpeed?.value, 4),
    InputMode: elements.inputMode?.value || "Controller",
    MouseSensitivity: safeFloat(currentSettings.MouseSensitivity, 1.0),
    RecordPerformance: currentSettings.RecordPerformance ?? true,
    GameDataRoot: elements.gameDataRoot?.value ? elements.gameDataRoot.value.trim() : "game-data",
    Configuration: "Release",
    Language: elements.language?.value || currentSettings.Language || "English",
    StepFix: elements.stepFix ? elements.stepFix.checked : false,
    AnimSpeedFix: elements.animSpeedFix ? elements.animSpeedFix.checked : true,
    DtSmoothing: elements.dtSmoothing ? elements.dtSmoothing.checked : false,
    FovScale: safeFloat(elements.fovScale?.value, 1.0),
    DepthOfField: elements.depthOfField ? elements.depthOfField.checked : true,
    Bloom: elements.bloom ? elements.bloom.checked : true,
    PauseBlur: elements.pauseBlur ? elements.pauseBlur.checked : true,
  };
}

// ==========================================
// Game Status Updates
// ==========================================
async function updateGameStatus() {
  try {
    const status = await invoke("check_game_status");
    if (status && typeof status === "object") {
      gameStatus = status;

      // Update Header Badge
      if (status.isReady) {
        elements.gameStatusBadge.className =
          "flex items-center gap-1.5 px-2.5 py-1 rounded text-xs font-medium border bg-[#6da712]/15 text-[#a3cc54] border-[#6da712]/40";
        elements.gameStatusBadge.innerHTML = `
          <span class="w-2 h-2 rounded-full bg-[#6da712]"></span>
          <span>Game Ready</span>
        `;
        elements.setupWarningBanner.classList.add("hidden");
        elements.tabBuildIndicator.classList.add("hidden");

        elements.btnLaunch.disabled = false;
        elements.btnLaunch.classList.remove("opacity-50");
        elements.btnLaunchText.textContent = "LAUNCH GAME";
      } else {
        elements.gameStatusBadge.className =
          "flex items-center gap-1.5 px-2.5 py-1 rounded text-xs font-medium border bg-amber-950/60 text-amber-300 border-amber-800/80";
        elements.gameStatusBadge.innerHTML = `
          <span class="w-2 h-2 rounded-full bg-amber-400 animate-pulse"></span>
          <span>Setup Required</span>
        `;
        elements.setupWarningBanner.classList.remove("hidden");
        elements.tabBuildIndicator.classList.remove("hidden");

        elements.btnLaunch.disabled = false;
        elements.btnLaunchText.textContent = "SETUP GAME FIRST";
      }

      // Update Tab 2 Status Cards
      elements.statusAssets.textContent = status.hasGameData ? "Extracted (Ready)" : "Missing";
      elements.statusAssets.className = status.hasGameData
        ? "font-bold text-[#6da712]"
        : "font-bold text-rose-400";

      elements.statusImage.textContent = status.hasUnpackedImage ? "Unpacked (Ready)" : "Pending";
      elements.statusImage.className = status.hasUnpackedImage
        ? "font-bold text-[#6da712]"
        : "font-bold text-amber-400";

      elements.statusBinary.textContent = status.hasExecutable ? "Compiled (Ready)" : "Missing";
      elements.statusBinary.className = status.hasExecutable
        ? "font-bold text-[#6da712]"
        : "font-bold text-rose-400";

      // If executable is already present, ensure compile binary checkbox remains unchecked
      if (status.hasExecutable && elements.chkCompileBinary) {
        elements.chkCompileBinary.checked = false;
      }
    }
  } catch (err) {
    console.warn("Failed to check game status:", err);
  }
}

// ==========================================
// Build / Extraction Logic
// ==========================================
function appendConsoleLog(line) {
  const lineDiv = document.createElement("div");
  lineDiv.className = line.startsWith("ERROR") || line.startsWith("[stderr]")
    ? "text-rose-400 font-semibold"
    : line.startsWith("===")
    ? "text-sky-300 font-bold border-t border-slate-800 pt-1 mt-1"
    : "text-slate-300";
  lineDiv.textContent = line;
  elements.buildConsole.appendChild(lineDiv);
  elements.buildConsole.scrollTop = elements.buildConsole.scrollHeight;
}

elements.btnClearLog.addEventListener("click", () => {
  elements.buildConsole.innerHTML = "";
});

if (elements.btnCopyLog) {
  elements.btnCopyLog.addEventListener("click", async () => {
    try {
      const text = elements.buildConsole.innerText || elements.buildConsole.textContent || "";
      await navigator.clipboard.writeText(text);
      if (elements.btnCopyLogText) {
        const originalText = elements.btnCopyLogText.textContent;
        elements.btnCopyLogText.textContent = "Copied!";
        elements.btnCopyLog.classList.add("text-emerald-400");
        setTimeout(() => {
          elements.btnCopyLogText.textContent = originalText;
          elements.btnCopyLog.classList.remove("text-emerald-400");
        }, 2000);
      }
    } catch (err) {
      console.warn("Failed to copy log to clipboard:", err);
    }
  });
}

// Disable native browser context menu (right click) across the application
window.addEventListener("contextmenu", (e) => {
  e.preventDefault();
});

// Select Game Data Folder
if (elements.btnBrowseGameData) {
  elements.btnBrowseGameData.addEventListener("click", async () => {
    try {
      const selected = await invoke("select_game_data_folder");
      if (selected) {
        elements.gameDataRoot.value = selected;
        const settings = readForm();
        await invoke("save_settings", { settings });
        await updateGameStatus();
      }
    } catch (err) {
      alert("Error selecting folder: " + err);
    }
  });
}

// Select ISO
elements.btnBrowseIso.addEventListener("click", async () => {
  try {
    const selected = await invoke("select_iso_file");
    if (selected) {
      elements.txtIsoPath.value = selected;
    }
  } catch (err) {
    alert("Error selecting ISO file: " + err);
  }
});

// Start Build
elements.btnStartBuild.addEventListener("click", async () => {
  const isoPath = elements.txtIsoPath.value.trim();
  if (!isoPath) {
    alert("Please select your game disc image (.iso) first.");
    return;
  }

  const compileBinary = elements.chkCompileBinary.checked;
  if (compileBinary) {
    const confirmCompile = confirm(
      "Recompiling the game binary requires Visual Studio 2022 with C++ x64 tools installed on this PC.\n\n" +
      "If you only want to extract and play using the included precompiled executable (majin.exe), click Cancel and leave this checkbox unchecked.\n\n" +
      "Proceed with C++ source compilation?"
    );
    if (!confirmCompile) {
      elements.chkCompileBinary.checked = false;
      return;
    }
  }

  elements.btnStartBuild.disabled = true;
  elements.btnStartBuild.classList.add("opacity-50");
  elements.btnBrowseIso.disabled = true;
  elements.buildProgressSection.classList.remove("hidden");
  elements.buildConsole.innerHTML = "";
  elements.buildProgressBar.style.width = "5%";
  elements.buildPercentText.textContent = "5%";
  elements.buildStageText.textContent = "Starting pipeline...";
  elements.buildSpinner.classList.remove("hidden");

  appendConsoleLog(`[Setup] Target ISO: ${isoPath}`);
  appendConsoleLog(`[Setup] Compile Native Executable: ${compileBinary ? "YES" : "NO"}`);

  try {
    await invoke("start_game_build", {
      isoPath,
      compileBinary,
    });
  } catch (err) {
    appendConsoleLog(`ERROR starting build: ${err}`);
    elements.btnStartBuild.disabled = false;
    elements.btnStartBuild.classList.remove("opacity-50");
    elements.btnBrowseIso.disabled = false;
    elements.buildSpinner.classList.add("hidden");
  }
});

// Listen to backend build events
listen("build-stage", (event) => {
  const { stage, total, description } = event.payload;
  const pct = Math.round((stage / (total + 1)) * 100);
  elements.buildProgressBar.style.width = `${pct}%`;
  elements.buildPercentText.textContent = `${pct}%`;
  elements.buildStageText.textContent = `[${stage}/${total}] ${description}`;
});

listen("build-log", (event) => {
  appendConsoleLog(event.payload);
});

listen("build-complete", async () => {
  elements.buildProgressBar.style.width = "100%";
  elements.buildPercentText.textContent = "100%";
  elements.buildStageText.textContent = "Build Pipeline Completed Successfully!";
  elements.buildSpinner.classList.add("hidden");
  elements.btnStartBuild.disabled = false;
  elements.btnStartBuild.classList.remove("opacity-50");
  elements.btnBrowseIso.disabled = false;

  await updateGameStatus();
});

listen("build-error", async (event) => {
  elements.buildStageText.textContent = `Error: ${event.payload}`;
  elements.buildStageText.className = "text-xs font-semibold text-rose-400";
  elements.buildSpinner.classList.add("hidden");
  elements.btnStartBuild.disabled = false;
  elements.btnStartBuild.classList.remove("opacity-50");
  elements.btnBrowseIso.disabled = false;

  await updateGameStatus();
});

// ==========================================
// Launch & Exit
// ==========================================
elements.btnLaunch.addEventListener("click", async () => {
  if (!gameStatus.isReady) {
    switchTab("build");
    return;
  }

  const settings = readForm();
  elements.btnLaunch.disabled = true;
  elements.btnLaunch.classList.add("opacity-50");
  elements.btnLaunchText.textContent = "LAUNCHING...";

  try {
    await invoke("launch_game", { settings });
  } catch (err) {
    alert("Failed to launch game: " + err);
    elements.btnLaunch.disabled = false;
    elements.btnLaunch.classList.remove("opacity-50");
    elements.btnLaunchText.textContent = "LAUNCH GAME";
  }
});

// ==========================================
// Window Controls (Frameless Titlebar)
// ==========================================
async function updateMaximizeIcon() {
  try {
    const isMax = await invoke("is_window_maximized");
    if (elements.iconMaximize && elements.iconRestore) {
      if (isMax) {
        elements.iconMaximize.classList.add("hidden");
        elements.iconRestore.classList.remove("hidden");
      } else {
        elements.iconMaximize.classList.remove("hidden");
        elements.iconRestore.classList.add("hidden");
      }
    }
  } catch {
    // ignore
  }
}

// Window Dragging Support
const appHeader = document.getElementById("appHeader");
if (appHeader) {
  appHeader.addEventListener("mousedown", (e) => {
    if (e.button === 0 && !e.target.closest("button, a, input, select, textarea")) {
      invoke("start_drag_window").catch(() => {});
    }
  });
}

if (elements.btnWindowMinimize) {
  elements.btnWindowMinimize.addEventListener("click", async () => {
    try {
      await invoke("minimize_window");
    } catch (err) {
      console.warn("Failed to minimize window:", err);
    }
  });
}

if (elements.btnWindowMaximize) {
  elements.btnWindowMaximize.addEventListener("click", async () => {
    try {
      await invoke("toggle_maximize_window");
      await updateMaximizeIcon();
    } catch (err) {
      console.warn("Failed to toggle maximize window:", err);
    }
  });
}

if (elements.btnWindowClose) {
  elements.btnWindowClose.addEventListener("click", async () => {
    try {
      await invoke("close_window");
    } catch {
      window.close();
    }
  });
}

window.addEventListener("resize", () => {
  updateMaximizeIcon();
});

// Auto-save settings on change
document.addEventListener("change", async (e) => {
  if (e.target && (e.target.closest("#tabSettings") || e.target.closest("#tabFixes") || e.target.closest("#tabBuild"))) {
    const settings = readForm();
    try {
      await invoke("save_settings", { settings });
    } catch {
      // ignore
    }
    if (e.target.id === "gameDataRoot") {
      await updateGameStatus();
    }
  }
});

// ==========================================
// Software Update Logic
// ==========================================
let currentPendingUpdate = null;
let isCheckingUpdates = false;
let isDownloadingUpdate = false;

function setUpdateBtnState(state, text) {
  const btn = elements.btnCheckUpdates;
  const btnText = elements.btnCheckUpdatesText;
  const btnIcon = elements.updateBtnIcon;

  if (!btn || !btnText) return;

  if (state === "checking") {
    btnText.textContent = text || "Checking...";
    if (btnIcon) {
      btnIcon.classList.add("animate-spin");
      btnIcon.innerHTML = `<path d="M224,128a96,96,0,0,1-144,83.14V224a8,8,0,0,1-16,0V192a8,8,0,0,1,8-8h32a8,8,0,0,1,0,16H84.62A80,80,0,1,0,80,68.9V80a8,8,0,0,1-16,0V48a8,8,0,0,1,8-8h32a8,8,0,0,1,0,16H83.14A96,96,0,0,1,224,128Z"/>`;
    }
    btn.disabled = true;
    btn.className = "h-6 inline-flex items-center gap-1.5 px-2.5 rounded bg-darkControl/50 text-slate-400 border border-darkBorder text-[11px] font-medium select-none cursor-wait focus:outline-none";
  } else if (state === "up-to-date") {
    btnText.textContent = text || "Up to Date";
    if (btnIcon) {
      btnIcon.classList.remove("animate-spin");
      btnIcon.innerHTML = `<path d="M229.66,77.66l-128,128a8,8,0,0,1-11.32,0l-56-56a8,8,0,0,1,11.32-11.32L96,188.69,218.34,66.34a8,8,0,0,1,11.32,11.32Z"/>`;
    }
    btn.disabled = false;
    btn.className = "h-6 inline-flex items-center gap-1.5 px-2.5 rounded bg-emerald-950/40 text-emerald-400 border border-emerald-800/60 text-[11px] font-medium transition-all select-none cursor-pointer focus:outline-none";
  } else if (state === "available") {
    btnText.textContent = text || "Update Available";
    if (btnIcon) {
      btnIcon.classList.remove("animate-spin");
      btnIcon.innerHTML = `<path d="M224,128a96,96,0,0,1-144,83.14V224a8,8,0,0,1-16,0V192a8,8,0,0,1,8-8h32a8,8,0,0,1,0,16H84.62A80,80,0,1,0,80,68.9V80a8,8,0,0,1-16,0V48a8,8,0,0,1,8-8h32a8,8,0,0,1,0,16H83.14A96,96,0,0,1,224,128Z"/>`;
    }
    btn.disabled = false;
    btn.className = "h-6 inline-flex items-center gap-1.5 px-2.5 rounded bg-accent/20 hover:bg-accent/30 text-accent hover:text-white border border-accent/50 text-[11px] font-semibold transition-all select-none cursor-pointer focus:outline-none";
  } else if (state === "error") {
    btnText.textContent = text || "Check Failed";
    if (btnIcon) {
      btnIcon.classList.remove("animate-spin");
    }
    btn.disabled = false;
    btn.className = "h-6 inline-flex items-center gap-1.5 px-2.5 rounded bg-rose-950/40 text-rose-400 border border-rose-800/60 text-[11px] font-medium transition-all select-none cursor-pointer focus:outline-none";
  } else {
    // default
    btnText.textContent = "Check for Updates";
    if (btnIcon) {
      btnIcon.classList.remove("animate-spin");
      btnIcon.innerHTML = `<path d="M224,128a96,96,0,0,1-144,83.14V224a8,8,0,0,1-16,0V192a8,8,0,0,1,8-8h32a8,8,0,0,1,0,16H84.62A80,80,0,1,0,80,68.9V80a8,8,0,0,1-16,0V48a8,8,0,0,1,8-8h32a8,8,0,0,1,0,16H83.14A96,96,0,0,1,224,128Z"/>`;
    }
    btn.disabled = false;
    btn.className = "h-6 inline-flex items-center gap-1.5 px-2.5 rounded bg-darkControl/80 hover:bg-darkControl text-slate-300 hover:text-white border border-darkBorder hover:border-slate-500 text-[11px] font-medium transition-all select-none cursor-pointer focus:outline-none";
  }
}

function showUpdateModal(update) {
  if (!elements.updateModal) return;
  if (elements.updateModalVersion) {
    elements.updateModalVersion.textContent = `v${update.version}`;
  }
  if (elements.updateModalNotesContainer) {
    if (update.body && update.body.trim().length > 0) {
      elements.updateModalNotesContainer.textContent = update.body;
      elements.updateModalNotesContainer.classList.remove("hidden");
    } else {
      elements.updateModalNotesContainer.classList.add("hidden");
    }
  }
  if (elements.updateProgressContainer) {
    elements.updateProgressContainer.classList.add("hidden");
  }
  if (elements.updateModalActions) {
    elements.updateModalActions.classList.remove("hidden");
  }
  elements.updateModal.classList.remove("hidden");
}

function hideUpdateModal() {
  if (!elements.updateModal) return;
  elements.updateModal.classList.add("hidden");
}

async function checkForUpdates(isUserClick = false) {
  if (isCheckingUpdates || isDownloadingUpdate) return;
  isCheckingUpdates = true;
  setUpdateBtnState("checking");
  logToBackend(`checkForUpdates: start (userClick=${isUserClick})`);

  try {
    const update = await check();
    if (update) {
      logToBackend(`checkForUpdates: update available: v${update.version}`);
      currentPendingUpdate = update;
      setUpdateBtnState("available");
      showUpdateModal(update);
    } else {
      logToBackend("checkForUpdates: up to date");
      currentPendingUpdate = null;
      setUpdateBtnState("up-to-date");
      setTimeout(() => {
        if (!currentPendingUpdate) setUpdateBtnState("default");
      }, 4000);
    }
  } catch (err) {
    logToBackend(`checkForUpdates: error: ${err?.message || err}`);
    console.warn("Update check response:", err);
    const errMsg = String(err?.message || err || "").toLowerCase();
    // In GitHub releases, if release 1 hasn't been posted yet, 404, or remote is not reachable, app is up to date
    if (
      errMsg.includes("404") ||
      errMsg.includes("not found") ||
      errMsg.includes("no release") ||
      errMsg.includes("could not fetch") ||
      errMsg.includes("status code") ||
      errMsg.includes("network")
    ) {
      currentPendingUpdate = null;
      setUpdateBtnState("up-to-date", "Up to Date");
      setTimeout(() => {
        if (!currentPendingUpdate) setUpdateBtnState("default");
      }, 4000);
    } else {
      setUpdateBtnState("error", "Check Failed");
      setTimeout(() => {
        if (!currentPendingUpdate) setUpdateBtnState("default");
      }, 4000);
    }
  } finally {
    isCheckingUpdates = false;
  }
}

async function installUpdate() {
  if (!currentPendingUpdate || isDownloadingUpdate) return;
  isDownloadingUpdate = true;

  if (elements.updateProgressContainer) {
    elements.updateProgressContainer.classList.remove("hidden");
  }
  if (elements.updateModalActions) {
    elements.updateModalActions.classList.add("hidden");
  }
  if (elements.btnCheckUpdatesText) {
    elements.btnCheckUpdatesText.textContent = "Downloading...";
  }

  let totalBytes = 0;
  let downloadedBytes = 0;

  try {
    await currentPendingUpdate.downloadAndInstall((event) => {
      if (event.event === "Started") {
        totalBytes = event.data?.contentLength || 0;
        if (elements.updateProgressStatus) {
          elements.updateProgressStatus.textContent = "Downloading update...";
        }
      } else if (event.event === "Progress") {
        downloadedBytes += event.data?.chunkLength || 0;
        let percent = totalBytes > 0 ? Math.min(100, Math.round((downloadedBytes / totalBytes) * 100)) : 0;
        if (elements.updateProgressPercent) {
          elements.updateProgressPercent.textContent = `${percent}%`;
        }
        if (elements.updateProgressBar) {
          elements.updateProgressBar.style.width = `${percent}%`;
        }
        if (elements.btnCheckUpdatesText) {
          elements.btnCheckUpdatesText.textContent = `Updating (${percent}%)...`;
        }
      } else if (event.event === "Finished") {
        if (elements.updateProgressStatus) {
          elements.updateProgressStatus.textContent = "Installing and restarting launcher...";
        }
        if (elements.btnCheckUpdatesText) {
          elements.btnCheckUpdatesText.textContent = "Restarting...";
        }
      }
    });
  } catch (err) {
    console.error("Failed to install update:", err);
    if (elements.updateProgressStatus) {
      elements.updateProgressStatus.textContent = "Installation error: " + (err?.message || err);
    }
    if (elements.updateModalActions) {
      elements.updateModalActions.classList.remove("hidden");
    }
    isDownloadingUpdate = false;
  }
}

// Updater event listeners
if (elements.btnCheckUpdates) {
  elements.btnCheckUpdates.addEventListener("click", () => {
    if (currentPendingUpdate) {
      showUpdateModal(currentPendingUpdate);
    } else {
      checkForUpdates(true);
    }
  });
}

if (elements.btnCloseUpdateModal) {
  elements.btnCloseUpdateModal.addEventListener("click", hideUpdateModal);
}
if (elements.btnUpdateLater) {
  elements.btnUpdateLater.addEventListener("click", hideUpdateModal);
}
if (elements.btnUpdateNow) {
  elements.btnUpdateNow.addEventListener("click", installUpdate);
}

// ==========================================
// Initialization
// ==========================================
async function init() {
  logToBackend("init() started");
  try {
    const loaded = await invoke("load_settings");
    if (loaded && typeof loaded === "object") {
      currentSettings = { ...defaultSettings, ...loaded };
    }
  } catch (err) {
    console.warn("Could not load settings:", err);
    logToBackend("Could not load settings: " + err);
  }
  populateForm(currentSettings);
  await updateGameStatus();
  await updateMaximizeIcon();
  logToBackend("init() completed successfully");

  // Note: Automatic silent check for updates on startup was disabled to prevent
  // idle compositor freezes on AMD GPUs. Updates can be checked manually via "Check for Updates".
}

init();
