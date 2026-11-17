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

REXCVAR_DEFINE_BOOL(majin_pause_blur, true, "Majin", "Blur the game behind the pause menu");
REXCVAR_DEFINE_BOOL(majin_dof, true, "Majin", "Depth of field (background blur)");
REXCVAR_DEFINE_BOOL(majin_bloom, true, "Majin", "Glare and bloom around bright light");
REXCVAR_DEFINE_BOOL(majin_force_pause_blur, false, "Majin",
                    "Draw the pause screen blur all the time (testing)");
REXCVAR_DEFINE_BOOL(majin_pause_distortion, true, "Graphics",
                    "Screen wave distortion effect behind pause and map menus");

void Majin_PauseBlurEnabled(PPCRegister& r11) {
  if (!REXCVAR_GET(majin_pause_blur)) r11.u64 = 0;
  else if (REXCVAR_GET(majin_force_pause_blur)) r11.u64 = 1;
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
