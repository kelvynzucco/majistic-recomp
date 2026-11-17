// SPDX-License-Identifier: GPL-3.0-only
// Majistic Recomp - switches for the game's post effects
// Adapted from ArthurMS15/MajinRecomp
//
// SysKemumaki (the post effect system) draws its passes from one function
// (sub_821A00E0), each behind the "enabled" flag of the frame's parameters
// (r28): +0x448 depth of field (sub_821A4028), +0x3A4 glare and bloom
// (sub_821A2C10), and +0x5A4 (1444(r28)) the pause/map transition filter
// TKemumakiPause (sub_821A5F98). Each hook runs between the flag's load and
// its test, and clears it when switched off.

#include "generated/default/majin_pch.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>

REXCVAR_DEFINE_BOOL(majin_dof, true, "Majin", "Depth of field (background blur)");

// Glare and specular bloom intensity (slider from 0.0 to 2.0, default 1.0).
// Accepts both booleans ("true" -> 1.0, "false" -> 0.0) from the launcher / CLI
// and float numbers ("0.0" to "2.0") from the in-game settings slider.
double& FLAGS_majin_bloom_storage_() {
  static double storage = 1.0;
  return storage;
}
static auto _cvar_reg_majin_bloom = ::rex::cvar::FlagRegistrar({
    "majin_bloom",
    ::rex::cvar::FlagType::Double,
    "Majin",
    "Glare & Specular Bloom",
    [](std::string_view v) {
      if (v == "true" || v == "yes" || v == "on") {
        FLAGS_majin_bloom_storage_() = 1.0;
        return true;
      }
      if (v == "false" || v == "no" || v == "off") {
        FLAGS_majin_bloom_storage_() = 0.0;
        return true;
      }
      double val = 0.0;
      if (!::rex::cvar::ParseDouble(v, val)) {
        return false;
      }
      if (val < 0.0) val = 0.0;
      if (val > 2.0) val = 2.0;
      FLAGS_majin_bloom_storage_() = val;
      return true;
    },
    []() { return std::to_string(FLAGS_majin_bloom_storage_()); },
    [](std::string_view) {},
    ::rex::cvar::Lifecycle::kHotReload,
    {},
    "1.000000",
    false});

REXCVAR_DEFINE_BOOL(majin_pause_distortion, true, "Graphics",
                    "Screen wave distortion effect behind pause and map menus");

void Majin_DofEnabled(PPCRegister& r11) {
  if (!REXCVAR_GET(majin_dof)) r11.u64 = 0;
}

void Majin_BloomEnabled(PPCRegister& r11) {
  if (REXCVAR_GET(majin_bloom) <= 0.001) {
    r11.u64 = 0;
  }
}

// Intercepts the TKemumakiPause transition factor load (lfs f0, 1444(r28) at 0x821A0B60)
// in sub_821A00E0 before fcmpu cr6, f0, f31 (0x821A0B64).
// When majin_pause_distortion is false, zeroing f0 takes ble cr6, 0x821A0BF4,
// skipping both the unnecessary EDRAM Resolve into 184(r28) and sub_821A5F98.
void Majin_PauseDistortionEnabled(PPCRegister& f0) {
  if (!REXCVAR_GET(majin_pause_distortion)) {
    f0.f64 = 0.0;
  }
}

// Intercepts SysKemumaki Glare & Specular Bloom pass (sub_821A2C10, 0x821A2C10).
// - When majin_bloom <= 0.001f, skips execution completely (also disabled via Majin_BloomEnabled).
// - When majin_bloom > 0.001f:
//   Scales dynamic parameters within the SysKemumaki instance struct (ctx.r3):
//   +980: Dynamic extraction Red
//   +984: Dynamic extraction Green
//   +988: Dynamic extraction Blue
//   +996: Dynamic composite blend weight
//   +1008: Dynamic streak brightness
//   All modified memory entries are cleanly restored immediately after the draw call.
REX_EXTERN(__imp__sub_821A2C10);
REX_EXTERN(sub_821A2C10) {
  const float raw_intensity = static_cast<float>(REXCVAR_GET(majin_bloom));
  const float intensity = (std::isfinite(raw_intensity) && raw_intensity >= 0.0f)
                              ? std::min(raw_intensity, 2.0f)
                              : 1.0f;
  if (intensity <= 0.001f) {
    return;
  }

  if (intensity != 1.0f && ctx.r3.u32 >= 0x10000) {
    // Dynamic struct entries at ctx.r3 (heap-allocated SysKemumaki instance parameters):
    const uint32_t orig_980  = REX_LOAD_U32(ctx.r3.u32 + 980);
    const uint32_t orig_984  = REX_LOAD_U32(ctx.r3.u32 + 984);
    const uint32_t orig_988  = REX_LOAD_U32(ctx.r3.u32 + 988);
    const uint32_t orig_996  = REX_LOAD_U32(ctx.r3.u32 + 996);
    const uint32_t orig_1008 = REX_LOAD_U32(ctx.r3.u32 + 1008);

    float val_980 = 0.0f, val_984 = 0.0f, val_988 = 0.0f, val_996 = 0.0f, val_1008 = 0.0f;
    std::memcpy(&val_980, &orig_980, sizeof(float));
    std::memcpy(&val_984, &orig_984, sizeof(float));
    std::memcpy(&val_988, &orig_988, sizeof(float));
    std::memcpy(&val_996, &orig_996, sizeof(float));
    std::memcpy(&val_1008, &orig_1008, sizeof(float));

    bool scaled_dynamic = false;
    if (std::isfinite(val_980) && std::isfinite(val_984) &&
        std::isfinite(val_988) && std::isfinite(val_996) &&
        std::isfinite(val_1008)) {
      float new_980  = std::clamp(val_980 * intensity, 0.0f, 100.0f);
      float new_984  = std::clamp(val_984 * intensity, 0.0f, 100.0f);
      float new_988  = std::clamp(val_988 * intensity, 0.0f, 100.0f);
      float new_996  = std::clamp(val_996 * intensity, 0.0f, 100.0f);
      float new_1008 = std::clamp(val_1008 * intensity, 0.0f, 100.0f);

      uint32_t u_980 = 0, u_984 = 0, u_988 = 0, u_996 = 0, u_1008 = 0;
      std::memcpy(&u_980, &new_980, sizeof(float));
      std::memcpy(&u_984, &new_984, sizeof(float));
      std::memcpy(&u_988, &new_988, sizeof(float));
      std::memcpy(&u_996, &new_996, sizeof(float));
      std::memcpy(&u_1008, &new_1008, sizeof(float));

      REX_STORE_U32(ctx.r3.u32 + 980, u_980);
      REX_STORE_U32(ctx.r3.u32 + 984, u_984);
      REX_STORE_U32(ctx.r3.u32 + 988, u_988);
      REX_STORE_U32(ctx.r3.u32 + 996, u_996);
      REX_STORE_U32(ctx.r3.u32 + 1008, u_1008);
      scaled_dynamic = true;
    }

    __imp__sub_821A2C10(ctx, base);

    // Cleanly restore all dynamic values
    if (scaled_dynamic) {
      REX_STORE_U32(ctx.r3.u32 + 980, orig_980);
      REX_STORE_U32(ctx.r3.u32 + 984, orig_984);
      REX_STORE_U32(ctx.r3.u32 + 988, orig_988);
      REX_STORE_U32(ctx.r3.u32 + 996, orig_996);
      REX_STORE_U32(ctx.r3.u32 + 1008, orig_1008);
    }
    return;
  }
  __imp__sub_821A2C10(ctx, base);
}

// Intercepts SysKemumaki TKemumakiPause pass (sub_821A5F98, 0x821A5F98 in both USA and PAL).
// - When majin_pause_distortion is false, skips sub_821A5F98 completely.
// - When majin_pause_distortion is true, zeroes 1520(r3) (pauseSurfaceParam2.w base scale,
//   default 0.10f) so c2.w = 0.0f * t = 0.0f in PS = 0x62F2F461F0C692A1. This prevents a
//   zeroed PauseTexNormal from shifting refractedUV by ~7-9% and melting top/left borders
//   with CLAMP_TO_EDGE, while preserving the transition fade, caustics, and specular highlight.
REX_EXTERN(__imp__sub_821A5F98);
REX_EXTERN(sub_821A5F98) {
  if (!REXCVAR_GET(majin_pause_distortion)) {
    return;
  }
  if (ctx.r3.u32 >= 0x10000) {
    REX_STORE_U32(ctx.r3.u32 + 1520, 0);
  }
  __imp__sub_821A5F98(ctx, base);
}
