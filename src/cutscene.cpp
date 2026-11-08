// SPDX-License-Identifier: GPL-3.0-only
// Majistic Recomp - Unified In-game 3D Cutscene (DEMO) Fast-Forward
//
// Fast-forward is driven purely by the state of the engine's DEMO player, read
// once per frame from the main timing hook (timing.cpp). It is NOT driven by
// script-command polling (DEMO_END_CHECK / CHECK_END_MESSAGE), because those are
// also polled while the player is in control (gameplay dialogue, event cameras).

#include <windows.h>
#include <atomic>
#include <bit>
#include <cmath>
#include "generated/default/majin_pch.h"
#include "edition_offsets.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/audio/audio_driver.h>

REXCVAR_DEFINE_BOOL(majin_skip_cutscenes, true, "Gameplay",
    "Allow fast-forwarding in-game cutscenes by holding Select/Back on controller or Tab/Backspace on keyboard");

REXCVAR_DEFINE_INT32(majin_cutscene_speed, 4, "Gameplay",
    "In-game cutscene fast-forward speed multiplier (2, 4, 6, 8)");

REXCVAR_DEFINE_INT32(majin_cutscene_end_margin, 90, "Gameplay",
    "Cutscene frames before the end of a DEMO where fast-forward is forced back to normal speed");

REXCVAR_DEFINE_BOOL(majin_cutscene_ff_camera_demos, false, "Gameplay",
    "Also allow fast-forward during short DEMO_CAMERA events (camera-only scripted shots)");

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
      REXLOG_INFO("[Majin Cutscene] button pressed during DEMO (frame {:.1f}/{:.1f}, step {:.2f}, {})",
                  demo.current, demo.end, demo.step, MAJIN_EDITION_NAME);
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
      REXLOG_INFO("[Majin Cutscene] fast-forward x{} ({} frames left, {})",
                  static_cast<int>(multiplier), static_cast<int>(remaining), MAJIN_EDITION_NAME);
    } else {
      REXLOG_INFO("[Majin Cutscene] fast-forward off ({})", MAJIN_EDITION_NAME);
      // If turbo stopped while the button is still held (end margin reached or cutscene ended),
      // lock out turbo so it never affects gameplay even if held continuously.
      if (button_held) {
        s_must_release_button.store(true, std::memory_order_relaxed);
      }
    }
  }
  return multiplier;
}
