import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { check } from "@tauri-apps/plugin-updater";
import { initCustomSelects, refreshCustomSelect } from "./customSelect.js";

let isDevModeCached = false;

// Determine dev mode immediately to avoid unwanted telemetry IPC in production release
invoke("is_dev_mode")
  .then((dev) => {
    isDevModeCached = !!dev;
    if (isDevModeCached) {
      logToBackend(
        `JS Init: innerSize=${window.innerWidth}x${window.innerHeight}, screen=${screen.width}x${screen.height}, dpr=${window.devicePixelRatio}, ua=${navigator.userAgent}`
      );
    }
  })
  .catch(() => {});

function logToBackend(msg) {
  if (!isDevModeCached) return;
  invoke("log_message", { msg: String(msg) }).catch(() => {});
}

// Global JS error & rejection logging
window.addEventListener("error", (e) => {
  logToBackend(`GLOBAL_ERROR: ${e.message} at ${e.filename}:${e.lineno}:${e.colno}`);
});
window.addEventListener("unhandledrejection", (e) => {
  const reasonStr = String(e.reason?.stack || e.reason || "");
  if (reasonStr.includes("start_dragging")) {
    return; // Ignore benign drag race
  }
  logToBackend(`UNHANDLED_REJECTION: ${reasonStr}`);
});

// Compositor Keep-Alive: keeps WebView2 swapchain active on idle to prevent DWM/MPO blank screen.
// A canvas 2D draw guarantees that Chromium's cc/Viz damage tracker registers dirty rects every second.
const heartbeatEl = document.createElement("div");
heartbeatEl.id = "compositor-heartbeat";
heartbeatEl.style.cssText =
  "position:fixed;bottom:0;right:0;width:1px;height:1px;pointer-events:none;opacity:0.01;z-index:99999;transform:translateZ(0);";
document.body.appendChild(heartbeatEl);

const keepAliveCanvas = document.createElement("canvas");
keepAliveCanvas.id = "compositor-canvas-keepalive";
keepAliveCanvas.width = 1;
keepAliveCanvas.height = 1;
keepAliveCanvas.style.cssText =
  "position:fixed;bottom:0;right:0;width:1px;height:1px;pointer-events:none;opacity:0.01;z-index:99999;";
document.body.appendChild(keepAliveCanvas);
const keepAliveCtx = keepAliveCanvas.getContext("2d");

let heartbeatTick = false;
let heartbeatCounter = 0;
setInterval(() => {
  heartbeatTick = !heartbeatTick;
  heartbeatCounter++;
  heartbeatEl.style.transform = heartbeatTick ? "translateZ(0)" : "translateZ(1px)";
  if (keepAliveCtx) {
    keepAliveCtx.fillStyle = heartbeatTick ? "#0f172a" : "#0f172b";
    keepAliveCtx.fillRect(0, 0, 1, 1);
  }
  if (isDevModeCached && heartbeatCounter % 3 === 0) {
    logToBackend(
      `JS Heartbeat #${heartbeatCounter} (${heartbeatCounter}s): visibility=${document.visibilityState}, active=<${document.activeElement?.tagName || "none"}>`
    );
  }
}, 1000);

// Watchdog: manual reload keyboard shortcut (F5 or Ctrl+R)
window.addEventListener("keydown", (e) => {
  if (isDevModeCached) {
    logToBackend(`JS KeyDown: key='${e.key}' code='${e.code}' ctrl=${e.ctrlKey} alt=${e.altKey}`);
  }
  if (e.key === "F5" || (e.ctrlKey && e.key.toLowerCase() === "r")) {
    e.preventDefault();
    if (isDevModeCached) {
      logToBackend("Manual reload triggered via keyboard shortcut");
    }
    window.location.reload();
  }
});

// User Click Diagnostic Logging
window.addEventListener("click", (e) => {
  if (isDevModeCached) {
    logToBackend(`User Click at (${e.clientX}, ${e.clientY}) on <${e.target?.tagName || "unknown"}> id='${e.target?.id || ""}'`);
  }
});

// Header double-click emergency recovery
document.addEventListener("DOMContentLoaded", () => {
  const header = document.getElementById("appHeader");
  if (header) {
    header.addEventListener("dblclick", () => {
      if (isDevModeCached) {
        logToBackend("Header double-click emergency recovery: reload()");
      }
      window.location.reload();
    });
  }
});

// Default configuration structure
const defaultSettings = {
  WindowSize: "1280x720",
  Fullscreen: false,
  RenderScale: 1,
  RenderingPath: "Vulkan",
  FrameRateMode: "30",
  VSync: true,
  PresentDither: false,
  PostEffect: "dedither_fxaa",
  DeDitherStrength: 0.85,
  Upscaler: "bilinear",
  CasSharpness: 0.0,
  FsrReduction: 0.0,
  SkipIntroLogos: false,
  SkipCutscenes: false,
  CutsceneSpeed: 4,
  InputMode: "Controller",
  MouseSensitivity: 1.0,
  RecordPerformance: false,
  GameDataRoot: "game-data",
  Configuration: "Release",
  Language: "English",
  StepFix: false,
  AnimSpeedFix: false,
  DtSmoothing: true,
  FovScale: 1.0,
  DrawDistanceScale: 3.0,
  DepthOfField: false,
  Bloom: true,
  BloomIntensity: 1.0,
  PauseDistortion: true,
  WindowFps: false,
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
  tabBtnGraphics: document.getElementById("tabBtnGraphics"),
  tabBtnControls: document.getElementById("tabBtnControls"),
  tabBtnSettings: document.getElementById("tabBtnGraphics"),
  tabBtnFixes: document.getElementById("tabBtnFixes"),
  tabBtnBuild: document.getElementById("tabBtnBuild"),
  tabGraphics: document.getElementById("tabGraphics"),
  tabControls: document.getElementById("tabControls"),
  tabSettings: document.getElementById("tabGraphics"),
  tabFixes: document.getElementById("tabFixes"),
  tabBuild: document.getElementById("tabBuild"),
  tabBuildIndicator: document.getElementById("tabBuildIndicator"),

  // Header & Status
  appVersionBadge: document.getElementById("appVersionBadge"),
  btnCheckUpdates: document.getElementById("btnCheckUpdates"),
  btnCheckUpdatesText: document.getElementById("btnCheckUpdatesText"),
  btnBuyMeCoffee: document.getElementById("btnBuyMeCoffee"),
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
  upscalerSharpeningContainer: document.getElementById("upscalerSharpeningContainer"),
  casSharpness: document.getElementById("casSharpness"),
  casSharpnessVal: document.getElementById("casSharpnessVal"),
  casSharpnessBlock: document.getElementById("casSharpnessBlock"),
  fsrReduction: document.getElementById("fsrReduction"),
  fsrReductionVal: document.getElementById("fsrReductionVal"),
  fsrReductionBlock: document.getElementById("fsrReductionBlock"),
  postEffect: document.getElementById("postEffect"),
  deDitherStrength: document.getElementById("deDitherStrength"),
  deDitherStrengthVal: document.getElementById("deDitherStrengthVal"),
  deDitherStrengthContainer: document.getElementById("deDitherStrengthContainer"),
  postProcessingSlidersRow: document.getElementById("postProcessingSlidersRow"),
  deDitherDisabledBadge: document.getElementById("deDitherDisabledBadge"),
  vsync: document.getElementById("vsync"),
  presentDither: document.getElementById("presentDither"),
  windowFps: document.getElementById("windowFps"),
  inputMode: document.getElementById("inputMode"),
  cutsceneSpeed: document.getElementById("cutsceneSpeed"),
  mouseSensitivityContainer: document.getElementById("mouseSensitivityContainer"),
  mouseSensitivity: document.getElementById("mouseSensitivity"),
  mouseSensitivityVal: document.getElementById("mouseSensitivityVal"),
  skipCutscenes: document.getElementById("skipCutscenes"),
  skipIntroLogos: document.getElementById("skipIntroLogos"),
  gameDataRoot: document.getElementById("gameDataRoot"),
  language: document.getElementById("language"),

  // Fixes & Graphics form elements
  stepFix: document.getElementById("stepFix"),
  dtSmoothing: document.getElementById("dtSmoothing"),
  animSpeedFix: document.getElementById("animSpeedFix"),
  fovScale: document.getElementById("fovScale"),
  fovScaleVal: document.getElementById("fovScaleVal"),
  drawDistanceScale: document.getElementById("drawDistanceScale"),
  drawDistanceScaleVal: document.getElementById("drawDistanceScaleVal"),
  depthOfField: document.getElementById("depthOfField"),
  bloomIntensity: document.getElementById("bloomIntensity"),
  bloomIntensityVal: document.getElementById("bloomIntensityVal"),
  pauseDistortion: document.getElementById("pauseDistortion"),

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
  btnToggleLog: document.getElementById("btnToggleLog"),
  toggleLogText: document.getElementById("toggleLogText"),
  toggleLogIcon: document.getElementById("toggleLogIcon"),
  terminalLogContainer: document.getElementById("terminalLogContainer"),
  btnCopyLog: document.getElementById("btnCopyLog"),
  btnCopyLogText: document.getElementById("btnCopyLogText"),
  btnClearLog: document.getElementById("btnClearLog"),

  // Window Control Buttons
  btnWindowMinimize: document.getElementById("btnWindowMinimize"),
  btnWindowMaximize: document.getElementById("btnWindowMaximize"),
  btnWindowClose: document.getElementById("btnWindowClose"),
  iconMaximize: document.getElementById("iconMaximize"),
  iconRestore: document.getElementById("iconRestore"),

  // Footer & Action Buttons
  btnLaunch: document.getElementById("btnLaunch"),
  btnLaunchText: document.getElementById("btnLaunchText"),
  btnPrecompile: document.getElementById("btnPrecompile"),
  btnPrecompileText: document.getElementById("btnPrecompileText"),
  btnCleanShaders: document.getElementById("btnCleanShaders"),
  precompileStatusIcon: document.getElementById("precompileStatusIcon"),

  // Precompile Modal Elements
  precompileModal: document.getElementById("precompileModal"),
  btnClosePrecompileModal: document.getElementById("btnClosePrecompileModal"),
  btnPrecompileDone: document.getElementById("btnPrecompileDone"),
  precompileSpinner: document.getElementById("precompileSpinner"),
  precompileStatusText: document.getElementById("precompileStatusText"),
  precompilePercentText: document.getElementById("precompilePercentText"),
  precompileProgressBar: document.getElementById("precompileProgressBar"),
  precompileConsole: document.getElementById("precompileConsole"),
  btnClearPrecompileLog: document.getElementById("btnClearPrecompileLog"),

  // Dev Shader Cache Elements
  devShaderCacheCard: document.getElementById("devShaderCacheCard"),
  devShaderTotalSize: document.getElementById("devShaderTotalSize"),
  devShaderPipelineCount: document.getElementById("devShaderPipelineCount"),
  devShaderVkpcSize: document.getElementById("devShaderVkpcSize"),
  devShaderXshSize: document.getElementById("devShaderXshSize"),
  devShaderLastUpdated: document.getElementById("devShaderLastUpdated"),
  btnRefreshShaderCache: document.getElementById("btnRefreshShaderCache"),
};

// ==========================================
// Tab Switching
// ==========================================
function switchTab(tab) {
  const activeClass =
    "w-full h-11 flex items-center gap-3 px-4 py-3 rounded-xl text-sm font-bold bg-[#6da712]/20 text-[#b5de63] transition-all duration-200 cursor-pointer shadow-sm text-left";
  const inactiveClass =
    "w-full h-11 flex items-center gap-3 px-4 py-3 rounded-xl text-sm font-semibold text-slate-400 hover:text-slate-200 hover:bg-darkControl/60 transition-all duration-200 cursor-pointer text-left";

  const target = tab === "settings" ? "graphics" : tab;

  const tabList = [
    { key: "graphics", el: elements.tabGraphics, btn: elements.tabBtnGraphics },
    { key: "controls", el: elements.tabControls, btn: elements.tabBtnControls },
    { key: "fixes", el: elements.tabFixes, btn: elements.tabBtnFixes },
    { key: "build", el: elements.tabBuild, btn: elements.tabBtnBuild },
  ];

  tabList.forEach(({ key, el, btn }) => {
    if (!el || !btn) return;
    if (key === target) {
      const wasHidden = el.classList.contains("hidden");
      el.classList.remove("hidden");
      if (wasHidden) {
        el.classList.remove("tab-content-active");
        void el.offsetWidth;
        el.classList.add("tab-content-active");
      }
      btn.className = activeClass;
    } else {
      el.classList.add("hidden");
      el.classList.remove("tab-content-active");
      btn.className = inactiveClass;
    }
  });

  if (target === "build") {
    updateShaderCacheInfo();
  } else if (target === "controls") {
    updateConnectedGamepads();
  }
}

if (elements.tabBtnGraphics) elements.tabBtnGraphics.addEventListener("click", () => switchTab("graphics"));
if (elements.tabBtnControls) elements.tabBtnControls.addEventListener("click", () => switchTab("controls"));
if (elements.tabBtnFixes) elements.tabBtnFixes.addEventListener("click", () => switchTab("fixes"));
if (elements.tabBtnBuild) elements.tabBtnBuild.addEventListener("click", () => switchTab("build"));
if (elements.btnGoToSetup) elements.btnGoToSetup.addEventListener("click", () => switchTab("build"));

function safeFloat(val, fallback) {
  const num = typeof val === "number" ? val : parseFloat(val);
  return Number.isFinite(num) ? num : fallback;
}

function safeInt(val, fallback) {
  const num = typeof val === "number" ? val : parseInt(val, 10);
  return Number.isFinite(num) ? num : fallback;
}

function updatePostProcessingVisibility() {
  if (!elements.postEffect || !elements.upscaler) return;

  const postVal = String(elements.postEffect.value || "none");
  const isDeDither = postVal.startsWith("dedither");

  const upVal = String(elements.upscaler.value || "bilinear");
  const hasSharpening = upVal === "cas" || upVal === "fsr";

  // De-Dither intensity slider visibility
  if (elements.deDitherStrengthContainer) {
    elements.deDitherStrengthContainer.classList.toggle("hidden", !isDeDither);
    if (elements.deDitherStrength) {
      elements.deDitherStrength.disabled = !isDeDither;
    }
  }

  // Upscaler sharpening slider visibility
  if (elements.upscalerSharpeningContainer) {
    elements.upscalerSharpeningContainer.classList.toggle("hidden", !hasSharpening);
  }

  // Sub-blocks for CAS / FSR
  if (elements.casSharpnessBlock) {
    elements.casSharpnessBlock.classList.toggle("hidden", upVal !== "cas");
  }
  if (elements.fsrReductionBlock) {
    elements.fsrReductionBlock.classList.toggle("hidden", upVal !== "fsr");
  }

  // Entire row visibility
  if (elements.postProcessingSlidersRow) {
    elements.postProcessingSlidersRow.classList.toggle("hidden", !isDeDither && !hasSharpening);
  }
}

function updateDeDitherVisibility() {
  updatePostProcessingVisibility();
}

function updateUpscalerVisibility() {
  updatePostProcessingVisibility();
}

function updateSliderFill(slider) {
  if (!slider) return;
  const min = parseFloat(slider.min);
  const max = parseFloat(slider.max);
  const val = parseFloat(slider.value);
  const minVal = isNaN(min) ? 0 : min;
  const maxVal = isNaN(max) ? 100 : max;
  const currentVal = isNaN(val) ? minVal : val;
  const range = maxVal - minVal;
  const pct = range > 0 ? Math.max(0, Math.min(100, ((currentVal - minVal) / range) * 100)) : 0;
  slider.style.setProperty("--slider-pct", `${pct}%`);
  slider.style.background = `linear-gradient(to right, #38bdf8 0%, #38bdf8 ${pct}%, #232734 ${pct}%, #232734 100%)`;
}

function updateAllSliderFills() {
  document.querySelectorAll('input[type="range"]').forEach(updateSliderFill);
}

function updateInputModeVisibility() {
  if (!elements.inputMode || !elements.mouseSensitivityContainer) return;
  const isKbm = elements.inputMode.value === "KeyboardMouse";
  elements.mouseSensitivityContainer.classList.toggle("hidden", !isKbm);
  if (isKbm && elements.mouseSensitivity) {
    updateSliderFill(elements.mouseSensitivity);
  }
}

let lastConnectedGamepadsKey = null;

async function updateConnectedGamepads(preferredMode = null) {
  if (!elements.inputMode) return;

  let gamepads = [];
  try {
    const nativeGamepads = await invoke("get_connected_gamepads");
    if (Array.isArray(nativeGamepads)) {
      gamepads = [...nativeGamepads];
    }
  } catch (err) {
    if (isDevModeCached) {
      logToBackend(`Error fetching gamepads: ${err}`);
    }
    // Fallback only if native invoke is unavailable (e.g. standalone browser environment)
    if (typeof navigator !== "undefined" && typeof navigator.getGamepads === "function") {
      try {
        const webPads = navigator.getGamepads();
        if (webPads) {
          for (const pad of webPads) {
            if (pad && pad.connected && pad.id) {
              let name = pad.id.replace(/\s*\([^)]*\)/g, "").trim();
              if (name && !gamepads.includes(name)) {
                gamepads.push(name);
              }
            }
          }
        }
      } catch (_) {}
    }
  }

  // Deduplicate generic entries if a specific model was identified
  if (gamepads.some((g) => g.startsWith("Xbox ") && g !== "Xbox Controller")) {
    gamepads = gamepads.filter((g) => g !== "Xbox Controller");
  }
  if (gamepads.some((g) => g.startsWith("Sony ") && g !== "PlayStation Controller")) {
    gamepads = gamepads.filter((g) => g !== "PlayStation Controller");
  }
  if (gamepads.some((g) => g.startsWith("GameSir ") && g !== "GameSir Controller")) {
    gamepads = gamepads.filter((g) => g !== "GameSir Controller");
  }
  if (gamepads.some((g) => g !== "Wireless / USB Controller" && g !== "Joystick Controller")) {
    gamepads = gamepads.filter((g) => g !== "Wireless / USB Controller" && g !== "Joystick Controller");
  }

  const currentKey = gamepads.join("|||");
  if (currentKey === lastConnectedGamepadsKey && preferredMode === null) {
    return;
  }
  lastConnectedGamepadsKey = currentKey;

  const currentSelection = preferredMode || elements.inputMode.value;
  elements.inputMode.innerHTML = "";

  if (gamepads.length > 0) {
    gamepads.forEach((gpName) => {
      const opt = document.createElement("option");
      opt.value = "Controller";
      opt.textContent = gpName;
      elements.inputMode.appendChild(opt);
    });

    const kbmOpt = document.createElement("option");
    kbmOpt.value = "KeyboardMouse";
    kbmOpt.textContent = "Keyboard & Mouse";
    elements.inputMode.appendChild(kbmOpt);

    if (currentSelection === "KeyboardMouse") {
      elements.inputMode.value = "KeyboardMouse";
    } else {
      elements.inputMode.value = "Controller";
    }
  } else {
    const kbmOpt = document.createElement("option");
    kbmOpt.value = "KeyboardMouse";
    kbmOpt.textContent = "Keyboard & Mouse";
    elements.inputMode.appendChild(kbmOpt);
    elements.inputMode.value = "KeyboardMouse";
  }

  refreshCustomSelect(elements.inputMode);
  updateInputModeVisibility();
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
    const rp = settings.RenderingPath || "Vulkan";
    elements.renderingPath.value = (rp === "Vulkan_FSI") ? "Vulkan" : rp;
  }
  if (elements.upscaler) elements.upscaler.value = settings.Upscaler || "bilinear";
  if (elements.casSharpness) {
    const casVal = Math.min(1.0, Math.max(0.0, safeFloat(settings.CasSharpness, 0.0)));
    elements.casSharpness.value = casVal.toFixed(2);
    if (elements.casSharpnessVal) {
      elements.casSharpnessVal.textContent = casVal.toFixed(2);
    }
  }
  if (elements.fsrReduction) {
    const fsrVal = Math.min(2.0, Math.max(0.0, safeFloat(settings.FsrReduction, 0.0)));
    elements.fsrReduction.value = fsrVal.toFixed(2);
    if (elements.fsrReductionVal) {
      elements.fsrReductionVal.textContent = fsrVal.toFixed(2) + " stops";
    }
  }
  updateUpscalerVisibility();
  if (elements.postEffect) elements.postEffect.value = settings.PostEffect || "dedither_fxaa";
  if (elements.deDitherStrength) {
    const strength = Math.min(1.0, Math.max(0.0, safeFloat(settings.DeDitherStrength, 0.85)));
    elements.deDitherStrength.value = strength.toFixed(2);
    if (elements.deDitherStrengthVal) {
      elements.deDitherStrengthVal.textContent = strength.toFixed(2);
    }
  }
  updateDeDitherVisibility();
  if (elements.vsync) elements.vsync.checked = settings.VSync ?? true;
  if (elements.presentDither) elements.presentDither.checked = settings.PresentDither ?? false;
  if (elements.windowFps) elements.windowFps.checked = settings.WindowFps ?? false;
  updateConnectedGamepads(settings.InputMode || "Controller");
  if (elements.cutsceneSpeed) elements.cutsceneSpeed.value = String(settings.CutsceneSpeed || 4);
  if (elements.mouseSensitivity) {
    const sens = safeFloat(settings.MouseSensitivity, 1.0);
    elements.mouseSensitivity.value = String(sens);
    if (elements.mouseSensitivityVal) {
      elements.mouseSensitivityVal.textContent = sens.toFixed(1) + "x";
    }
  }
  if (elements.skipCutscenes) elements.skipCutscenes.checked = settings.SkipCutscenes ?? false;
  if (elements.skipIntroLogos) elements.skipIntroLogos.checked = settings.SkipIntroLogos ?? false;
  if (elements.gameDataRoot) elements.gameDataRoot.value = settings.GameDataRoot || "game-data";
  if (elements.language) elements.language.value = settings.Language || "English";

  // Visuals, Camera & Fixes form
  if (elements.stepFix) elements.stepFix.checked = settings.StepFix ?? false;
  if (elements.animSpeedFix) elements.animSpeedFix.checked = settings.AnimSpeedFix ?? false;
  if (elements.dtSmoothing) elements.dtSmoothing.checked = settings.DtSmoothing ?? true;
  if (elements.fovScale) {
    const targetFov = Math.min(1.50, Math.max(0.80, safeFloat(settings.FovScale, 1.0)));
    elements.fovScale.value = targetFov.toFixed(2);
    if (elements.fovScaleVal) {
      elements.fovScaleVal.textContent = targetFov.toFixed(2) + "x";
    }
  }
  if (elements.drawDistanceScale) {
    const targetDist = Math.min(3.0, Math.max(0.5, safeFloat(settings.DrawDistanceScale, 3.0)));
    elements.drawDistanceScale.value = targetDist.toFixed(2);
    if (elements.drawDistanceScaleVal) {
      elements.drawDistanceScaleVal.textContent = targetDist.toFixed(2) + "x";
    }
  }
  if (elements.depthOfField) elements.depthOfField.checked = settings.DepthOfField ?? false;
  if (elements.pauseDistortion) elements.pauseDistortion.checked = settings.PauseDistortion ?? true;
  if (elements.bloomIntensity) {
    const bloomVal = settings.BloomIntensity !== undefined
      ? safeFloat(settings.BloomIntensity, 1.0)
      : (settings.Bloom ? 1.0 : 0.0);
    const clamped = Math.min(2.0, Math.max(0.0, bloomVal));
    elements.bloomIntensity.value = clamped.toFixed(2);
    if (elements.bloomIntensityVal) {
      elements.bloomIntensityVal.textContent = clamped <= 0.001 ? "Off" : clamped.toFixed(2);
    }
  }

  updateAllSliderFills();
}

function readForm() {
  return {
    WindowSize: elements.windowSize?.value || "1280x720",
    Fullscreen: elements.fullscreen ? elements.fullscreen.value === "true" : false,
    RenderScale: safeInt(elements.renderScale?.value, 1),
    RenderingPath: elements.renderingPath?.value || "Vulkan",
    FrameRateMode: elements.frameRateMode?.value || "30",
    VSync: elements.vsync ? elements.vsync.checked : true,
    PresentDither: elements.presentDither ? elements.presentDither.checked : false,
    WindowFps: elements.windowFps ? elements.windowFps.checked : false,
    PostEffect: elements.postEffect?.value || "dedither_fxaa",
    DeDitherStrength: elements.deDitherStrength ? safeFloat(elements.deDitherStrength.value, 0.85) : safeFloat(currentSettings.DeDitherStrength, 0.85),
    Upscaler: elements.upscaler?.value || "bilinear",
    CasSharpness: elements.casSharpness ? safeFloat(elements.casSharpness.value, 0.0) : safeFloat(currentSettings.CasSharpness, 0.0),
    FsrReduction: elements.fsrReduction ? safeFloat(elements.fsrReduction.value, 0.0) : safeFloat(currentSettings.FsrReduction, 0.0),
    SkipIntroLogos: elements.skipIntroLogos ? elements.skipIntroLogos.checked : false,
    SkipCutscenes: elements.skipCutscenes ? elements.skipCutscenes.checked : false,
    CutsceneSpeed: safeInt(elements.cutsceneSpeed?.value, 4),
    InputMode: elements.inputMode?.value || "Controller",
    MouseSensitivity: elements.mouseSensitivity ? safeFloat(elements.mouseSensitivity.value, 1.0) : safeFloat(currentSettings.MouseSensitivity, 1.0),
    RecordPerformance: currentSettings.RecordPerformance ?? false,
    GameDataRoot: elements.gameDataRoot?.value ? elements.gameDataRoot.value.trim() : "game-data",
    Configuration: "Release",
    Language: elements.language?.value || currentSettings.Language || "English",
    StepFix: elements.stepFix ? elements.stepFix.checked : false,
    AnimSpeedFix: elements.animSpeedFix ? elements.animSpeedFix.checked : false,
    DtSmoothing: elements.dtSmoothing ? elements.dtSmoothing.checked : true,
    FovScale: safeFloat(elements.fovScale?.value, 1.0),
    DrawDistanceScale: safeFloat(elements.drawDistanceScale?.value, 3.0),
    DepthOfField: elements.depthOfField ? elements.depthOfField.checked : false,
    PauseDistortion: elements.pauseDistortion ? elements.pauseDistortion.checked : true,
    Bloom: elements.bloomIntensity ? safeFloat(elements.bloomIntensity.value, 1.0) > 0.001 : true,
    BloomIntensity: elements.bloomIntensity ? safeFloat(elements.bloomIntensity.value, 1.0) : safeFloat(currentSettings.BloomIntensity, 1.0),
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
          "inline-flex items-center gap-1.5 px-2.5 py-1 rounded-lg text-xs font-normal border bg-[#6da712]/15 text-[#a3cc54] border-[#6da712]/40 select-none";
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
          "inline-flex items-center gap-1.5 px-2.5 py-1 rounded-lg text-xs font-normal border bg-amber-950/60 text-amber-300 border-amber-800/80 select-none";
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
function setTerminalLogOpen(open) {
  if (!elements.terminalLogContainer) return;
  if (open) {
    elements.terminalLogContainer.classList.remove("hidden");
    if (elements.toggleLogText) elements.toggleLogText.textContent = "Hide Terminal Log";
    if (elements.toggleLogIcon) elements.toggleLogIcon.classList.add("rotate-180");
  } else {
    elements.terminalLogContainer.classList.add("hidden");
    if (elements.toggleLogText) elements.toggleLogText.textContent = "Show Terminal Log";
    if (elements.toggleLogIcon) elements.toggleLogIcon.classList.remove("rotate-180");
  }
}

if (elements.btnToggleLog) {
  elements.btnToggleLog.addEventListener("click", () => {
    const isHidden = elements.terminalLogContainer?.classList.contains("hidden");
    setTerminalLogOpen(isHidden);
  });
}

function appendConsoleLog(line) {
  const isErr = line.startsWith("ERROR") || line.startsWith("[stderr]");
  const lineDiv = document.createElement("div");
  lineDiv.className = isErr
    ? "text-rose-400 font-semibold"
    : line.startsWith("===")
    ? "text-sky-300 font-bold border-t border-slate-800 pt-1 mt-1"
    : "text-slate-300";
  lineDiv.textContent = line;
  elements.buildConsole.appendChild(lineDiv);
  elements.buildConsole.scrollTop = elements.buildConsole.scrollHeight;

  if (isErr) {
    setTerminalLogOpen(true);
  }
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

  const compileBinary = elements.chkCompileBinary ? elements.chkCompileBinary.checked : false;
  if (compileBinary) {
    const confirmCompile = confirm(
      "Recompiling the game binary requires Visual Studio 2022 with C++ x64 tools installed on this PC.\n\n" +
      "If you only want to extract and play using the included precompiled executable (majin.exe), click Cancel and leave this checkbox unchecked.\n\n" +
      "Proceed with C++ source compilation?"
    );
    if (!confirmCompile) {
      if (elements.chkCompileBinary) {
        elements.chkCompileBinary.checked = false;
      }
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
  elements.buildStageText.className = "text-xs font-semibold text-white";
  elements.buildSpinner.classList.remove("hidden");
  setTerminalLogOpen(false);

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
    setTerminalLogOpen(true);
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

  setTerminalLogOpen(true);
  await updateGameStatus();
});

// ==========================================
// Shader Precompilation
// ==========================================
let isPrecompiling = false;

function appendPrecompileLog(line) {
  if (!elements.precompileConsole) return;
  const lineDiv = document.createElement("div");
  lineDiv.className = line.startsWith("ERROR") || line.startsWith("[stderr]")
    ? "text-rose-400 font-semibold"
    : line.startsWith("===")
    ? "text-sky-300 font-bold border-t border-slate-800 pt-1 mt-1"
    : line.includes("monolithic driver pipeline cache: saved")
    ? "text-emerald-400 font-bold"
    : "text-slate-300";
  lineDiv.textContent = line;
  elements.precompileConsole.appendChild(lineDiv);
  elements.precompileConsole.scrollTop = elements.precompileConsole.scrollHeight;
}

async function startPrecompileShaders() {
  if (isPrecompiling) return;
  if (!gameStatus.isReady) {
    switchTab("build");
    return;
  }

  isPrecompiling = true;
  const settings = readForm();

  // Open modal and show initialization state
  if (elements.precompileModal) {
    elements.precompileModal.classList.remove("hidden");
  }
  if (elements.precompileSpinner) {
    elements.precompileSpinner.classList.remove("hidden");
  }
  if (elements.precompileProgressBar) {
    elements.precompileProgressBar.style.width = "5%";
  }
  if (elements.precompilePercentText) {
    elements.precompilePercentText.textContent = "5%";
  }
  if (elements.precompileStatusText) {
    elements.precompileStatusText.textContent = "Initializing Vulkan device and shader cache...";
    elements.precompileStatusText.className = "font-semibold text-slate-200";
  }
  if (elements.precompileConsole) {
    elements.precompileConsole.innerHTML = "";
  }
  if (elements.btnPrecompileDone) {
    elements.btnPrecompileDone.disabled = true;
    elements.btnPrecompileDone.textContent = "Compiling...";
  }
  if (elements.btnPrecompile) {
    elements.btnPrecompile.disabled = true;
  }
  if (elements.btnLaunch) {
    elements.btnLaunch.disabled = true;
  }

  appendPrecompileLog("[Precompiler] Initializing Vulkan backend with cataloged target pipelines...");

  try {
    await invoke("precompile_shaders", { settings });
  } catch (err) {
    appendPrecompileLog(`ERROR starting shader precompiler: ${err}`);
    if (elements.precompileStatusText) {
      elements.precompileStatusText.textContent = `Error: ${err}`;
      elements.precompileStatusText.className = "font-semibold text-rose-400";
    }
    if (elements.precompileSpinner) {
      elements.precompileSpinner.classList.add("hidden");
    }
    if (elements.btnPrecompileDone) {
      elements.btnPrecompileDone.disabled = false;
      elements.btnPrecompileDone.textContent = "Close";
    }
    if (elements.btnPrecompile) {
      elements.btnPrecompile.disabled = false;
    }
    if (elements.btnLaunch) {
      elements.btnLaunch.disabled = false;
    }
    isPrecompiling = false;
  }
}

listen("precompile-progress", (event) => {
  const { current, total, status } = event.payload;
  const pct = total > 0 ? Math.min(100, Math.round((current / total) * 100)) : 0;
  if (elements.precompileProgressBar) elements.precompileProgressBar.style.width = `${pct}%`;
  if (elements.precompilePercentText) elements.precompilePercentText.textContent = `${pct}%`;
  if (elements.precompileStatusText) elements.precompileStatusText.textContent = status;
});

listen("precompile-log", (event) => {
  appendPrecompileLog(event.payload);
});

listen("precompile-complete", () => {
  if (elements.precompileProgressBar) elements.precompileProgressBar.style.width = "100%";
  if (elements.precompilePercentText) elements.precompilePercentText.textContent = "100%";
  if (elements.precompileStatusText) {
    elements.precompileStatusText.textContent = "All Vulkan shaders compiled successfully!";
    elements.precompileStatusText.className = "font-semibold text-[#a3cc54]";
  }
  if (elements.precompileSpinner) elements.precompileSpinner.classList.add("hidden");
  if (elements.btnPrecompileDone) {
    elements.btnPrecompileDone.disabled = false;
    elements.btnPrecompileDone.textContent = "Done";
  }
  if (elements.btnPrecompile) elements.btnPrecompile.disabled = false;
  if (elements.btnLaunch) elements.btnLaunch.disabled = false;
  isPrecompiling = false;
  updateShaderCacheInfo();
});

listen("precompile-error", (event) => {
  if (elements.precompileStatusText) {
    elements.precompileStatusText.textContent = `Error: ${event.payload}`;
    elements.precompileStatusText.className = "font-semibold text-rose-400";
  }
  if (elements.precompileSpinner) elements.precompileSpinner.classList.add("hidden");
  if (elements.btnPrecompileDone) {
    elements.btnPrecompileDone.disabled = false;
    elements.btnPrecompileDone.textContent = "Close";
  }
  if (elements.btnPrecompile) elements.btnPrecompile.disabled = false;
  if (elements.btnLaunch) elements.btnLaunch.disabled = false;
  isPrecompiling = false;
  updateShaderCacheInfo();
});

if (elements.btnPrecompile) {
  elements.btnPrecompile.addEventListener("click", startPrecompileShaders);
}
if (elements.btnClosePrecompileModal) {
  elements.btnClosePrecompileModal.addEventListener("click", () => {
    if (elements.precompileModal) elements.precompileModal.classList.add("hidden");
  });
}
if (elements.btnPrecompileDone) {
  elements.btnPrecompileDone.addEventListener("click", () => {
    if (elements.precompileModal) elements.precompileModal.classList.add("hidden");
  });
}
if (elements.btnClearPrecompileLog) {
  elements.btnClearPrecompileLog.addEventListener("click", () => {
    if (elements.precompileConsole) elements.precompileConsole.innerHTML = "";
  });
}

// ==========================================
// Shader Cache & Precompilation Status
// ==========================================
const ICON_CHECK_SVG = `<svg class="w-3.5 h-3.5 text-emerald-400 shrink-0" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="currentColor"><path fill="currentColor" fill-rule="evenodd" d="M22 12C22 17.5228 17.5228 22 12 22C6.47715 22 2 17.5228 2 12C2 6.47715 6.47715 2 12 2C17.5228 2 22 6.47715 22 12ZM16.0303 8.96967C16.3232 9.26256 16.3232 9.73744 16.0303 10.0303L11.0303 15.0303C10.7374 15.3232 10.2626 15.3232 9.96967 15.0303L7.96967 13.0303C7.67678 12.7374 7.67678 12.2626 7.96967 11.9697C8.26256 11.6768 8.73744 11.6768 9.03033 11.9697L10.5 13.4393L12.7348 11.2045L14.9697 8.96967C15.2626 8.67678 15.7374 8.67678 16.0303 8.96967Z" clip-rule="evenodd"/></svg>`;

const ICON_UNCHECK_SVG = `<svg class="w-3.5 h-3.5 text-rose-400 shrink-0" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="currentColor"><path fill="currentColor" fill-rule="evenodd" d="M22 12C22 17.5228 17.5228 22 12 22C6.47715 22 2 17.5228 2 12C2 6.47715 6.47715 2 12 2C17.5228 2 22 6.47715 22 12ZM8.96963 8.96965C9.26252 8.67676 9.73739 8.67676 10.0303 8.96965L12 10.9393L13.9696 8.96967C14.2625 8.67678 14.7374 8.67678 15.0303 8.96967C15.3232 9.26256 15.3232 9.73744 15.0303 10.0303L13.0606 12L15.0303 13.9696C15.3232 14.2625 15.3232 14.7374 15.0303 15.0303C14.7374 15.3232 14.2625 15.3232 13.9696 15.0303L12 13.0607L10.0303 15.0303C9.73742 15.3232 9.26254 15.3232 8.96965 15.0303C8.67676 14.7374 8.67676 14.2625 8.96965 13.9697L10.9393 12L8.96963 10.0303C8.67673 9.73742 8.67673 9.26254 8.96963 8.96965Z" clip-rule="evenodd"/></svg>`;

async function updateShaderCacheInfo() {
  try {
    const info = await invoke("get_shader_cache_info");
    if (info) {
      // 1. Update status icon on precompile button (green check vs red uncheck)
      if (elements.precompileStatusIcon) {
        elements.precompileStatusIcon.innerHTML = info.is_precompiled ? ICON_CHECK_SVG : ICON_UNCHECK_SVG;
      }

      if (elements.btnPrecompile) {
        elements.btnPrecompile.title = info.is_precompiled
          ? "Vulkan shaders precompiled in driver cache (Ready). Click to verify or re-compile."
          : "Vulkan shaders not yet precompiled. Click to precompile now.";
      }

      // 2. Enable or disable Clean button depending on whether vkpc exists
      if (elements.btnCleanShaders) {
        elements.btnCleanShaders.disabled = !info.is_precompiled;
        if (info.is_precompiled) {
          elements.btnCleanShaders.classList.remove("opacity-50", "pointer-events-none");
          elements.btnCleanShaders.title = "Purge compiled Vulkan driver cache (.vkpc) to allow a clean re-compilation";
        } else {
          elements.btnCleanShaders.classList.add("opacity-50", "pointer-events-none");
          elements.btnCleanShaders.title = "Driver cache is already clean";
        }
      }

      // 3. Update Dev Monitor Card (if in dev mode)
      if (elements.devShaderTotalSize) {
        elements.devShaderTotalSize.textContent = info.total_formatted || "--";
      }
      if (elements.devShaderPipelineCount) {
        if (info.pipeline_count === 0) {
          elements.devShaderPipelineCount.textContent = "0 PSOs";
        } else if (info.delta_pipelines > 0) {
          elements.devShaderPipelineCount.innerHTML = `${info.pipeline_count} PSOs <span class="text-emerald-400 font-semibold text-[10px] ml-1">(+${info.delta_pipelines} new)</span>`;
        } else if (info.delta_pipelines === 0) {
          elements.devShaderPipelineCount.innerHTML = `${info.pipeline_count} PSOs <span class="text-slate-400 font-normal text-[10px] ml-1">(base)</span>`;
        } else {
          elements.devShaderPipelineCount.textContent = `${info.pipeline_count} PSOs`;
        }
      }
      if (elements.devShaderVkpcSize) {
        elements.devShaderVkpcSize.textContent = info.vkpc_formatted || "--";
      }
      if (elements.devShaderXshSize) {
        elements.devShaderXshSize.textContent = info.xsh_formatted || "--";
      }
      if (elements.devShaderLastUpdated) {
        elements.devShaderLastUpdated.textContent = info.last_updated || "--";
      }
    }
  } catch (err) {
    console.warn("Could not query shader cache info:", err);
  }
}

if (elements.btnRefreshShaderCache) {
  elements.btnRefreshShaderCache.addEventListener("click", updateShaderCacheInfo);
}

if (elements.btnCleanShaders) {
  elements.btnCleanShaders.addEventListener("click", async () => {
    try {
      elements.btnCleanShaders.disabled = true;
      elements.btnCleanShaders.classList.add("opacity-50", "pointer-events-none");
      await invoke("clean_shader_cache");
      await updateShaderCacheInfo();
    } catch (err) {
      alert("Failed to clean shader cache: " + err);
    } finally {
      await updateShaderCacheInfo();
    }
  });
}

window.addEventListener("focus", () => {
  updateShaderCacheInfo();
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
    const appContainer = document.getElementById("appContainer");
    const appFooter = document.getElementById("appFooter");
    if (elements.iconMaximize && elements.iconRestore) {
      if (isMax) {
        elements.iconMaximize.classList.add("hidden");
        elements.iconRestore.classList.remove("hidden");
        appContainer?.classList.remove("rounded-xl", "border");
        appHeader?.classList.remove("rounded-t-xl");
        appFooter?.classList.remove("rounded-b-xl");
      } else {
        elements.iconMaximize.classList.remove("hidden");
        elements.iconRestore.classList.add("hidden");
        appContainer?.classList.add("rounded-xl", "border");
        appHeader?.classList.add("rounded-t-xl");
        appFooter?.classList.add("rounded-b-xl");
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

if (elements.btnBuyMeCoffee) {
  elements.btnBuyMeCoffee.addEventListener("click", async (e) => {
    e.preventDefault();
    try {
      await invoke("open_external_url", { url: "https://buymeacoffee.com/majistic" });
    } catch (err) {
      console.warn("Failed to open external url via tauri command:", err);
      window.open("https://buymeacoffee.com/majistic", "_blank");
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

if (elements.mouseSensitivity) {
  elements.mouseSensitivity.addEventListener("input", (e) => {
    const val = parseFloat(e.target.value);
    if (elements.mouseSensitivityVal && !isNaN(val)) {
      elements.mouseSensitivityVal.textContent = val.toFixed(1) + "x";
    }
  });
}

if (elements.drawDistanceScale) {
  elements.drawDistanceScale.addEventListener("input", (e) => {
    const val = parseFloat(e.target.value);
    if (elements.drawDistanceScaleVal && !isNaN(val)) {
      elements.drawDistanceScaleVal.textContent = val.toFixed(2) + "x";
    }
  });
}

if (elements.fovScale) {
  elements.fovScale.addEventListener("input", (e) => {
    const val = parseFloat(e.target.value);
    if (elements.fovScaleVal && !isNaN(val)) {
      elements.fovScaleVal.textContent = val.toFixed(2) + "x";
    }
  });
}

if (elements.deDitherStrength) {
  elements.deDitherStrength.addEventListener("input", (e) => {
    const val = parseFloat(e.target.value);
    if (elements.deDitherStrengthVal && !isNaN(val)) {
      elements.deDitherStrengthVal.textContent = val.toFixed(2);
    }
  });
}

if (elements.casSharpness) {
  elements.casSharpness.addEventListener("input", (e) => {
    const val = parseFloat(e.target.value);
    if (elements.casSharpnessVal && !isNaN(val)) {
      elements.casSharpnessVal.textContent = val.toFixed(2);
    }
  });
}

if (elements.fsrReduction) {
  elements.fsrReduction.addEventListener("input", (e) => {
    const val = parseFloat(e.target.value);
    if (elements.fsrReductionVal && !isNaN(val)) {
      elements.fsrReductionVal.textContent = val.toFixed(2) + " stops";
    }
  });
}

if (elements.bloomIntensity) {
  elements.bloomIntensity.addEventListener("input", (e) => {
    const val = parseFloat(e.target.value);
    if (elements.bloomIntensityVal && !isNaN(val)) {
      elements.bloomIntensityVal.textContent = val <= 0.001 ? "Off" : val.toFixed(2);
    }
  });
}

// Global delegated listener to ensure live progress fill on all sliders
document.addEventListener("input", (e) => {
  if (e.target && e.target.type === "range") {
    updateSliderFill(e.target);
  }
});

if (elements.upscaler) {
  elements.upscaler.addEventListener("change", () => {
    updateUpscalerVisibility();
  });
}

if (elements.postEffect) {
  elements.postEffect.addEventListener("change", () => {
    updateDeDitherVisibility();
  });
}

if (elements.inputMode) {
  elements.inputMode.addEventListener("change", () => {
    updateInputModeVisibility();
  });
}

window.addEventListener("gamepadconnected", () => {
  updateConnectedGamepads();
});
window.addEventListener("gamepaddisconnected", () => {
  updateConnectedGamepads();
});
setInterval(() => {
  updateConnectedGamepads();
}, 2500);

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
  if (
    e.target &&
    (e.target.closest("#tabGraphics") ||
      e.target.closest("#tabControls") ||
      e.target.closest("#tabSettings") ||
      e.target.closest("#tabFixes") ||
      e.target.closest("#tabBuild"))
  ) {
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
      btnIcon.setAttribute("class", "w-3.5 h-3.5 shrink-0 transition-colors animate-spin");
      btnIcon.innerHTML = `<path fill="currentColor" d="M12.0789 2.25C7.28537 2.25 3.34474 5.913 2.96052 10.5833H1.99999C1.69611 10.5833 1.42226 10.7667 1.30652 11.0477C1.19078 11.3287 1.25603 11.6517 1.47175 11.8657L3.15156 13.5324C3.44397 13.8225 3.91564 13.8225 4.20805 13.5324L5.88786 11.8657C6.10358 11.6517 6.16883 11.3287 6.05309 11.0477C5.93735 10.7667 5.6635 10.5833 5.35961 10.5833H4.46677C4.84649 6.75167 8.10476 3.75 12.0789 3.75C14.8484 3.75 17.2727 5.20845 18.6156 7.39279C18.8325 7.74565 19.2944 7.85585 19.6473 7.63892C20.0001 7.42199 20.1103 6.96007 19.8934 6.60721C18.287 3.99427 15.3873 2.25 12.0789 2.25Z"/><path fill="currentColor" d="M20.8411 10.4666C20.5489 10.1778 20.0788 10.1778 19.7867 10.4666L18.1004 12.1333C17.8841 12.3471 17.8183 12.6703 17.9339 12.9517C18.0495 13.233 18.3235 13.4167 18.6277 13.4167H19.5268C19.1455 17.2462 15.8759 20.25 11.8827 20.25C9.10023 20.25 6.66583 18.7903 5.31793 16.6061C5.10039 16.2536 4.6383 16.1442 4.2858 16.3618C3.93331 16.5793 3.8239 17.0414 4.04143 17.3939C5.65404 20.007 8.56403 21.75 11.8827 21.75C16.6906 21.75 20.6474 18.0892 21.0331 13.4167H22.0001C22.3043 13.4167 22.5783 13.233 22.6939 12.9517C22.8094 12.6703 22.7437 12.3471 22.5273 12.1333L20.8411 10.4666Z"/>`;
    }
    btn.disabled = true;
    btn.className = "inline-flex items-center gap-1.5 px-2.5 py-1 rounded-lg border border-darkControl/60 bg-darkControl/50 text-slate-400 text-xs font-normal select-none cursor-wait focus:outline-none";
  } else if (state === "up-to-date") {
    btnText.textContent = text || "Up to Date";
    if (btnIcon) {
      btnIcon.setAttribute("class", "w-3.5 h-3.5 shrink-0 transition-colors");
      btnIcon.innerHTML = `<path fill="currentColor" fill-rule="evenodd" d="M22 12C22 17.5228 17.5228 22 12 22C6.47715 22 2 17.5228 2 12C2 6.47715 6.47715 2 12 2C17.5228 2 22 6.47715 22 12ZM16.0303 8.96967C16.3232 9.26256 16.3232 9.73744 16.0303 10.0303L11.0303 15.0303C10.7374 15.3232 10.2626 15.3232 9.96967 15.0303L7.96967 13.0303C7.67678 12.7374 7.67678 12.2626 7.96967 11.9697C8.26256 11.6768 8.73744 11.6768 9.03033 11.9697L10.5 13.4393L12.7348 11.2045L14.9697 8.96967C15.2626 8.67678 15.7374 8.67678 16.0303 8.96967Z" clip-rule="evenodd"/>`;
    }
    btn.disabled = false;
    btn.className = "inline-flex items-center gap-1.5 px-2.5 py-1 rounded-lg border border-emerald-800/60 bg-emerald-950/40 text-emerald-400 text-xs font-normal transition-all select-none cursor-pointer focus:outline-none";
  } else if (state === "available") {
    btnText.textContent = text || "Update Available";
    if (btnIcon) {
      btnIcon.setAttribute("class", "w-3.5 h-3.5 shrink-0 transition-colors");
      btnIcon.innerHTML = `<path fill="currentColor" fill-rule="evenodd" d="M2 12C2 7.28595 2 4.92893 3.46447 3.46447C4.92893 2 7.28595 2 12 2C16.714 2 19.0711 2 20.5355 3.46447C22 4.92893 22 7.28595 22 12C22 16.714 22 19.0711 20.5355 20.5355C19.0711 22 16.714 22 12 22C7.28595 22 4.92893 22 3.46447 20.5355C2 19.0711 2 16.714 2 12ZM12 6.25C12.4142 6.25 12.75 6.58579 12.75 7V12.1893L14.4697 10.4697C14.7626 10.1768 15.2374 10.1768 15.5303 10.4697C15.8232 10.7626 15.8232 11.2374 15.5303 11.5303L12.5303 14.5303C12.3897 14.671 12.1989 14.75 12 14.75C11.8011 14.75 11.6103 14.671 11.4697 14.5303L8.46967 11.5303C8.17678 11.2374 8.17678 10.7626 8.46967 10.4697C8.76256 10.1768 9.23744 10.1768 9.53033 10.4697L11.25 12.1893V7C11.25 6.58579 11.5858 6.25 12 6.25ZM8 16.25C7.58579 16.25 7.25 16.5858 7.25 17C7.25 17.4142 7.58579 17.75 8 17.75H16C16.4142 17.75 16.75 17.4142 16.75 17C16.75 16.5858 16.4142 16.25 16 16.25H8Z" clip-rule="evenodd"/>`;
    }
    btn.disabled = false;
    btn.className = "inline-flex items-center gap-1.5 px-2.5 py-1 rounded-lg border border-accent/50 bg-accent/20 hover:bg-accent/30 text-accent hover:text-white text-xs font-normal transition-all select-none cursor-pointer focus:outline-none";
  } else if (state === "error") {
    btnText.textContent = text || "Check Failed";
    if (btnIcon) {
      btnIcon.setAttribute("class", "w-3.5 h-3.5 shrink-0 transition-colors");
      btnIcon.innerHTML = `<path fill="currentColor" fill-rule="evenodd" d="M22 12C22 6.47715 17.5228 2 12 2C6.47715 2 2 6.47715 2 12C2 17.5228 6.47715 22 12 22C17.5228 22 22 17.5228 22 12ZM12 6.25C12.4142 6.25 12.75 6.58579 12.75 7V13C12.75 13.4142 12.4142 13.75 12 13.75C11.5858 13.75 11.25 13.4142 11.25 13V7C11.25 6.58579 11.5858 6.25 12 6.25ZM12 17C12.5523 17 13 16.5523 13 16C13 15.4477 12.5523 15 12 15C11.4477 15 11 15.4477 11 16C11 16.5523 11.4477 17 12 17Z" clip-rule="evenodd"/>`;
    }
    btn.disabled = false;
    btn.className = "inline-flex items-center gap-1.5 px-2.5 py-1 rounded-lg border border-rose-800/60 bg-rose-950/40 text-rose-400 text-xs font-normal transition-all select-none cursor-pointer focus:outline-none";
  } else {
    // default
    btnText.textContent = "Check for Updates";
    if (btnIcon) {
      btnIcon.setAttribute("class", "w-3.5 h-3.5 shrink-0 transition-colors");
      btnIcon.innerHTML = `<path fill="currentColor" d="M12.0789 2.25C7.28537 2.25 3.34474 5.913 2.96052 10.5833H1.99999C1.69611 10.5833 1.42226 10.7667 1.30652 11.0477C1.19078 11.3287 1.25603 11.6517 1.47175 11.8657L3.15156 13.5324C3.44397 13.8225 3.91564 13.8225 4.20805 13.5324L5.88786 11.8657C6.10358 11.6517 6.16883 11.3287 6.05309 11.0477C5.93735 10.7667 5.6635 10.5833 5.35961 10.5833H4.46677C4.84649 6.75167 8.10476 3.75 12.0789 3.75C14.8484 3.75 17.2727 5.20845 18.6156 7.39279C18.8325 7.74565 19.2944 7.85585 19.6473 7.63892C20.0001 7.42199 20.1103 6.96007 19.8934 6.60721C18.287 3.99427 15.3873 2.25 12.0789 2.25Z"/><path fill="currentColor" d="M20.8411 10.4666C20.5489 10.1778 20.0788 10.1778 19.7867 10.4666L18.1004 12.1333C17.8841 12.3471 17.8183 12.6703 17.9339 12.9517C18.0495 13.233 18.3235 13.4167 18.6277 13.4167H19.5268C19.1455 17.2462 15.8759 20.25 11.8827 20.25C9.10023 20.25 6.66583 18.7903 5.31793 16.6061C5.10039 16.2536 4.6383 16.1442 4.2858 16.3618C3.93331 16.5793 3.8239 17.0414 4.04143 17.3939C5.65404 20.007 8.56403 21.75 11.8827 21.75C16.6906 21.75 20.6474 18.0892 21.0331 13.4167H22.0001C22.3043 13.4167 22.5783 13.233 22.6939 12.9517C22.8094 12.6703 22.7437 12.3471 22.5273 12.1333L20.8411 10.4666Z"/>`;
    }
    btn.disabled = false;
    btn.className = "inline-flex items-center gap-1.5 px-2.5 py-1 rounded-lg border border-darkBorder/60 bg-darkControl/80 hover:bg-darkControl text-slate-300 hover:text-white text-xs font-normal transition-all select-none cursor-pointer focus:outline-none";
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

function isVersionNewer(remote, current) {
  if (!remote || !current) return false;
  const parse = (v) => v.replace(/^v/, "").split(".").map((n) => parseInt(n, 10) || 0);
  const r = parse(remote);
  const c = parse(current);
  for (let i = 0; i < Math.max(r.length, c.length); i++) {
    const rVal = r[i] || 0;
    const cVal = c[i] || 0;
    if (rVal > cVal) return true;
    if (rVal < cVal) return false;
  }
  return false;
}

async function checkForUpdates(isUserClick = false) {
  if (isCheckingUpdates || isDownloadingUpdate) return;
  isCheckingUpdates = true;
  setUpdateBtnState("checking");
  logToBackend(`checkForUpdates: start (userClick=${isUserClick})`);

  try {
    const currentVer = (elements.appVersionBadge?.textContent || "0.1.3").replace(/^v/, "").trim();
    let remoteVer = null;
    let releaseData = null;

    // Fast check: query GitHub Releases API directly with 2.5s timeout
    try {
      const controller = new AbortController();
      const timer = setTimeout(() => controller.abort(), 2500);
      const res = await fetch("https://api.github.com/repos/kelvynzucco/majistic-recomp/releases/latest", {
        signal: controller.signal,
        headers: { Accept: "application/vnd.github.v3+json" }
      });
      clearTimeout(timer);

      if (res.ok) {
        releaseData = await res.json();
        remoteVer = (releaseData.tag_name || "").replace(/^v/, "").trim();
      }
    } catch (e) {
      logToBackend(`GitHub API fetch error: ${e?.message || e}`);
    }

    // Secondary fallback if api.github.com is rate-limited: check raw manifest
    if (!remoteVer) {
      try {
        const controller = new AbortController();
        const timer = setTimeout(() => controller.abort(), 2000);
        const res = await fetch("https://raw.githubusercontent.com/kelvynzucco/majistic-recomp/main/launcher/package.json", {
          signal: controller.signal
        });
        clearTimeout(timer);
        if (res.ok) {
          const pkg = await res.json();
          remoteVer = (pkg.version || "").replace(/^v/, "").trim();
        }
      } catch (e) {
        logToBackend(`Raw fallback error: ${e?.message || e}`);
      }
    }

    const hasUpdate = remoteVer && isVersionNewer(remoteVer, currentVer);

    if (hasUpdate) {
      logToBackend(`checkForUpdates: newer version detected: v${remoteVer} (current: v${currentVer})`);

      // Attempt Tauri plugin check for native installer hook
      let pluginUpdate = null;
      try {
        pluginUpdate = await check({ timeout: 2000 });
      } catch {}

      currentPendingUpdate = pluginUpdate || {
        version: remoteVer,
        body: releaseData?.body || "",
        date: releaseData?.published_at || "",
        html_url: releaseData?.html_url || "https://github.com/kelvynzucco/majistic-recomp/releases/latest"
      };

      setUpdateBtnState("available");
      showUpdateModal(currentPendingUpdate);
    } else {
      logToBackend(`checkForUpdates: up to date (current: v${currentVer}, remote: v${remoteVer || currentVer})`);
      currentPendingUpdate = null;
      setUpdateBtnState("up-to-date");
      setTimeout(() => {
        if (!currentPendingUpdate) setUpdateBtnState("default");
      }, 4000);
    }
  } catch (err) {
    logToBackend(`checkForUpdates: error: ${err?.message || err}`);
    console.warn("Update check general error:", err);
    currentPendingUpdate = null;
    setUpdateBtnState("up-to-date");
    setTimeout(() => {
      if (!currentPendingUpdate) setUpdateBtnState("default");
    }, 4000);
  } finally {
    isCheckingUpdates = false;
  }
}

async function installUpdate() {
  if (!currentPendingUpdate || isDownloadingUpdate) return;
  if (typeof currentPendingUpdate.downloadAndInstall !== "function") {
    window.open(currentPendingUpdate.html_url || "https://github.com/kelvynzucco/majistic-recomp/releases/latest", "_blank");
    hideUpdateModal();
    return;
  }
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

  let isDev = false;
  try {
    isDev = await invoke("is_dev_mode");
  } catch (err) {
    console.warn("Could not determine dev mode:", err);
  }

  if (!isDev) {
    document.querySelectorAll("[data-dev-only]").forEach((el) => el.remove());
    elements.chkCompileBinary = null;
  } else {
    const versionBadge = document.getElementById("launcherVersionBadge");
    if (versionBadge && !document.getElementById("devModeBadge")) {
      const devBadge = document.createElement("span");
      devBadge.id = "devModeBadge";
      devBadge.className = "text-[9px] px-1.5 py-0.5 rounded-md bg-amber-950/80 text-amber-300 border border-amber-800/60 font-mono font-bold";
      devBadge.textContent = "DEV";
      versionBadge.parentElement.appendChild(devBadge);
    }
  }

  try {
    const loaded = await invoke("load_settings");
    if (loaded && typeof loaded === "object") {
      currentSettings = { ...defaultSettings, ...loaded };
    }
  } catch (err) {
    console.warn("Could not load settings:", err);
    logToBackend("Could not load settings: " + err);
  }
  initCustomSelects();
  populateForm(currentSettings);
  await updateGameStatus();
  await updateShaderCacheInfo();
  await updateMaximizeIcon();
  logToBackend("init() completed successfully");

  // Note: Automatic silent check for updates on startup was disabled to prevent
  // idle compositor freezes on AMD GPUs. Updates can be checked manually via "Check for Updates".
}

init();
