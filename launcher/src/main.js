import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";

// Default configuration structure
const defaultSettings = {
  WindowSize: "1920x1080",
  Fullscreen: false,
  RenderScale: 2,
  RenderingPath: "Default",
  FrameRateMode: "60",
  VSync: true,
  PostEffect: "fxaa",
  Upscaler: "fsr",
  SkipIntroLogos: true,
  SkipCutscenes: true,
  CutsceneSpeed: 4,
  InputMode: "Controller",
  MouseSensitivity: 1.0,
  RecordPerformance: true,
  GameDataRoot: "game-data-usa",
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
  gameStatusBadge: document.getElementById("gameStatusBadge"),
  gameStatusBadgeText: document.getElementById("gameStatusBadgeText"),
  setupWarningBanner: document.getElementById("setupWarningBanner"),
  btnGoToSetup: document.getElementById("btnGoToSetup"),

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
  if (elements.windowSize) elements.windowSize.value = settings.WindowSize || "1920x1080";
  if (elements.fullscreen) elements.fullscreen.value = String(!!settings.Fullscreen);
  if (elements.renderScale) elements.renderScale.value = String(settings.RenderScale || 2);
  if (elements.frameRateMode) elements.frameRateMode.value = settings.FrameRateMode || "60";
  if (elements.renderingPath) elements.renderingPath.value = settings.RenderingPath || "Default";
  if (elements.upscaler) elements.upscaler.value = settings.Upscaler || "fsr";
  if (elements.postEffect) elements.postEffect.value = settings.PostEffect || "fxaa";
  if (elements.vsync) elements.vsync.checked = settings.VSync ?? true;
  if (elements.inputMode) elements.inputMode.value = settings.InputMode || "Controller";
  if (elements.cutsceneSpeed) elements.cutsceneSpeed.value = String(settings.CutsceneSpeed || 4);
  if (elements.skipCutscenes) elements.skipCutscenes.checked = settings.SkipCutscenes ?? true;
  if (elements.skipIntroLogos) elements.skipIntroLogos.checked = settings.SkipIntroLogos ?? true;
  if (elements.gameDataRoot) elements.gameDataRoot.value = settings.GameDataRoot || "game-data-usa";
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
    WindowSize: elements.windowSize?.value || "1920x1080",
    Fullscreen: elements.fullscreen ? elements.fullscreen.value === "true" : false,
    RenderScale: safeInt(elements.renderScale?.value, 2),
    RenderingPath: elements.renderingPath?.value || "Default",
    FrameRateMode: elements.frameRateMode?.value || "60",
    VSync: elements.vsync ? elements.vsync.checked : true,
    PostEffect: elements.postEffect?.value || "fxaa",
    Upscaler: elements.upscaler?.value || "fsr",
    SkipIntroLogos: elements.skipIntroLogos ? elements.skipIntroLogos.checked : true,
    SkipCutscenes: elements.skipCutscenes ? elements.skipCutscenes.checked : true,
    CutsceneSpeed: safeInt(elements.cutsceneSpeed?.value, 4),
    InputMode: elements.inputMode?.value || "Controller",
    MouseSensitivity: safeFloat(currentSettings.MouseSensitivity, 1.0),
    RecordPerformance: currentSettings.RecordPerformance ?? true,
    GameDataRoot: elements.gameDataRoot?.value ? elements.gameDataRoot.value.trim() : "game-data-usa",
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
          "flex items-center gap-1.5 px-2.5 py-1 rounded text-xs font-medium border bg-emerald-950/60 text-emerald-300 border-emerald-800/80";
        elements.gameStatusBadge.innerHTML = `
          <span class="w-2 h-2 rounded-full bg-emerald-400"></span>
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
        ? "font-bold text-emerald-400"
        : "font-bold text-rose-400";

      elements.statusImage.textContent = status.hasUnpackedImage ? "Unpacked (Ready)" : "Pending";
      elements.statusImage.className = status.hasUnpackedImage
        ? "font-bold text-emerald-400"
        : "font-bold text-amber-400";

      elements.statusBinary.textContent = status.hasExecutable ? "Compiled (Ready)" : "Missing";
      elements.statusBinary.className = status.hasExecutable
        ? "font-bold text-emerald-400"
        : "font-bold text-rose-400";
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
// Initialization
// ==========================================
async function init() {
  try {
    const loaded = await invoke("load_settings");
    if (loaded && typeof loaded === "object") {
      currentSettings = { ...defaultSettings, ...loaded };
    }
  } catch (err) {
    console.warn("Could not load settings:", err);
  }
  populateForm(currentSettings);
  await updateGameStatus();
  await updateMaximizeIcon();
}

init();
