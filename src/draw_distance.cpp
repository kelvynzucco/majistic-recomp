// SPDX-License-Identifier: GPL-3.0-only
// Majistic Recomp - Dynamic Draw Distance & Far Plane Multiplier (Issue #25)

#include <rex/cvar.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

REXCVAR_DEFINE_DOUBLE(
    majin_draw_distance_scale, 1.0, "Majin",
    "Draw distance & far clip plane multiplier (1.0 = original)")
    .range(0.5, 3.0);

namespace {

uint32_t LoadU32(uint32_t address) {
  if (address < 0x10000) return 0;
  uint32_t v = 0;
  std::memcpy(&v, REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(address), 4);
  return std::byteswap(v);
}

// Inspects a 608-byte CMaterial descriptor (sub_82340768 / sub_829AC6D0 / sub_829AC7D0):
//   +296 (uint32_t Big-Endian): blend_mode
//     0 = Opaque
//     1 = Alpha-Test Cutout (foliage leaves, grass, bushes)
//     2 = Translucent Alpha-Blend (SRCALPHA / INVSRCALPHA)
//     3 = Alpha-Test Dither / Foliage Fade
//     4 = Additive Blend (SRCALPHA / ONE, BLENDOP_ADD - magic barriers, energy fields, auras)
//     5 = Reverse-Subtract Blend (BLENDOP_REVSUBTRACT - dark corruption barriers/auras)
bool IsTranslucentOrEffectMaterial(uint32_t mat_addr) {
  if (mat_addr < 0x10000) return false;
  const uint32_t blend_mode = LoadU32(mat_addr + 296);
  if (blend_mode >= 2 && blend_mode <= 5) {
    return true;
  }
  return false;
}

}  // namespace

void Majin_DrawDistanceScale(PPCRegister& f) {
  const double scale = REXCVAR_GET(majin_draw_distance_scale);
  if (scale != 1.0 && std::isfinite(f.f64) && f.f64 > 0.0) {
    // Clamp multiplier defensively and round back to IEEE-754 single precision
    // to match the game's native lfs/fcmpu math.
    const double clamped_scale = std::clamp(scale, 0.5, 3.0);
    f.f64 = double(float(f.f64 * clamped_scale));
  }
}

void Majin_LodDistanceScale(PPCRegister& f, PPCRegister& mesh_res) {
  // Intentionally no-op:
  // In Majin's engine, submeshes declare an allowed LOD visibility bitmask at +52:
  //   Bit 0 (0x1) = Near LOD
  //   Bit 1 (0x2) = Mid LOD
  //   Bit 2 (0x4) = Far LOD
  // Submeshes that only exist in Mid or Far LOD (such as distance visual effects,
  // magical barriers, energy fields, and portals) have Bit 0 clear.
  // Artificially dividing the Euclidean distance (f) in sub_829AD170 / sub_829AE860
  // causes the engine to select Near LOD (1) for distant objects. When evaluated
  // in sub_82344580 and sub_829AE0D8, (submesh_mask & lod_level) evaluates to
  // (submesh_mask & 1) == 0, which culls the submesh completely and erases the effect.
  // Extended draw distance is fully achieved via CPU culling (sub_8233DE98),
  // foliage shader fade (sub_8218B600), and camera far plane projection without
  // tampering with mesh LOD selection.
  (void)f;
  (void)mesh_res;
}

void Majin_FoliageFadeScale(PPCRegister& f, PPCRegister& mat, PPCRegister& model) {
  const double scale = REXCVAR_GET(majin_draw_distance_scale);
  if (scale != 1.0 && std::isfinite(f.f64) && f.f64 > 0.0) {
    // Skip translucent/additive/subtractive materials and forced-translucent models (+544)
    // so near-camera proximity fade on barriers/auras is never scaled.
    if (IsTranslucentOrEffectMaterial(mat.u32) ||
        (model.u32 >= 0x10000 && LoadU32(model.u32 + 544) != 0)) {
      return;
    }
    const double clamped_scale = std::clamp(scale, 0.5, 3.0);
    f.f64 = double(float(f.f64 * clamped_scale));
  }
}
