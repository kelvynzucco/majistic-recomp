// SPDX-License-Identifier: GPL-3.0-only
// Majistic Recomp - Dynamic Draw Distance & Far Plane Multiplier (Issue #25)

#include <rex/cvar.h>
#include <rex/ppc/context.h>

#include <algorithm>
#include <cmath>

REXCVAR_DEFINE_DOUBLE(
    majin_draw_distance_scale, 1.0, "Majin",
    "Draw distance & far clip plane multiplier (1.0 = original)")
    .range(0.5, 3.0);

void Majin_DrawDistanceScale(PPCRegister& f) {
  const double scale = REXCVAR_GET(majin_draw_distance_scale);
  if (scale != 1.0 && std::isfinite(f.f64) && f.f64 > 0.0) {
    // Clamp multiplier defensively and round back to IEEE-754 single precision
    // to match the game's native lfs/fcmpu math.
    const double clamped_scale = std::clamp(scale, 0.5, 3.0);
    f.f64 = double(float(f.f64 * clamped_scale));
  }
}

void Majin_LodDistanceScale(PPCRegister& f) {
  const double scale = REXCVAR_GET(majin_draw_distance_scale);
  if (scale != 1.0 && scale > 0.0 && std::isfinite(f.f64) && f.f64 > 0.0) {
    // Divide the computed Euclidean distance (f2) before float-to-int conversion
    // (fctidz) in sub_829AD170 / sub_829AD100 so integer LOD thresholds (168(r3)
    // and 172(r3)) scale proportionally in floating point.
    const double clamped_scale = std::clamp(scale, 0.5, 3.0);
    f.f64 = double(float(f.f64 / clamped_scale));
  }
}
