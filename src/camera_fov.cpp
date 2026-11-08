// Majistic Recomp - camera field of view multiplier
// Adapted from ArthurMS15/MajinRecomp
//
// The engine camera keeps its vertical field of view in degrees at +0x268
// (aspect ratio +0x264, near +0x26C, far +0x270). Two functions read it every
// frame: sub_829FC7A0 builds the view frustum used for culling (two loads),
// and sub_829FD3E8 / sub_829FD950 build the projection matrix
// (sub_82A02EA0). --majin_fov_scale scales the value at each of those loads,
// so the picture and the culling widen together; the stored value is left
// alone, so nothing compounds.

#include <rex/cvar.h>
#include <rex/ppc/context.h>

#include <algorithm>

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
