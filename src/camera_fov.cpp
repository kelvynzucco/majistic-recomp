// SPDX-License-Identifier: GPL-3.0-only
// Majistic Recomp - camera field of view multiplier (Issue #25)
// Adapted from ArthurMS15/MajinRecomp

#include <rex/cvar.h>
#include <rex/ppc/context.h>

#include <algorithm>
#include <cmath>

REXCVAR_DEFINE_DOUBLE(majin_fov_scale, 1.0, "Majin",
                      "Field of view multiplier (1 = original)")
    .range(0.5, 2.0);

namespace {

void Scale(PPCRegister& f) {
  const double scale = REXCVAR_GET(majin_fov_scale);
  if (scale != 1.0) {
    // The game reads it back as a single; stay well under 180 degrees.
    f.f64 = double(float(std::min(f.f64 * scale, 150.0)));
  }
}

}  // namespace

void Majin_FovFrustumA(PPCRegister& f0) { Scale(f0); }
void Majin_FovFrustumB(PPCRegister& f12) { Scale(f12); }
void Majin_FovProjection(PPCRegister& f13) { Scale(f13); }

void Majin_ClampTanHalfHfov(PPCRegister& f) {
  if (!std::isfinite(f.f64)) {
    f.f64 = 1.0;
  } else {
    // Prevent extreme tan(HFOV/2) values on ultrawide displays at high FOV.
    f.f64 = double(float(std::clamp(f.f64, -10.0, 10.0)));
  }
}

void Majin_ActorConeCullThreshold(PPCRegister& f0) {
  const double scale = REXCVAR_GET(majin_fov_scale);
  if (scale > 1.0) {
    // Original threshold in .rdata is cos(45 deg) = ~0.7071 for a 90 deg view cone.
    // When FOV is expanded, widen the cone up to 170 deg (85 deg half-angle)
    // so actors and props on screen edges are not put into SLEEP/INACTIVE state.
    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
    const double scaled_half_angle_deg = std::min(45.0 * scale, 85.0);
    f0.f64 = double(float(std::cos(scaled_half_angle_deg * kDegToRad)));
  }
}
