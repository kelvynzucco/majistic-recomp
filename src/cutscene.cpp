// SPDX-License-Identifier: GPL-3.0-only
// Majistic Recomp - Unified In-game 3D Cutscene (DEMO) Fast-Forward
//
// Fast-forward is driven purely by the state of the engine's DEMO player, read
// once per frame from the main timing hook (timing.cpp). It is NOT driven by
// script-command polling (DEMO_END_CHECK / CHECK_END_MESSAGE), because those are
// also polled while the player is in control (gameplay dialogue, event cameras).

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
#include "generated/default/majin_pch.h"
#include "edition_offsets.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/audio/audio_driver.h>
#include <rex/ui/windowed_app_context.h>

namespace majin {
rex::ui::WindowedAppContext* g_app_context = nullptr;
void RequestCleanQuit() {
  if (g_app_context) {
    g_app_context->RequestDeferredQuit();
  }
}
}  // namespace majin

REXCVAR_DEFINE_BOOL(majin_skip_cutscenes, true, "Gameplay",
    "Allow fast-forwarding in-game cutscenes by holding Select/Back on controller or Tab/Backspace on keyboard");

REXCVAR_DEFINE_INT32(majin_cutscene_speed, 4, "Gameplay",
    "In-game cutscene fast-forward speed multiplier (2, 4, 6, 8)");

REXCVAR_DEFINE_INT32(majin_cutscene_end_margin, 90, "Gameplay",
    "Cutscene frames before the end of a DEMO where fast-forward is forced back to normal speed");

REXCVAR_DEFINE_BOOL(majin_cutscene_ff_camera_demos, false, "Gameplay",
    "Also allow fast-forward during short DEMO_CAMERA events (camera-only scripted shots)");

REXCVAR_DECLARE(bool, majin_harvest_demos);
REXCVAR_DECLARE(int32_t, majin_harvest_start);
REXCVAR_DECLARE(int32_t, majin_harvest_count);

DECLARE_REX_FUNC(sub_8215B918);
DECLARE_REX_FUNC(sub_8215A700);
DECLARE_REX_FUNC(sub_8215B290);
DECLARE_REX_FUNC(sub_8215BEC8);
DECLARE_REX_FUNC(sub_8215BA58);
DECLARE_REX_FUNC(sub_8215AD28);
#if !defined(MAJIN_EDITION_USA)
DECLARE_REX_FUNC(sub_8235A140);
#endif

namespace {

constexpr uint32_t kScenarioRootPtr = majin::offsets::kScenarioRootPtr;
constexpr uint32_t kDemoManagerOffset = 0x15144;
constexpr uint32_t kDemoNodeOffset = 120;
constexpr uint32_t kDemoCameraNodeOffset = 124;

bool Enabled() {
  return REXCVAR_GET(majin_skip_cutscenes);
}

bool CameraDemosEnabled() {
  return REXCVAR_GET(majin_cutscene_ff_camera_demos);
}

float EndMarginFrames() {
  const int32_t v = REXCVAR_GET(majin_cutscene_end_margin);
  return static_cast<float>(v < 15 ? 15 : (v > 600 ? 600 : v));
}

float SpeedMultiplier() {
  const int32_t spd = REXCVAR_GET(majin_cutscene_speed);
  return static_cast<float>(spd < 2 ? 2 : (spd > 16 ? 16 : spd));
}

bool Span(uint32_t addr, uint32_t size) {
  return addr >= 0x10000 && addr <= UINT32_MAX - size;
}

float LoadFloat(uint8_t* base, uint32_t address) {
  return std::bit_cast<float>(REX_LOAD_U32(address));
}

struct DemoProgress {
  float current = 0.0f;  // current frame
  float end = 0.0f;      // last frame
  float step = 0.0f;     // frames advanced on the last tick
};

// Returns true only while the DEMO player in `slot` is actively playing.
bool ReadDemoPlayer(uint8_t* base, uint32_t slot, DemoProgress& out) {
  const uint32_t root = REX_LOAD_U32(kScenarioRootPtr);
  if (!Span(root, kDemoManagerOffset + 4)) return false;
  const uint32_t manager = REX_LOAD_U32(root + kDemoManagerOffset);
  if (!Span(manager, 128)) return false;
  const uint32_t node = REX_LOAD_U32(manager + slot);
  if (!Span(node, 24)) return false;
  const uint32_t player = REX_LOAD_U32(node + 20);
  if (!Span(player, 160)) return false;

  const int32_t state = static_cast<int32_t>(REX_LOAD_U32(player + 96));
  const int32_t request = static_cast<int32_t>(REX_LOAD_U32(player + 92));
  if (state != 1 && state != 2) return false;  // not playing
  if (request == 3) return false;              // stop already requested

  const uint32_t resource = REX_LOAD_U32(player + 156);
  if (!Span(resource, 104)) return false;
  const int32_t start = static_cast<int32_t>(REX_LOAD_U32(resource + 96));
  const int32_t end = static_cast<int32_t>(REX_LOAD_U32(resource + 100));
  if (end <= start) return false;

  out.current = LoadFloat(base, player + 80);
  out.step = LoadFloat(base, player + 88);
  out.end = static_cast<float>(end);
  return std::isfinite(out.current) && std::isfinite(out.step);
}

// True while a DEMO is playing and is still far enough from its last frame.
bool DemoAllowsTurbo(uint8_t* base, uint32_t slot, float& remaining_out) {
  DemoProgress demo;
  if (!ReadDemoPlayer(base, slot, demo)) return false;
  const float step = demo.step > 0.0f ? demo.step : 0.0f;
  const float guard = EndMarginFrames() + step * 4.0f;
  remaining_out = demo.end - demo.current;
  return remaining_out > guard;
}

std::atomic<bool> s_turbo_active{false};
std::atomic<bool> s_must_release_button{false};

} // namespace

bool IsBackOrTabHeld() {
  if (!Enabled()) return false;

  // Keyboard check: Tab key or Backspace
  if ((GetAsyncKeyState(VK_TAB) & 0x8000) != 0 || (GetAsyncKeyState(VK_BACK) & 0x8000) != 0) {
    return true;
  }

  // Gamepad check: XInput controllers (Back / Select button = 0x0020)
  static HMODULE xinput = []() -> HMODULE {
    HMODULE h = LoadLibraryA("xinput1_4.dll");
    if (!h) h = LoadLibraryA("xinput1_3.dll");
    if (!h) h = LoadLibraryA("xinput9_1_0.dll");
    return h;
  }();

  if (xinput) {
    typedef DWORD (WINAPI *PFN_XInputGetState)(DWORD dwUserIndex, void* pState);
    static PFN_XInputGetState pfnGetState = (PFN_XInputGetState)GetProcAddress(xinput, "XInputGetState");
    if (pfnGetState) {
      struct {
        DWORD dwPacketNumber;
        struct {
          WORD wButtons;
          BYTE bLeftTrigger;
          BYTE bRightTrigger;
          SHORT sThumbLX;
          SHORT sThumbLY;
          SHORT sThumbRX;
          SHORT sThumbRY;
        } Gamepad;
      } state{};
      for (DWORD i = 0; i < 4; ++i) {
        if (pfnGetState(i, &state) == 0) {
          if ((state.Gamepad.wButtons & 0x0020) != 0) {
            return true;
          }
        }
      }
    }
  }

  return false;
}

float UpdateCutsceneTurbo(uint8_t* base) {
  float multiplier = 1.0f;
  float remaining = 0.0f;
  const bool button_held = IsBackOrTabHeld();

  static bool s_was_button_held = false;
  if (button_held && !s_was_button_held && Enabled()) {
    DemoProgress demo{};
    const bool demo_present = ReadDemoPlayer(base, kDemoNodeOffset, demo);
    if (demo_present) {
#if defined(MAJIN_ENABLE_DIAGNOSTICS)
      REXLOG_INFO("[Majin Cutscene] button pressed during DEMO (frame {:.1f}/{:.1f}, step {:.2f}, {})",
                  demo.current, demo.end, demo.step, MAJIN_EDITION_NAME);
#endif
    } else {
      REXLOG_DEBUG("[Majin Cutscene] button pressed, but no DEMO active ({})", MAJIN_EDITION_NAME);
    }
  }
  s_was_button_held = button_held;

  // Button release latch: if turbo was previously forced off while the button
  // was held, the user must release the button before turbo can be re-engaged.
  if (s_must_release_button.load(std::memory_order_relaxed)) {
    if (!button_held) {
      s_must_release_button.store(false, std::memory_order_relaxed);
    }
  }

  if (Enabled() && !s_must_release_button.load(std::memory_order_relaxed)) {
    bool demo_ok = DemoAllowsTurbo(base, kDemoNodeOffset, remaining);
    if (!demo_ok && CameraDemosEnabled()) {
      demo_ok = DemoAllowsTurbo(base, kDemoCameraNodeOffset, remaining);
    }
    // Only engage fast-forward while a DEMO cutscene is running and far from the end
    if (demo_ok && button_held) {
      multiplier = SpeedMultiplier();
    }
  }

  const bool active = multiplier > 1.0f;
  const bool was_active = s_turbo_active.exchange(active, std::memory_order_relaxed);
  if (was_active != active) {
    rex::audio::SetPlaybackFrequencyRatio(active ? multiplier : 1.0f);
    if (active) {
#if defined(MAJIN_ENABLE_DIAGNOSTICS)
      REXLOG_INFO("[Majin Cutscene] fast-forward x{} ({} frames left, {})",
                  static_cast<int>(multiplier), static_cast<int>(remaining), MAJIN_EDITION_NAME);
#endif
    } else {
#if defined(MAJIN_ENABLE_DIAGNOSTICS)
      REXLOG_INFO("[Majin Cutscene] fast-forward off ({})", MAJIN_EDITION_NAME);
#endif
      // If turbo stopped while the button is still held (end margin reached or cutscene ended),
      // lock out turbo so it never affects gameplay even if held continuously.
      if (button_held) {
        s_must_release_button.store(true, std::memory_order_relaxed);
      }
    }
  }
  return multiplier;
}

namespace {

namespace fs = std::filesystem;

std::vector<uint32_t> DiscoverDemoQueue() {
  std::vector<fs::path> candidates = {
    "game-data/finalizedXBox360/KB/demo/package",
    "finalizedXBox360/KB/demo/package",
    "KB/demo/package",
    "../finalizedXBox360/KB/demo/package",
    "../../finalizedXBox360/KB/demo/package",
    "../../../game-data/finalizedXBox360/KB/demo/package",
    "../../game-data/finalizedXBox360/KB/demo/package",
    "../game-data/finalizedXBox360/KB/demo/package"
  };

#if defined(_WIN32)
  wchar_t exe_path[MAX_PATH]{};
  if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH)) {
    fs::path exe_dir = fs::path(exe_path).parent_path();
    candidates.push_back(exe_dir / "finalizedXBox360/KB/demo/package");
    candidates.push_back(exe_dir / "../finalizedXBox360/KB/demo/package");
    candidates.push_back(exe_dir / "../../finalizedXBox360/KB/demo/package");
    candidates.push_back(exe_dir / "../game-data/finalizedXBox360/KB/demo/package");
    candidates.push_back(exe_dir / "../../game-data/finalizedXBox360/KB/demo/package");
  }
#endif

  fs::path package_dir;
  for (const auto& candidate : candidates) {
    std::error_code ec;
    if (fs::is_directory(candidate, ec)) {
      package_dir = candidate;
      break;
    }
  }

  std::vector<uint32_t> queue;
  if (package_dir.empty()) {
    REXLOG_ERROR("[HARVEST] Pasta de pacotes demo (KB/demo/package) nao encontrada!");
    return queue;
  }

  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(package_dir, ec)) {
    if (!entry.is_regular_file()) continue;
    const std::string filename = entry.path().filename().string();
    if (filename.size() > 8 &&
        (filename.compare(0, 4, "demo") == 0 || filename.compare(0, 4, "DEMO") == 0) &&
        (filename.compare(filename.size() - 4, 4, ".pak") == 0 || filename.compare(filename.size() - 4, 4, ".PAK") == 0)) {
      std::string id_str = filename.substr(4, filename.size() - 8);
      try {
        const uint32_t id = static_cast<uint32_t>(std::stoul(id_str));
        queue.push_back(id);
      } catch (...) {
        // Ignora caso nao numerico
      }
    }
  }

  std::sort(queue.begin(), queue.end());
  REXLOG_INFO("[HARVEST] {} demos descobertas em '{}'",
              queue.size(), package_dir.string());
  return queue;
}

enum class HarvestState {
  kUninitialized,
  kWaitingForEngine,
  kMountPackage,
  kWaitingForMount,
  kStartDemo,
  kPlayingDemo,
  kDemoCooldown,
  kAllFinished
};

HarvestState s_harvest_state = HarvestState::kUninitialized;
std::vector<uint32_t> s_demo_queue;
size_t s_current_index = 0;
size_t s_global_offset = 0;
size_t s_total_demos = 0;
uint32_t s_warmup_frames = 0;
uint32_t s_mount_wait_frames = 0;
uint32_t s_playing_frames = 0;
uint32_t s_cooldown_frames = 0;
bool s_has_detected_playing = false;
std::chrono::steady_clock::time_point s_demo_start_time;

uint32_t FindDemoNode(uint8_t* base, uint32_t manager, uint32_t demo_id) {
  uint32_t node = REX_LOAD_U32(manager + 52);
  while (node != 0 && Span(node, 36)) {
    if (REX_LOAD_U32(node + 24) == demo_id) {
      return node;
    }
    node = REX_LOAD_U32(node + 16);
  }
  return 0;
}

bool IsDemoInManagerList(uint8_t* base, uint32_t manager, uint32_t demo_id) {
  return FindDemoNode(base, manager, demo_id) != 0;
}

uint32_t CheckDemoStatus(PPCContext& ctx, uint8_t* base, uint32_t manager, uint32_t demo_id) {
  PPCContext call_ctx = ctx;
  call_ctx.r1.u32 = ctx.r1.u32 - 128;
  REX_STORE_U32(call_ctx.r1.u32, ctx.r1.u32);
  const uint32_t guest_scratch = call_ctx.r1.u32 + 64;
  REX_STORE_U32(guest_scratch, 0);

  call_ctx.r3.u32 = guest_scratch;
  call_ctx.r4.u32 = manager;
  call_ctx.r5.u32 = demo_id;
  sub_8215B290(call_ctx, base);

  return REX_LOAD_U32(guest_scratch);
}

} // namespace

void UpdateCutsceneHarvest(PPCContext& ctx, uint8_t* base) {
  if (!REXCVAR_GET(majin_harvest_demos)) return;

#if defined(_WIN32)
  static bool s_console_init = false;
  if (!s_console_init) {
    s_console_init = true;
    HANDLE hStd = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hStd == NULL || hStd == INVALID_HANDLE_VALUE) {
      if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* fp = nullptr;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
      }
    }
  }
#endif

  const uint32_t root = REX_LOAD_U32(kScenarioRootPtr);
  if (!Span(root, kDemoManagerOffset + 4)) return;
  const uint32_t manager = REX_LOAD_U32(root + kDemoManagerOffset);
  if (!Span(manager, 164)) return;

  switch (s_harvest_state) {
    case HarvestState::kUninitialized: {
      std::vector<uint32_t> full_queue = DiscoverDemoQueue();
      if (full_queue.empty()) {
        REXLOG_ERROR("[HARVEST] Nenhuma cutscene encontrada para colheita. Desativando...");
        std::printf("[HARVEST] ERRO: Nenhuma cutscene encontrada para colheita.\n");
        std::fflush(stdout);
        s_harvest_state = HarvestState::kAllFinished;
        return;
      }

      s_total_demos = full_queue.size();
      const int32_t start_idx = std::max(0, REXCVAR_GET(majin_harvest_start));
      const int32_t count = REXCVAR_GET(majin_harvest_count);

      if (static_cast<size_t>(start_idx) < full_queue.size()) {
        const size_t end_idx = (count > 0) ? std::min(full_queue.size(), static_cast<size_t>(start_idx + count)) : full_queue.size();
        s_demo_queue = std::vector<uint32_t>(full_queue.begin() + start_idx, full_queue.begin() + end_idx);
        s_global_offset = static_cast<size_t>(start_idx);
      } else {
        s_demo_queue.clear();
        s_global_offset = static_cast<size_t>(start_idx);
      }

      if (s_demo_queue.empty()) {
        REXLOG_INFO("[HARVEST] Fila deste lote esta vazia (inicio {} >= total {}). Finalizando.",
                    start_idx, full_queue.size());
        s_harvest_state = HarvestState::kAllFinished;
        return;
      }

      std::printf("[HARVEST] Lote configurado: %zu demos (offset %zu de %zu totais). Aguardando motor...\n",
                  s_demo_queue.size(), s_global_offset + 1, s_total_demos);
      std::fflush(stdout);
      s_current_index = 0;
      s_harvest_state = HarvestState::kWaitingForEngine;
      break;
    }

    case HarvestState::kWaitingForEngine: {
      if (++s_warmup_frames < 60) return;

      REXLOG_INFO("[HARVEST] Motor estabilizado (ScenarioRoot=0x{:08X}, DemoManager=0x{:08X})",
                  root, manager);
      REXLOG_INFO("[HARVEST] Iniciando lote de {} cutscenes (offset {}/{})...",
                  s_demo_queue.size(), s_global_offset + 1, s_total_demos);
      std::printf("[HARVEST] Motor estabilizado. Iniciando lote de %zu cutscenes (%zu a %zu de %zu)...\n",
                  s_demo_queue.size(), s_global_offset + 1, s_global_offset + s_demo_queue.size(), s_total_demos);
      std::fflush(stdout);
      s_harvest_state = HarvestState::kMountPackage;
      break;
    }

    case HarvestState::kMountPackage: {
      if (s_current_index >= s_demo_queue.size()) {
        s_harvest_state = HarvestState::kAllFinished;
        break;
      }

      const uint32_t demo_id = s_demo_queue[s_current_index];
      REXLOG_INFO("[HARVEST] [{}/{}] Montando demo{:05d}.pak...",
                  s_global_offset + s_current_index + 1, s_total_demos, demo_id);

      PPCContext call_ctx = ctx;
      call_ctx.r3.u32 = manager;
      call_ctx.r4.u32 = demo_id;
      sub_8215B918(call_ctx, base);

      s_harvest_state = HarvestState::kWaitingForMount;
      s_mount_wait_frames = 0;
      break;
    }

    case HarvestState::kWaitingForMount: {
      s_mount_wait_frames++;
      const uint32_t demo_id = s_demo_queue[s_current_index];
      const uint32_t task_handle = REX_LOAD_U32(manager + 160);
      bool package_ready = false;
      if (task_handle != 0 && Span(task_handle, 64)) {
        const uint32_t status_flags = REX_LOAD_U32(task_handle + 56);
        if ((status_flags & 1) != 0) {
          package_ready = true;
        }
      }

      // Garante ao menos 10 frames de montagem para o novo pacote ser registrado
      if (package_ready && s_mount_wait_frames >= 10) {
        s_harvest_state = HarvestState::kStartDemo;
      } else if (s_mount_wait_frames >= 300) {
        REXLOG_WARN("[HARVEST] Timeout aguardando montagem do pacote demo{:05d}.pak. Pulando...", demo_id);
        std::printf("[HARVEST] Timeout aguardando montagem de demo%05u.pak. Pulando...\n", demo_id);
        std::fflush(stdout);
        s_current_index++;
        s_harvest_state = (s_current_index < s_demo_queue.size()) ? HarvestState::kMountPackage : HarvestState::kAllFinished;
      }
      break;
    }

    case HarvestState::kStartDemo: {
      const uint32_t demo_id = s_demo_queue[s_current_index];
      PPCContext call_ctx = ctx;
      call_ctx.r3.u32 = manager;
      call_ctx.r4.u32 = demo_id;
      sub_8215A700(call_ctx, base);

      const uint32_t node = FindDemoNode(base, manager, demo_id);
      const uint32_t player = (node != 0) ? REX_LOAD_U32(node + 20) : 0;
#if !defined(MAJIN_EDITION_USA)
      if (player != 0 && Span(player, 160)) {
        PPCContext p_ctx = ctx;
        p_ctx.r3.u32 = player;
        sub_8235A140(p_ctx, base);
      }
#endif

      call_ctx = ctx;
      call_ctx.r3.u32 = manager;
      call_ctx.r4.u32 = demo_id;
      sub_8215AD28(call_ctx, base);

      if (player != 0 && Span(player, 160)) {
        REX_STORE_U32(player + 96, 1);
      }

      REXLOG_INFO("[HARVEST] Reproducao iniciada: demo{:05d}.pak (node=0x{:X}, player=0x{:X})",
                  demo_id, node, player);
      std::printf("[HARVEST] Reproducao iniciada: demo%05u.pak (node=0x%X, player=0x%X)\n",
                  demo_id, node, player);
      std::fflush(stdout);

      s_demo_start_time = std::chrono::steady_clock::now();
      s_playing_frames = 0;
      s_has_detected_playing = false;
      s_harvest_state = HarvestState::kPlayingDemo;
      break;
    }

    case HarvestState::kPlayingDemo: {
      const uint32_t demo_id = s_demo_queue[s_current_index];
      s_playing_frames++;

      const auto now = std::chrono::steady_clock::now();
      const double elapsed_sec = std::chrono::duration<double>(now - s_demo_start_time).count();

      const uint32_t active_node = REX_LOAD_U32(manager + 120);
      const uint32_t node = FindDemoNode(base, manager, demo_id);
      const bool in_list = (node != 0);
      const uint32_t player = in_list ? REX_LOAD_U32(node + 20) : 0;
      const uint32_t pstate = (player != 0 && Span(player, 160)) ? REX_LOAD_U32(player + 96) : 0;
      const uint32_t prequest = (player != 0 && Span(player, 160)) ? REX_LOAD_U32(player + 92) : 0;

      float cur_f = 0.0f;
      float end_f = 0.0f;
      static float s_last_cur_f = 0.0f;
      static float s_last_end_f = 0.0f;
      const char* finish_reason = "";

      if (s_playing_frames == 1) {
        s_last_cur_f = 0.0f;
        s_last_end_f = 0.0f;
      }

      if (player != 0 && Span(player, 160)) {
        cur_f = LoadFloat(base, player + 80);
        const uint32_t resource = REX_LOAD_U32(player + 156);
        if (resource != 0 && Span(resource, 104)) {
          end_f = static_cast<float>(static_cast<int32_t>(REX_LOAD_U32(resource + 100)));
        }
        if (cur_f > 0.0f) s_last_cur_f = cur_f;
        if (end_f > 0.0f) s_last_end_f = end_f;
      }

      if (pstate == 1 || pstate == 2) {
        s_has_detected_playing = true;
      } else if (s_playing_frames <= 10 && player != 0 && Span(player, 160)) {
        // Assegura que o player permanece no estado de reproducao
        REX_STORE_U32(player + 96, 1);
        s_has_detected_playing = true;
      }

      if (s_playing_frames <= 35 || (s_playing_frames % 60 == 0)) {
        REXLOG_INFO("[HARVEST DBG] demo{} f{} in_list={} act_node=0x{:X} node=0x{:X} player=0x{:X} pstate={} req={} frames={:.1f}/{:.1f}",
                    demo_id, s_playing_frames, in_list, active_node, node, player, pstate, prequest, cur_f, end_f);
        std::printf("[HARVEST DBG] demo%u f%u in_list=%d act_node=0x%X node=0x%X player=0x%X pstate=%u req=%u frames=%.1f/%.1f\n",
                    demo_id, s_playing_frames, in_list ? 1 : 0, active_node, node, player, pstate, prequest, cur_f, end_f);
        std::fflush(stdout);
      }

      bool demo_finished = false;

      // Completion conditions:
      // 1. Natural end after cutscene was confirmed playing and advanced frames:
      if (s_has_detected_playing && s_last_cur_f > 1.0f) {
        if (active_node == 0) {
          // sub_8215BEC8 liberou o node ativo ao término natural da demo
          demo_finished = true;
          finish_reason = "active_node==0";
        } else if (pstate != 1 && pstate != 2) {
          demo_finished = true;
          finish_reason = "pstate_stopped";
        } else if (!in_list || player == 0) {
          demo_finished = true;
          finish_reason = "unlinked_from_manager";
        } else if (end_f > 0.0f && cur_f >= end_f - 1.0f) {
          demo_finished = true;
          finish_reason = "reached_end_frame";
        }
      }

      // Safety watchdog: max 45s real-time per demo
      if (elapsed_sec >= 45.0) {
        demo_finished = true;
        finish_reason = "watchdog_timeout_45s";
        REXLOG_WARN("[HARVEST] Watchdog acionado: demo{:05d}.pak atingiu 45s de execucao! Avancando...",
                    demo_id);
        std::printf("[HARVEST] Watchdog acionado: demo%05u.pak atingiu 45s de execucao! Avancando...\n",
                    demo_id);
        std::fflush(stdout);
      }

      if (demo_finished) {
        const float report_cur = (cur_f > 0.0f) ? cur_f : s_last_cur_f;
        const float report_end = (end_f > 0.0f) ? end_f : s_last_end_f;
        REXLOG_INFO("[HARVEST] [{}/{}] demo{:05d}.pak concluido | Tempo: {:.1f}s | Frame final: {:.1f}/{:.1f} ({})",
                    s_global_offset + s_current_index + 1, s_total_demos, demo_id, elapsed_sec, report_cur, report_end, finish_reason);
        std::printf("[HARVEST] [%zu/%zu] demo%05u.pak concluido | Tempo: %.1fs | Frame final: %.1f/%.1f (%s)\n",
                    s_global_offset + s_current_index + 1, s_total_demos, demo_id, elapsed_sec, report_cur, report_end, finish_reason);
        std::fflush(stdout);

        // Flush continuo de shaders para disco
        _flushall();

        s_cooldown_frames = 0;
        s_harvest_state = HarvestState::kDemoCooldown;
      }
      break;
    }

    case HarvestState::kDemoCooldown: {
      s_cooldown_frames++;
      // Intervalo entre demos para o motor liberar recursos e o storage thread persistir PSOs
      if (s_cooldown_frames >= 30) {
        s_current_index++;
        if (s_current_index < s_demo_queue.size()) {
          s_harvest_state = HarvestState::kMountPackage;
        } else {
          s_harvest_state = HarvestState::kAllFinished;
        }
      }
      break;
    }

    case HarvestState::kAllFinished: {
      static bool s_shutdown_started = false;
      static auto s_shutdown_time = std::chrono::steady_clock::now();
      if (!s_shutdown_started) {
        s_shutdown_started = true;
        s_shutdown_time = std::chrono::steady_clock::now();
        REXLOG_INFO("[HARVEST] Lote de {} cutscenes concluido com sucesso!", s_demo_queue.size());
        REXLOG_INFO("[HARVEST] Solicitando encerramento limpo da aplicacao para commit dos caches...");
        std::printf("[HARVEST] Lote de %zu cutscenes concluido com sucesso!\n", s_demo_queue.size());
        std::printf("[HARVEST] Solicitando encerramento limpo da aplicacao para commit dos caches...\n");
        std::fflush(stdout);
        _flushall();
        majin::RequestCleanQuit();
      } else {
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - s_shutdown_time).count();
        if (elapsed >= 3) {
          REXLOG_INFO("[HARVEST] Watchdog de finalizacao: chamando exit(0)...");
          std::printf("[HARVEST] Watchdog de finalizacao: chamando exit(0)...\n");
          std::fflush(stdout);
          _flushall();
          std::exit(0);
        }
      }
      break;
    }
  }
}
