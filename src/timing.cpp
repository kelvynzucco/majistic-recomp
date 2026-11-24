// SPDX-License-Identifier: GPL-3.0-only
// Majistic Recomp - Unified Timing & Engine Synchronization Hooks
#include "generated/default/majin_pch.h"
#include "edition_offsets.h"
#include "equivalent_counter.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <algorithm>
#include <bit>
#include <cmath>

REXCVAR_DEFINE_BOOL(majin_60fps, false, "Graphics",
    "Opt-in 60 FPS test mode for Majistic Recomp");

REXCVAR_DEFINE_BOOL(majin_skip_intro_logos, false, "Gameplay",
    "Skip startup publisher and developer logos and jump directly to the title screen");

REXCVAR_DECLARE(bool, majin_harvest_demos);
REXCVAR_DECLARE(double, majin_harvest_speed);

namespace {
thread_local uint32_t main_object = 0;
constexpr uint32_t kRatio = 0x150F8, kRate = 0x15104;

bool Enabled() {
  return REXCVAR_GET(majin_60fps);
}
bool Span(uint32_t object, uint32_t size) {
  return object >= 0x10000 && object <= UINT32_MAX - size;
}
float ReadFloat(uint8_t* base, uint32_t address) {
  return std::bit_cast<float>(REX_LOAD_U32(address));
}
}  // namespace

extern float UpdateCutsceneTurbo(uint8_t* base);
extern void UpdateCutsceneHarvest(PPCContext& ctx, uint8_t* base);

// Below 0x821C0000: Address is identical across PAL and USA (sub_82162238, caller LR 0x821616EC)
REX_EXTERN(__imp__sub_82162238);
REX_EXTERN(sub_82162238) {
  const uint32_t object = ctx.r3.u32;
  const bool caller = uint32_t(ctx.lr) == 0x821616EC;
  __imp__sub_82162238(ctx, base);
  if (!caller || !Span(object, kRate + 4)) return;

  const float rate = ReadFloat(base, object + kRate);
  float ratio = ReadFloat(base, object + kRatio);

  float speed_mult = UpdateCutsceneTurbo(base);
  if (REXCVAR_GET(majin_harvest_demos)) {
    const double raw_speed = REXCVAR_GET(majin_harvest_speed);
    const float harvest_spd = static_cast<float>(
        (std::isfinite(raw_speed) && raw_speed > 0.0) ? raw_speed : 5.0);
    // Safe clamp to avoid particle emitter frame skipping
    const float safe_harvest_spd = std::min(std::max(harvest_spd, 1.0f), 10.0f);
    speed_mult = std::max(speed_mult, safe_harvest_spd);
  }

  if (speed_mult > 1.0f && std::isfinite(ratio)) {
    ratio *= speed_mult;
    REX_STORE_U32(object + kRatio, std::bit_cast<uint32_t>(ratio));
    REX_STORE_U32(object + 0x150F4, std::bit_cast<uint32_t>(ratio / rate));
  }

  UpdateCutsceneHarvest(ctx, base);

  if (!Enabled()) return;
  main_object = rate == 30.0f && std::isfinite(ratio) && ratio >= 0 && ratio <= 64
                    ? object : 0;
  static const bool announced = [] {
    REXLOG_INFO("[Majin 60 FPS] opt-in mode; engine time calculation unchanged; title idle timer normalized ({})",
                MAJIN_EDITION_NAME);
    return true;
  }();
  (void)announced;
}

// SceneTitleTitle::Update (PAL: sub_821CC390 / USA: sub_821CC320)
REX_EXTERN(MAJIN_IMP_TITLE_UPDATE);
REX_EXTERN(MAJIN_FN_TITLE_UPDATE) {
  if (!Enabled()) { MAJIN_IMP_TITLE_UPDATE(ctx, base); return; }
  const uint32_t object = ctx.r3.u32;
  thread_local majin::timing::EquivalentCounter counter;
  thread_local uint32_t owner = 0;
  if (!main_object || !Span(object, 100) ||
      REX_LOAD_U32(object) != majin::offsets::kTitleIdleVtable ||
      REX_LOAD_U32(object + 48) != 6 || REX_LOAD_U32(object + 88) > 870) {
    counter.Reset(); owner = 0;
    MAJIN_IMP_TITLE_UPDATE(ctx, base); return;
  }
  if (owner != object) { counter.Reset(); owner = object; }
  const double ratio = ReadFloat(base, main_object + kRatio);
  REX_STORE_U32(object + 88,
      counter.BeforeIncrement(REX_LOAD_U32(object + 88), ratio));
  MAJIN_IMP_TITLE_UPDATE(ctx, base);
}

// SceneTitleLogo::Update (PAL: sub_821CEB68 / USA: sub_821CEAF8)
REX_EXTERN(MAJIN_IMP_TITLE_LOGO);
REX_EXTERN(MAJIN_FN_TITLE_LOGO) {
  const uint32_t object = ctx.r3.u32;
  const bool valid_logo = Span(object, 68) &&
      REX_LOAD_U32(object) == majin::offsets::kTitleLogoVtable;
  const uint32_t s_before = valid_logo ? REX_LOAD_U32(object + 48) : 0;
  if (valid_logo && s_before == 0 && REXCVAR_GET(majin_skip_intro_logos)) {
    // Bypass logo movie creation (state 0), 1s fade-in (state 1), logo playback (state 2),
    // and 1s fade-out (state 3), jumping straight to state 4 (essential package wait + init)
    // and state 5 (clean logo SWF/pak unload + transition to SceneTitleTitle).
    constexpr uint32_t kFadeManagerOffset = 0x15114;
    const uint32_t scenario = REX_LOAD_U32(majin::offsets::kScenarioRootPtr);
    if (Span(scenario, kFadeManagerOffset + 4)) {
      const uint32_t fade_mgr = REX_LOAD_U32(scenario + kFadeManagerOffset);
      if (Span(fade_mgr, 7284)) {
        // Cancel SceneTitleLogo::OnEnter's fade-in and hold the solid black post-fade-out
        // overlay until SceneTitleTitle takes over.
        REX_STORE_U32(fade_mgr + 20, 0u);
        REX_STORE_U32(fade_mgr + 24, 0u);
        REX_STORE_U32(fade_mgr + 28, 0u);  // fade-in inactive
        REX_STORE_U32(fade_mgr + 32, 0u);  // fade-out inactive
        REX_STORE_U32(fade_mgr + 36, 1u);  // fade-out completed (solid black)
        REX_STORE_U32(fade_mgr + 40, 0u);
        REX_STORE_U32(fade_mgr + 52, 0u);
        REX_STORE_U32(fade_mgr + 60, 0u);
        REX_STORE_U32(fade_mgr + 7272, 0u);
      }
    }
    REX_STORE_U32(object + 48, 4u);
    static bool announced = false;
    if (!announced) {
      REXLOG_INFO("[Majin Skip Logos] bypassed logo movie and fades (state 0 -> 4; {})",
                  MAJIN_EDITION_NAME);
      announced = true;
    }
  }
  MAJIN_IMP_TITLE_LOGO(ctx, base);
  const uint32_t s_after = valid_logo ? REX_LOAD_U32(object + 48) : 0;
#if defined(MAJIN_ENABLE_DIAGNOSTICS)
  if (s_before != s_after && REXCVAR_GET(majin_skip_intro_logos)) {
    REXLOG_INFO("[Majin SceneTitleLogo] state {} -> {}", s_before, s_after);
  }
#endif
}

// Scaleform GFx Invoke (PAL: sub_823D4B80 / USA: sub_823D4B10)
REX_EXTERN(MAJIN_IMP_SCALEFORM_INVOKE);
REX_EXTERN(MAJIN_FN_SCALEFORM_INVOKE) {
  if (uint32_t(ctx.lr) == majin::offsets::kLogoEndFlagCallLR && REXCVAR_GET(majin_skip_intro_logos)) {
    static bool announced = false;
    if (!announced) {
      REXLOG_INFO("[Majin Skip Logos] fallback logo movie completion on tick 1 ({})",
                  MAJIN_EDITION_NAME);
      announced = true;
    }
    ctx.r3.u64 = majin::offsets::kGuestTrueString;
    return;
  }
  MAJIN_IMP_SCALEFORM_INVOKE(ctx, base);
}
