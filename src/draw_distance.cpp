// SPDX-License-Identifier: GPL-3.0-only
// Majistic Recomp - Dynamic Draw Distance & Far Plane Multiplier (Issue #25)

#include <rex/cvar.h>
#include <rex/ppc/context.h>

#include <algorithm>

REXCVAR_DEFINE_DOUBLE(
    majin_draw_distance_scale, 1.0, "Majin",
    "Draw distance & far clip plane multiplier (1.0 = original)")
    .range(0.5, 3.0);

void Majin_DrawDistanceScale(PPCRegister& f) {
  const double scale = REXCVAR_GET(majin_draw_distance_scale);
  if (scale != 1.0) {
    // Clamp multiplier defensively and round back to IEEE-754 single precision
    // to match the game's native lfs/fcmpu math.
    const double clamped_scale = std::clamp(scale, 0.5, 3.0);
    f.f64 = double(float(f.f64 * clamped_scale));
  }
}

void Majin_ScaleIntDistance(PPCRegister& r) {
  const double scale = REXCVAR_GET(majin_draw_distance_scale);
  if (scale != 1.0) {
    const double clamped = std::clamp(scale, 0.5, 3.0);
    const int32_t val = r.s32;
    if (val > 0) {
      const double scaled = static_cast<double>(val) * clamped;
      r.u64 = static_cast<uint64_t>(static_cast<uint32_t>(
          std::clamp(scaled, 1.0, 100000.0)));
    }
  }
}
