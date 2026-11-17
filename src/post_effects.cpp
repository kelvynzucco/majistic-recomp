// SPDX-License-Identifier: GPL-3.0-only
// Majistic Recomp - switches for the game's post effects
// Adapted from ArthurMS15/MajinRecomp
//
// SysKemumaki (the post effect system) draws its passes from one function
// (sub_821A00E0), each behind the "enabled" flag of the frame's parameters
// (r28): +0x418 the pause screen blur (sub_821A1530), +0x448 depth of field
// (sub_821A4028), +0x3A4 glare and bloom (sub_821A2C10), and +0x5A4 (1444(r28))
// the pause/map transition filter TKemumakiPause (sub_821A5F98). Each hook runs
// between the flag's load and its test, and clears it when switched off.

#include "generated/default/majin_pch.h"

#include <rex/cvar.h>
#include <rex/ppc/context.h>

#include <cstring>

#ifndef REXCVAR_DEFINE_FLOAT
#define REXCVAR_DEFINE_FLOAT REXCVAR_DEFINE_DOUBLE
#endif

REXCVAR_DEFINE_FLOAT(majin_pause_blur, 1.0f, "Majin",
                     "Atmospheric light glow intensity")
    .range(0.0, 2.0);
REXCVAR_DEFINE_BOOL(majin_dof, true, "Majin", "Depth of field (background blur)");
REXCVAR_DEFINE_BOOL(majin_bloom, true, "Majin", "Glare and bloom around bright light");
REXCVAR_DEFINE_BOOL(majin_force_pause_blur, false, "Majin",
                    "Draw the pause screen blur all the time (testing)");
REXCVAR_DEFINE_BOOL(majin_pause_distortion, true, "Graphics",
                    "Screen wave distortion effect behind pause and map menus");

void Majin_PauseBlurEnabled(PPCRegister& r11) {
  const double intensity = REXCVAR_GET(majin_pause_blur);
  if (intensity <= 0.0) {
    r11.u64 = 0;
  } else if (REXCVAR_GET(majin_force_pause_blur)) {
    r11.u64 = 1;
  }
}

void Majin_DofEnabled(PPCRegister& r11) {
  if (!REXCVAR_GET(majin_dof)) r11.u64 = 0;
}

void Majin_BloomEnabled(PPCRegister& r11) {
  if (!REXCVAR_GET(majin_bloom)) r11.u64 = 0;
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

// Intercepts SysKemumaki atmospheric light diffusion / pause blur pass (sub_821A1530).
// - When majin_pause_blur <= 0.0f, skips execution completely.
// - When majin_pause_blur > 0.0f, scales the blur intensity parameters at
//   1096(r3) and 1108(r3) in SysKemumaki by majin_pause_blur for this pass
//   and cleanly restores them afterwards.
REX_EXTERN(__imp__sub_821A1530);
REX_EXTERN(sub_821A1530) {
  const float intensity = static_cast<float>(REXCVAR_GET(majin_pause_blur));
  if (intensity <= 0.0f) {
    return;
  }
  if (ctx.r3.u32 >= 0x10000) {
    const uint32_t orig_1096_u32 = REX_LOAD_U32(ctx.r3.u32 + 1096);
    const uint32_t orig_1108_u32 = REX_LOAD_U32(ctx.r3.u32 + 1108);

    float val_1096 = 0.0f;
    float val_1108 = 0.0f;
    std::memcpy(&val_1096, &orig_1096_u32, sizeof(float));
    std::memcpy(&val_1108, &orig_1108_u32, sizeof(float));

    float scaled_1096 = val_1096 * intensity;
    float scaled_1108 = val_1108 * intensity;

    uint32_t scaled_1096_u32 = 0;
    uint32_t scaled_1108_u32 = 0;
    std::memcpy(&scaled_1096_u32, &scaled_1096, sizeof(float));
    std::memcpy(&scaled_1108_u32, &scaled_1108, sizeof(float));

    REX_STORE_U32(ctx.r3.u32 + 1096, scaled_1096_u32);
    REX_STORE_U32(ctx.r3.u32 + 1108, scaled_1108_u32);

    __imp__sub_821A1530(ctx, base);

    REX_STORE_U32(ctx.r3.u32 + 1096, orig_1096_u32);
    REX_STORE_U32(ctx.r3.u32 + 1108, orig_1108_u32);
    return;
  }
  __imp__sub_821A1530(ctx, base);
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
