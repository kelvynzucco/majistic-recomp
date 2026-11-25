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
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
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

// Scene fog intensity (slider from 0.0 to 1.0, default 0.20, 1.0 = original).
// Accepts both booleans ("true" -> 1.0, "false" -> 0.0) and floats ("0.0" to "1.0").
double& FLAGS_majin_fog_storage_() {
  static double storage = 0.20;
  return storage;
}
static auto _cvar_reg_majin_fog = ::rex::cvar::FlagRegistrar({
    "majin_fog",
    ::rex::cvar::FlagType::Double,
    "Majin",
    "Scene Fog Intensity (0.0 = off, 0.20 = default, 1.0 = original)",
    [](std::string_view v) {
      if (v == "true" || v == "yes" || v == "on") {
        FLAGS_majin_fog_storage_() = 1.0;
        return true;
      }
      if (v == "false" || v == "no" || v == "off") {
        FLAGS_majin_fog_storage_() = 0.0;
        return true;
      }
      double val = 0.20;
      if (!::rex::cvar::ParseDouble(v, val) || !std::isfinite(val)) {
        return false;
      }
      FLAGS_majin_fog_storage_() = std::clamp(val, 0.0, 1.0);
      return true;
    },
    []() { return std::to_string(FLAGS_majin_fog_storage_()); },
    [](std::string_view) {},
    ::rex::cvar::Lifecycle::kHotReload,
    {},
    "0.200000",
    false});

void Majin_DofEnabled(PPCRegister& r11) {
  if (!REXCVAR_GET(majin_dof)) r11.u64 = 0;
}

void Majin_BloomEnabled(PPCRegister& r11) {
  if (REXCVAR_GET(majin_bloom) <= 0.001) {
    r11.u64 = 0;
  }
}

// Intercepts fog enable test (cmplwi cr6, r5, 0 at 0x8218B6E8 in sub_8218B600, identical in PAL & USA).
// When majin_fog <= 0.001, zeroes r5 so the engine takes its native fog-disabled branch (loc_8218B804),
// cleanly disabling fog across all 557 scene shaders (both fogEnable and fogParam/fogSelect paths).
void Majin_FogEnabled(PPCRegister& r5) {
  if (REXCVAR_GET(majin_fog) <= 0.001) {
    r5.u64 = 0;
  }
}

// Intercepts fog parameter upload (loc_8218B858 before fsubs f12, f0, f12 in sub_8218B600,
// identical in PAL & USA) where all 3 fog modes converge:
//   - Linear (fogSelect = {1, 0, 0, 1}): f12 = fogStart, f0 = fogEnd -> push range out by 1 / fog
//   - Exp    (fogSelect = {0, 1, 0, 1}): f13 = fogDensity            -> scale density by fog
//   - Exp2   (fogSelect = {0, 0, 1, 1}): f13 = fogDensity            -> scale density by fog
//   - Off    (fogSelect = {1, 1, 1, 1}): loc_8218B804 native off     -> untouched
void Majin_FogParams(PPCRegister& f0, PPCRegister& f12, PPCRegister& f13, PPCRegister& r1) {
  const double raw = REXCVAR_GET(majin_fog);
  if (!std::isfinite(raw) || raw >= 0.999) {
    return;
  }
  const double fog = std::clamp(raw, 0.0, 1.0);
  if (fog <= 0.001) {
    return;  // Already routed through native disabled path (loc_8218B804).
  }
  if (r1.u32 < 0x10000u || r1.u32 >= 0xF0000000u) {
    return;
  }

  auto load_stack_f32 = [](uint32_t addr) -> float {
    uint32_t be = 0;
    std::memcpy(&be, REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(addr), 4);
    return std::bit_cast<float>(std::byteswap(be));
  };

  const float sel_linear = load_stack_f32(r1.u32 + 160);
  const float sel_exp = load_stack_f32(r1.u32 + 164);
  const float sel_exp2 = load_stack_f32(r1.u32 + 168);

  // Skip natively disabled fog calls (loc_8218B804 fills fogSelect = {1, 1, 1, 1}).
  if (sel_linear > 0.5f && sel_exp > 0.5f) {
    return;
  }

  if (sel_linear > 0.5f) {
    if (std::isfinite(f0.f64) && std::isfinite(f12.f64)) {
      const double orig_start = f12.f64;
      const double orig_end = f0.f64;
      const double range = orig_end - orig_start;
      if (range > 1e-3) {
        const double dist_mult = 1.0 / fog;
        if (orig_start >= 0.0) {
          f12.f64 = double(float(orig_start * dist_mult));
          f0.f64 = double(float(orig_end * dist_mult));
        } else {
          const double new_start = orig_start * fog;
          f12.f64 = double(float(new_start));
          f0.f64 = double(float(new_start + range * dist_mult));
        }
      }
    }
  } else if (sel_exp > 0.5f || sel_exp2 > 0.5f) {
    if (std::isfinite(f13.f64) && f13.f64 > 0.0) {
      f13.f64 = double(float(f13.f64 * fog));
    }
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
//   +992: Dynamic extraction luminance Threshold
//   +996: Dynamic composite blend weight
//   +1000: Dynamic mode selector (0 = use dynamic +980..+996, !=0 = use static table 0x820E2BA0)
//   +1008: Dynamic streak brightness
//   When the engine is in default static mode (1000 != 0), baseline extraction constants
//   are loaded from ROM (0x820E2BA0 / 0x820D8E8C) and modulated by intensity, setting +1000 = 0
//   so sub_821A2C10 and sub_821A3300 execute the dynamic path smoothly and continuously.
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
    const uint32_t orig_992  = REX_LOAD_U32(ctx.r3.u32 + 992);
    const uint32_t orig_996  = REX_LOAD_U32(ctx.r3.u32 + 996);
    const uint32_t orig_1000 = REX_LOAD_U32(ctx.r3.u32 + 1000);
    const uint32_t orig_1008 = REX_LOAD_U32(ctx.r3.u32 + 1008);

    float struct_r = 0.0f, struct_g = 0.0f, struct_b = 0.0f;
    float struct_thresh = 0.0f, struct_comp = 0.0f, struct_streak = 0.0f;
    std::memcpy(&struct_r, &orig_980, sizeof(float));
    std::memcpy(&struct_g, &orig_984, sizeof(float));
    std::memcpy(&struct_b, &orig_988, sizeof(float));
    std::memcpy(&struct_thresh, &orig_992, sizeof(float));
    std::memcpy(&struct_comp, &orig_996, sizeof(float));
    std::memcpy(&struct_streak, &orig_1008, sizeof(float));

    float base_r = 1.0f, base_g = 1.0f, base_b = 1.0f;
    float base_thresh = 1.0f, base_f0 = 1.0f, base_comp = 1.0f;

    if (orig_1000 != 0 || (struct_r == 0.0f && struct_g == 0.0f)) {
      // Baseline constants from guest ROM static table (0x820E2BA0 / 0x820D8E8C / 0x820D8EB4)
      const uint32_t u_static_r      = REX_LOAD_U32(0x820E2BA0 + 956);
      const uint32_t u_static_g      = REX_LOAD_U32(0x820E2BA0 + 512);
      const uint32_t u_static_b      = REX_LOAD_U32(0x820E2BA0 + 608);
      const uint32_t u_static_thresh = REX_LOAD_U32(0x820D8E8C);
      const uint32_t u_static_f0     = REX_LOAD_U32(0x820D8EB4);
      const uint32_t u_static_comp   = REX_LOAD_U32(0x820E2BA0 + 272);

      float sr = 0.0f, sg = 0.0f, sb = 0.0f, st = 0.0f, sf0 = 0.0f, sc = 0.0f;
      std::memcpy(&sr, &u_static_r, sizeof(float));
      std::memcpy(&sg, &u_static_g, sizeof(float));
      std::memcpy(&sb, &u_static_b, sizeof(float));
      std::memcpy(&st, &u_static_thresh, sizeof(float));
      std::memcpy(&sf0, &u_static_f0, sizeof(float));
      std::memcpy(&sc, &u_static_comp, sizeof(float));

      if (std::isfinite(sr) && sr > 0.0f) base_r = sr;
      if (std::isfinite(sg) && sg > 0.0f) base_g = sg;
      if (std::isfinite(sb) && sb > 0.0f) base_b = sb;
      if (std::isfinite(st) && st > 0.0f) base_thresh = st;
      if (std::isfinite(sf0) && sf0 != 0.0f) base_f0 = sf0;
      if (std::isfinite(sc) && sc > 0.0f) base_comp = sc;
    } else {
      if (std::isfinite(struct_r)) base_r = struct_r;
      if (std::isfinite(struct_g)) base_g = struct_g;
      if (std::isfinite(struct_b)) base_b = struct_b;
      if (std::isfinite(struct_thresh)) base_thresh = struct_thresh;
      if (std::isfinite(struct_comp) && struct_comp > 0.0f) base_comp = struct_comp;
      const uint32_t u_static_f0 = REX_LOAD_U32(0x820D8EB4);
      float sf0 = 0.0f;
      std::memcpy(&sf0, &u_static_f0, sizeof(float));
      if (std::isfinite(sf0) && sf0 != 0.0f) base_f0 = sf0;
    }

    float new_980  = std::clamp(base_r * intensity, 0.0f, 100.0f);
    float new_984  = std::clamp((base_g / base_f0) * intensity, 0.0f, 100.0f);
    float new_988  = std::clamp((base_b / base_f0) * intensity, 0.0f, 100.0f);
    float new_992  = std::clamp(base_thresh * intensity, 0.0f, 100.0f);
    float new_996  = std::clamp(base_comp, 0.001f, 100.0f);

    const bool is_streak_float = std::isfinite(struct_streak) && struct_streak > 0.0f;
    float new_1008 = is_streak_float ? std::clamp(struct_streak * intensity, 0.0f, 100.0f) : struct_streak;

    uint32_t u_980 = 0, u_984 = 0, u_988 = 0, u_992 = 0, u_996 = 0, u_1008 = 0;
    std::memcpy(&u_980, &new_980, sizeof(float));
    std::memcpy(&u_984, &new_984, sizeof(float));
    std::memcpy(&u_988, &new_988, sizeof(float));
    std::memcpy(&u_992, &new_992, sizeof(float));
    std::memcpy(&u_996, &new_996, sizeof(float));
    std::memcpy(&u_1008, &new_1008, sizeof(float));

    REX_STORE_U32(ctx.r3.u32 + 980, u_980);
    REX_STORE_U32(ctx.r3.u32 + 984, u_984);
    REX_STORE_U32(ctx.r3.u32 + 988, u_988);
    REX_STORE_U32(ctx.r3.u32 + 992, u_992);
    REX_STORE_U32(ctx.r3.u32 + 996, u_996);
    REX_STORE_U32(ctx.r3.u32 + 1000, 0); // Forces sub_821A2C10 and sub_821A3300 to use dynamic values
    if (is_streak_float) {
      REX_STORE_U32(ctx.r3.u32 + 1008, u_1008);
    }

    __imp__sub_821A2C10(ctx, base);

    // Cleanly restore all modified struct entries
    REX_STORE_U32(ctx.r3.u32 + 980, orig_980);
    REX_STORE_U32(ctx.r3.u32 + 984, orig_984);
    REX_STORE_U32(ctx.r3.u32 + 988, orig_988);
    REX_STORE_U32(ctx.r3.u32 + 992, orig_992);
    REX_STORE_U32(ctx.r3.u32 + 996, orig_996);
    REX_STORE_U32(ctx.r3.u32 + 1000, orig_1000);
    if (is_streak_float) {
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
