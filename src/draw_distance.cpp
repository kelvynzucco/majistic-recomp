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

uint8_t LoadU8(uint32_t address) {
  if (address < 0x10000) return 0;
  return *REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(address);
}

// Inspects a 608-byte CMaterial descriptor (sub_82340768 / sub_829AC6D0 / sub_829AC7D0):
//   +296 (uint32_t): blend_mode
//     0 = Opaque
//     1 = Alpha-Test Cutout (leaves, bushes, grass, foliage)
//     2 = Translucent Alpha-Blend (SRCALPHA / INVSRCALPHA)
//     3 = Alpha-Test Dither / Foliage Fade
//     4 = Additive Blend (SRCALPHA / ONE, BLENDOP_ADD - magic barriers, energy fields, auras)
//     5 = Reverse-Subtract Blend (BLENDOP_REVSUBTRACT - dark corruption barriers/auras)
//   +300 (uint8_t): material_flags (bit 0 = custom per-material fade range at +308/+312)
bool IsTranslucentOrEffectMaterial(uint32_t mat_addr) {
  if (mat_addr < 0x10000) return false;
  const uint32_t blend_mode = LoadU32(mat_addr + 296);
  if (blend_mode == 2 || blend_mode == 4 || blend_mode == 5) {
    return true;
  }
  const uint8_t mat_flags = LoadU8(mat_addr + 300);
  if ((mat_flags & 0x01) != 0) {
    return true;
  }
  return false;
}

// Inspects a CMeshResource descriptor (r10 in sub_829AD170, r24 in sub_829AE860)
// to preserve native LOD on translucent magic barriers, glowing auras, and effects.
bool IsTranslucentOrEffectMesh(uint32_t mesh_res_addr) {
  if (mesh_res_addr < 0x10000) return false;

  // 1. Check material table (144(mesh_res) = CMaterial*, 148(mesh_res) = count, stride = 608)
  const uint32_t mat_array = LoadU32(mesh_res_addr + 144);
  const uint32_t mat_count = LoadU32(mesh_res_addr + 148);
  if (mat_array >= 0x10000 && mat_count > 0 && mat_count <= 256) {
    for (uint32_t i = 0; i < mat_count; ++i) {
      if (IsTranslucentOrEffectMaterial(mat_array + i * 608)) {
        return true;
      }
    }
  }

  // 2. Check submesh table (136(mesh_res) = CSubmesh*, 140(mesh_res) = count, stride = 72)
  // Submesh +52 holds the LOD visibility bitmask (1 = Near, 2 = Mid, 4 = Far).
  // If a mesh only has Mid/Far submeshes and no Near-LOD (bit 0) submesh,
  // forcing Near LOD (1) would cull it completely.
  const uint32_t submesh_array = LoadU32(mesh_res_addr + 136);
  const uint32_t submesh_count = LoadU32(mesh_res_addr + 140);
  if (submesh_array >= 0x10000 && submesh_count > 0 && submesh_count <= 512) {
    bool has_near_lod = false;
    bool has_mid_or_far_lod = false;
    for (uint32_t i = 0; i < submesh_count; ++i) {
      const uint32_t lod_mask = LoadU32(submesh_array + i * 72 + 52);
      if ((lod_mask & 0x1) != 0) has_near_lod = true;
      if ((lod_mask & 0x6) != 0) has_mid_or_far_lod = true;
    }
    if (has_mid_or_far_lod && !has_near_lod) {
      return true;
    }
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
  const double scale = REXCVAR_GET(majin_draw_distance_scale);
  if (scale != 1.0 && scale > 0.0 && std::isfinite(f.f64) && f.f64 > 0.0) {
    if (IsTranslucentOrEffectMesh(mesh_res.u32)) {
      return;
    }
    // Divide the computed Euclidean distance before float-to-int conversion
    // (fctidz) in sub_829AD170 and sub_829AE860 so integer LOD thresholds
    // (168(mesh_res) and 172(mesh_res)) scale proportionally in floating point.
    const double clamped_scale = std::clamp(scale, 0.5, 3.0);
    f.f64 = double(float(f.f64 / clamped_scale));
  }
}

void Majin_FoliageFadeScale(PPCRegister& f, PPCRegister& mat, PPCRegister& model) {
  const double scale = REXCVAR_GET(majin_draw_distance_scale);
  if (scale != 1.0 && std::isfinite(f.f64) && f.f64 > 0.0) {
    // Skip translucent/additive/subtractive materials and forced-translucent models (+544)
    // so near-camera proximity fade on barriers/auras is never inverted.
    if (IsTranslucentOrEffectMaterial(mat.u32) ||
        (model.u32 >= 0x10000 && LoadU32(model.u32 + 544) != 0)) {
      return;
    }
    const double clamped_scale = std::clamp(scale, 0.5, 3.0);
    f.f64 = double(float(f.f64 * clamped_scale));
  }
}
