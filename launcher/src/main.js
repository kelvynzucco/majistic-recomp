import { invoke } from "@tauri-apps/api/core";

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
};

let currentSettings = { ...defaultSettings };

// DOM element mapping
const elements = {
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
  btnLaunch: document.getElementById("btnLaunch"),
  btnExit: document.getElementById("btnExit"),
};

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
}

function readForm() {
  return {
    WindowSize: elements.windowSize.value,
    Fullscreen: elements.fullscreen.value === "true",
    RenderScale: parseInt(elements.renderScale.value, 10),
    RenderingPath: elements.renderingPath.value,
    FrameRateMode: elements.frameRateMode.value,
    VSync: elements.vsync.checked,
    PostEffect: elements.postEffect.value,
    Upscaler: elements.upscaler.value,
    SkipIntroLogos: elements.skipIntroLogos.checked,
    SkipCutscenes: elements.skipCutscenes.checked,
    CutsceneSpeed: parseInt(elements.cutsceneSpeed.value, 10),
    InputMode: elements.inputMode.value,
    MouseSensitivity: currentSettings.MouseSensitivity || 1.0,
    RecordPerformance: currentSettings.RecordPerformance ?? true,
    GameDataRoot: elements.gameDataRoot.value.trim(),
    Configuration: "Release",
  };
}

async function init() {
  try {
    const loaded = await invoke("load_settings");
    if (loaded && typeof loaded === "object") {
      currentSettings = { ...defaultSettings, ...loaded };
    }
  } catch (err) {
    console.warn("Tauri invoke not available or failed; using defaults:", err);
  }
  populateForm(currentSettings);
}

elements.btnLaunch.addEventListener("click", async () => {
  const settings = readForm();
  elements.btnLaunch.disabled = true;
  elements.btnLaunch.classList.add("opacity-50");
  elements.btnLaunch.innerHTML = `<span>⏳</span><span>LAUNCHING...</span>`;

  try {
    await invoke("launch_game", { settings });
  } catch (err) {
    alert("Failed to launch game: " + err);
    elements.btnLaunch.disabled = false;
    elements.btnLaunch.classList.remove("opacity-50");
    elements.btnLaunch.innerHTML = `<span>▶</span><span>LAUNCH GAME</span>`;
  }
});

elements.btnExit.addEventListener("click", async () => {
  try {
    await invoke("exit_app");
  } catch {
    window.close();
  }
});

// Auto-save on form change
document.addEventListener("change", async (e) => {
  if (e.target && e.target.closest("main")) {
    const settings = readForm();
    try {
      await invoke("save_settings", { settings });
    } catch {
      // ignore in dev preview
    }
  }
});

init();
