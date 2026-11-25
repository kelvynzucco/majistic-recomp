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

// Depth of Field intensity (slider from 0.0 to 2.0, default 0.40).
// Accepts both booleans ("true" -> 0.40, "false" -> 0.0) from the launcher / CLI
// and float numbers ("0.0" to "2.0") from the in-game settings slider.
double& FLAGS_majin_dof_storage_() {
  static double storage = 0.40;
  return storage;
}
static auto _cvar_reg_majin_dof = ::rex::cvar::FlagRegistrar({
    "majin_dof",
    ::rex::cvar::FlagType::Double,
    "Majin",
    "Depth of Field intensity (0.0 = off, 0.40 = default, 2.0 = strong bokeh)",
    [](std::string_view v) {
      if (v == "true" || v == "yes" || v == "on") {
        FLAGS_majin_dof_storage_() = 0.40;
        return true;
      }
      if (v == "false" || v == "no" || v == "off") {
        FLAGS_majin_dof_storage_() = 0.0;
        return true;
      }
      double val = 0.40;
      if (!::rex::cvar::ParseDouble(v, val) || !std::isfinite(val)) {
        return false;
      }
      FLAGS_majin_dof_storage_() = std::clamp(val, 0.0, 2.0);
      return true;
    },
    []() { return std::to_string(FLAGS_majin_dof_storage_()); },
    [](std::string_view) {},
    ::rex::cvar::Lifecycle::kHotReload,
    {},
    "0.400000",
    false});

// Glare and specular bloom intensity (slider from 0.0 to 2.0, default 0.80).
// Accepts both booleans ("true" -> 0.80, "false" -> 0.0) from the launcher / CLI
// and float numbers ("0.0" to "2.0") from the in-game settings slider.
double& FLAGS_majin_bloom_storage_() {
  static double storage = 0.80;
  return storage;
}
static auto _cvar_reg_majin_bloom = ::rex::cvar::FlagRegistrar({
    "majin_bloom",
    ::rex::cvar::FlagType::Double,
    "Majin",
    "Glare & Specular Bloom (0.0 = off, 0.80 = default, 2.0 = 2x glow)",
    [](std::string_view v) {
      if (v == "true" || v == "yes" || v == "on") {
        FLAGS_majin_bloom_storage_() = 0.80;
        return true;
      }
      if (v == "false" || v == "no" || v == "off") {
        FLAGS_majin_bloom_storage_() = 0.0;
        return true;
      }
      double val = 0.80;
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
    "0.800000",
    false});

REXCVAR_DEFINE_BOOL(majin_pause_distortion, true, "Graphics",
                    "Screen wave distortion effect behind pause and map menus");

// Scene fog intensity (slider from 0.0 to 1.0, default 0.80, 1.0 = original).
// Accepts both booleans ("true" -> 0.80, "false" -> 0.0) and floats ("0.0" to "1.0").
double& FLAGS_majin_fog_storage_() {
  static double storage = 0.80;
  return storage;
}
static auto _cvar_reg_majin_fog = ::rex::cvar::FlagRegistrar({
    "majin_fog",
    ::rex::cvar::FlagType::Double,
    "Majin",
    "Scene Fog Intensity (0.0 = off, 0.80 = default, 1.0 = original)",
    [](std::string_view v) {
      if (v == "true" || v == "yes" || v == "on") {
        FLAGS_majin_fog_storage_() = 0.80;
        return true;
      }
      if (v == "false" || v == "no" || v == "off") {
        FLAGS_majin_fog_storage_() = 0.0;
        return true;
      }
      double val = 0.80;
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
    "0.800000",
    false});

void Majin_DofEnabled(PPCRegister& r11) {
  if (REXCVAR_GET(majin_dof) <= 0.001) {
    r11.u64 = 0;
  }
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

// Intercepts SysKemumaki Glare & Specular Bloom pass (sub_821A2C10, identical in PAL & USA).
// - When majin_bloom <= 0.001f, skips execution completely (also disabled via Majin_BloomEnabled).
// - When majin_bloom > 0.001f:
//   Scales ONLY the luminance weights in the SysKemumaki instance struct (ctx.r3) while keeping
//   luminance thresholds and Gaussian blur dispersion at their exact native values:
//   +980: _glareThreshold       -> majinGlareParam.x (kept untouched; static ROM = 0.75f)
//   +984: _glareLuminance       -> majinGlareParam.y (scaled by intensity; dynamic 0..100+ * 0.01f, static ROM = 0.40f)
//   +988: _glareBloomLuminance  -> majinGlareParam.z (scaled by intensity; dynamic 0..100+ * 0.01f, static ROM = 0.70f)
//   +992: _glareBloomThreshold  -> majinGlareParam.w (kept untouched; static ROM = 0.10f)
//   +996: _glareGaussDispersion -> Gaussian sigma in sub_821A3300 (kept untouched; static ROM = 16.0f)
//   +1000: _glarePreset         -> 0 = dynamic (+980..+996), != 0 = static ROM constants
//   +1008: _starLuminance       -> majinStarParam.y in sub_821A3548 (scaled by intensity; 0..100+ * 0.01f)
//   When _glarePreset != 0, native ROM constants (0x820E2BA0 / 0x820D8E8C / 0x820D8EB4) are copied
//   into +980..+996 with only +984 and +988 scaled by intensity and +1000 temporarily set to 0.
//   When _glarePreset == 0, +984 and +988 are already in percentage units and are scaled directly
//   without dividing by 0.01f or modifying thresholds/dispersion.
//   All modified struct fields are cleanly restored to the saved sys_ptr immediately after the call
//   (since __imp__sub_821A2C10 clobbers volatile register r3).
REX_EXTERN(__imp__sub_821A2C10);
REX_EXTERN(sub_821A2C10) {
  const float raw_intensity = static_cast<float>(REXCVAR_GET(majin_bloom));
  const float intensity = (std::isfinite(raw_intensity) && raw_intensity >= 0.0f)
                              ? std::min(raw_intensity, 2.0f)
                              : 1.0f;
  if (intensity <= 0.001f) {
    return;
  }

  const uint32_t sys_ptr = ctx.r3.u32;
  if (std::abs(intensity - 1.0f) > 0.0005f &&
      sys_ptr >= 0x10000u && sys_ptr < 0xF0000000u) {
    constexpr float kMaxLuminancePct = 1000.0f;

    const uint32_t orig_984  = REX_LOAD_U32(sys_ptr + 984);
    const uint32_t orig_988  = REX_LOAD_U32(sys_ptr + 988);
    const uint32_t orig_1000 = REX_LOAD_U32(sys_ptr + 1000);
    const uint32_t orig_1008 = REX_LOAD_U32(sys_ptr + 1008);

    const float struct_star_lum = std::bit_cast<float>(orig_1008);
    const bool scale_star = std::isfinite(struct_star_lum) && struct_star_lum > 0.0f;
    if (scale_star) {
      const float new_1008 = std::clamp(struct_star_lum * intensity, 0.0f, kMaxLuminancePct);
      REX_STORE_U32(sys_ptr + 1008, std::bit_cast<uint32_t>(new_1008));
    }

    if (orig_1000 != 0) {
      // Static ROM preset mode: preserve exact ROM thresholds and Gaussian dispersion,
      // and scale only glare/bloom luminances into dynamic percentage units.
      const uint32_t orig_980 = REX_LOAD_U32(sys_ptr + 980);
      const uint32_t orig_992 = REX_LOAD_U32(sys_ptr + 992);
      const uint32_t orig_996 = REX_LOAD_U32(sys_ptr + 996);

      const uint32_t u_rom_thresh       = REX_LOAD_U32(0x820E2BA0 + 956);  // 0.75f
      const uint32_t u_rom_glare_lum    = REX_LOAD_U32(0x820E2BA0 + 512);  // 0.40f
      const uint32_t u_rom_bloom_lum    = REX_LOAD_U32(0x820E2BA0 + 608);  // 0.70f
      const uint32_t u_rom_bloom_thresh = REX_LOAD_U32(0x820D8E8C);        // 0.10f
      const uint32_t u_rom_pct_scale    = REX_LOAD_U32(0x820D8EB4);        // 0.01f
      const uint32_t u_rom_gauss_disp   = REX_LOAD_U32(0x820E2BA0 + 272);  // 16.0f

      const float rom_glare_lum = std::bit_cast<float>(u_rom_glare_lum);
      const float rom_bloom_lum = std::bit_cast<float>(u_rom_bloom_lum);
      const float raw_pct_scale = std::bit_cast<float>(u_rom_pct_scale);
      const float pct_scale = (std::isfinite(raw_pct_scale) && raw_pct_scale > 0.0f)
                                  ? raw_pct_scale
                                  : 0.01f;

      const float new_984 = (std::isfinite(rom_glare_lum) && rom_glare_lum > 0.0f)
                                ? std::clamp((rom_glare_lum / pct_scale) * intensity, 0.0f, kMaxLuminancePct)
                                : 0.0f;
      const float new_988 = (std::isfinite(rom_bloom_lum) && rom_bloom_lum > 0.0f)
                                ? std::clamp((rom_bloom_lum / pct_scale) * intensity, 0.0f, kMaxLuminancePct)
                                : 0.0f;

      REX_STORE_U32(sys_ptr + 980, u_rom_thresh);
      REX_STORE_U32(sys_ptr + 984, std::bit_cast<uint32_t>(new_984));
      REX_STORE_U32(sys_ptr + 988, std::bit_cast<uint32_t>(new_988));
      REX_STORE_U32(sys_ptr + 992, u_rom_bloom_thresh);
      REX_STORE_U32(sys_ptr + 996, u_rom_gauss_disp);
      REX_STORE_U32(sys_ptr + 1000, 0);

      __imp__sub_821A2C10(ctx, base);

      REX_STORE_U32(sys_ptr + 980, orig_980);
      REX_STORE_U32(sys_ptr + 984, orig_984);
      REX_STORE_U32(sys_ptr + 988, orig_988);
      REX_STORE_U32(sys_ptr + 992, orig_992);
      REX_STORE_U32(sys_ptr + 996, orig_996);
      REX_STORE_U32(sys_ptr + 1000, orig_1000);
    } else {
      // Dynamic (.epm) mode: +984 and +988 are already in 0..100+ percentage units.
      // Keep thresholds (+980, +992) and Gaussian dispersion (+996) completely untouched.
      const float struct_glare_lum = std::bit_cast<float>(orig_984);
      const float struct_bloom_lum = std::bit_cast<float>(orig_988);
      const bool scale_glare = std::isfinite(struct_glare_lum) && struct_glare_lum > 0.0f;
      const bool scale_bloom = std::isfinite(struct_bloom_lum) && struct_bloom_lum > 0.0f;

      if (scale_glare) {
        const float new_984 = std::clamp(struct_glare_lum * intensity, 0.0f, kMaxLuminancePct);
        REX_STORE_U32(sys_ptr + 984, std::bit_cast<uint32_t>(new_984));
      }
      if (scale_bloom) {
        const float new_988 = std::clamp(struct_bloom_lum * intensity, 0.0f, kMaxLuminancePct);
        REX_STORE_U32(sys_ptr + 988, std::bit_cast<uint32_t>(new_988));
      }

      __imp__sub_821A2C10(ctx, base);

      if (scale_glare) {
        REX_STORE_U32(sys_ptr + 984, orig_984);
      }
      if (scale_bloom) {
        REX_STORE_U32(sys_ptr + 988, orig_988);
      }
    }

    if (scale_star) {
      REX_STORE_U32(sys_ptr + 1008, orig_1008);
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
