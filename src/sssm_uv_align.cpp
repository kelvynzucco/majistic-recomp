// SPDX-License-Identifier: GPL-3.0-only
// Majin Recomp - Full-Resolution T_OHKUNI_SSSM Fix (majin_sssm_fullres_fix)
//
// Eliminates the bright silhouette halo around characters while preserving
// smooth, shimmer-free shadows and leaving ShadowScale (1x/2x/4x), RenderScale,
// De-Dither, FXAA, depth bias, and Pre-Z EDRAM depth (base_tiles=720) intact:
//
//   1. Promotes 3452 ("Screen Space Shadow Map") from 640x360 to 1280x720 guest
//      (0x8218DB7C), keeping 3456 ("Shadow Map Temp(Depth)") at half-res.
//   2. Disables MRT1 (3456) in PS#1 (0x82196228) and skips the redundant
//      Resolve(RT1) / SetRenderTarget(1, null) block (0x821963AC -> 0x821963EC),
//      keeping 3452 strictly in EDRAM tiles 0..719 so Pre-Z depth at
//      base_tiles=720..1439 is never overwritten.
//   3. Skips SetTexture(r25, 3456) at the start of PS#2 (0x82196448 -> 0x82196464),
//      so both PS#2 and PS#3 keep sampling the full-res 1280x720 Pre-Z depth
//      texture (0x13238000, k_24_8_FLOAT) already bound to tf1 in PS#1.
//   4. Scales GXDS_ShadowSize.zw by 1 / RenderScale (0x82195CD8) and tightens
//      the vertical depth discontinuity tolerance GXDS_SSSMParam.y (4140(r29))
//      from 100.0 to 2.0 (0x82195ED0), keeping Gaussian sigma at default 2.0.
//   5. Applies the VPOS + (1,1)/(1280,720) subpixel UV compensation exclusively
//      on PS#3's fullscreen triangle (0x821965C4), so PS#1 and PS#2 remain 1:1
//      locked to Pre-Z depth (0.0 px mismatch) and material tf7 reads land on
//      the exact pixel center.
//
// Can be disabled at launch with --majin_sssm_fullres_fix=false to restore the
// original Xbox 360 640x360 SSSM pipeline for A/B comparison.

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

REXCVAR_DEFINE_BOOL(
    majin_sssm_fullres_fix, true, "Majin/Graphics",
    "Promote T_OHKUNI_SSSM (3452) to 1280x720 with Pre-Z depth in tf1, RenderScale-scaled blur "
    "step, c4.y<=2.0 vertical depth containment, and PS#3 UV alignment (false restores original "
    "Xbox 360 640x360 behavior)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace {

std::atomic<bool> s_fullres_3452_allocated{false};

int32_t QueryIntFlag(const char* name, int32_t fallback) {
  const std::string s = rex::cvar::GetFlagByName(name);
  if (s.empty()) {
    return fallback;
  }
  const int v = std::atoi(s.c_str());
  return v > 0 ? v : fallback;
}

// Mirrors TextureCache::GetConfigDrawResolutionScale precedence.
void GetActiveDrawResolutionScale(uint32_t& scale_x_out, uint32_t& scale_y_out) {
  const int32_t shared_scale = QueryIntFlag("resolution_scale", 1);
  const bool use_shared = rex::cvar::HasNonDefaultValue("resolution_scale");
  const int32_t sx = (use_shared && !rex::cvar::HasNonDefaultValue("draw_resolution_scale_x"))
                         ? shared_scale
                         : QueryIntFlag("draw_resolution_scale_x", 1);
  const int32_t sy = (use_shared && !rex::cvar::HasNonDefaultValue("draw_resolution_scale_y"))
                         ? shared_scale
                         : QueryIntFlag("draw_resolution_scale_y", 1);
  scale_x_out = static_cast<uint32_t>(std::clamp(sx, 1, 8));
  scale_y_out = static_cast<uint32_t>(std::clamp(sy, 1, 8));
}

uint32_t LoadGuestU32BE(uint8_t* ptr) {
  uint32_t raw = 0;
  std::memcpy(&raw, ptr, sizeof(raw));
  return std::byteswap(raw);
}

float LoadGuestFloatBE(uint8_t* ptr) {
  return std::bit_cast<float>(LoadGuestU32BE(ptr));
}

void StoreGuestFloatBE(uint8_t* ptr, float value) {
  const uint32_t raw = std::byteswap(std::bit_cast<uint32_t>(value));
  std::memcpy(ptr, &raw, sizeof(raw));
}

bool ReadSssm3452Dimensions(uint32_t r29_addr, uint32_t& width_out, uint32_t& height_out) {
  width_out = 0;
  height_out = 0;
  if (r29_addr < 0x10000) {
    return false;
  }
  uint8_t* mgr = REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(r29_addr);
  if (!mgr) {
    return false;
  }
  const uint32_t rt_addr = LoadGuestU32BE(mgr + 3452);
  if (rt_addr < 0x10000) {
    return false;
  }
  uint8_t* rt = REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(rt_addr);
  if (!rt) {
    return false;
  }
  const uint32_t desc_addr = LoadGuestU32BE(rt + 36);
  if (desc_addr < 0x10000) {
    return false;
  }
  uint8_t* desc = REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(desc_addr);
  if (!desc) {
    return false;
  }
  width_out = LoadGuestU32BE(desc + 32);
  height_out = LoadGuestU32BE(desc + 36);
  return width_out > 0 && height_out > 0;
}

// Guards EDRAM tiles 720..1439 (Pre-Z depth) if 3452 was allocated at 1280x720.
bool ShouldGuardFullResEdram() {
  return REXCVAR_GET(majin_sssm_fullres_fix) ||
         s_fullres_3452_allocated.load(std::memory_order_relaxed);
}

}  // namespace

// 0x8218DB7C (majin_recomp.69.cpp, identical in PAL and USA):
// Promote 3452 ("Screen Space Shadow Map") from 640x360 to 1280x720 guest.
void Majin_SssmPromoteFullRes3452(PPCRegister& r3, PPCRegister& r4) {
  if (!REXCVAR_GET(majin_sssm_fullres_fix)) {
    return;
  }

  const uint32_t orig_w = r3.u32;
  const uint32_t orig_h = r4.u32;
  if (orig_w == 0 || orig_h == 0 || orig_w > 2048 || orig_h > 2048) {
    return;
  }

  const uint32_t new_w = orig_w * 2u;
  const uint32_t new_h = orig_h * 2u;
  r3.u64 = new_w;
  r4.u64 = new_h;
  s_fullres_3452_allocated.store(true, std::memory_order_relaxed);

  REXLOG_INFO(
      "majin_sssm_fullres_fix active (3452 Full-Res allocation): {}x{} -> {}x{} "
      "(3456 kept half-res, MRT1 disabled in PS#1 to preserve Pre-Z depth at base_tiles=720)",
      orig_w, orig_h, new_w, new_h);
}

// 0x82195CD8 (sub_82195C58): scale GXDS_ShadowSize.zw by 1 / RenderScale.
void Majin_SssmShadowSizeScale(PPCRegister& r5) {
  if (!REXCVAR_GET(majin_sssm_fullres_fix)) {
    return;
  }

  const uint32_t buf_addr = r5.u32;
  if (buf_addr < 0x10000) {
    return;
  }

  uint8_t* buf = REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(buf_addr);
  if (!buf) {
    return;
  }

  const float orig_w = LoadGuestFloatBE(buf + 0);
  const float orig_h = LoadGuestFloatBE(buf + 4);
  const float orig_inv_w = LoadGuestFloatBE(buf + 8);
  const float orig_inv_h = LoadGuestFloatBE(buf + 12);
  if (!std::isfinite(orig_w) || !std::isfinite(orig_h) || !std::isfinite(orig_inv_w) ||
      !std::isfinite(orig_inv_h) || orig_w <= 0.0f || orig_h <= 0.0f) {
    return;
  }

  uint32_t scale_x = 1;
  uint32_t scale_y = 1;
  GetActiveDrawResolutionScale(scale_x, scale_y);

  const float new_w = orig_w * static_cast<float>(scale_x);
  const float new_h = orig_h * static_cast<float>(scale_y);
  const float new_inv_w = orig_inv_w / static_cast<float>(scale_x);
  const float new_inv_h = orig_inv_h / static_cast<float>(scale_y);

  StoreGuestFloatBE(buf + 0, new_w);
  StoreGuestFloatBE(buf + 4, new_h);
  StoreGuestFloatBE(buf + 8, new_inv_w);
  StoreGuestFloatBE(buf + 12, new_inv_h);

  static std::atomic<uint32_t> s_logged_size_scale{0};
  const uint32_t packed =
      (static_cast<uint32_t>(orig_w) << 12) | (scale_x << 6) | scale_y;
  if (s_logged_size_scale.exchange(packed, std::memory_order_relaxed) != packed) {
    REXLOG_INFO(
        "majin_sssm_fullres_fix active (ShadowSize): scale={}x{}, {:.0f}x{:.0f} -> {:.0f}x{:.0f} "
        "(step {:.6f},{:.6f} -> {:.6f},{:.6f})",
        scale_x, scale_y, orig_w, orig_h, new_w, new_h, orig_inv_w, orig_inv_h, new_inv_w,
        new_inv_h);
  }
}

// 0x82195ED0 (sub_82195C58): restrict vertical depth tolerance GXDS_SSSMParam.y (100.0 -> 2.0).
void Majin_SssmParamContain(PPCRegister& r5) {
  if (!REXCVAR_GET(majin_sssm_fullres_fix)) {
    return;
  }

  const uint32_t buf_addr = r5.u32;
  if (buf_addr < 0x10000) {
    return;
  }

  uint8_t* buf = REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(buf_addr);
  if (!buf) {
    return;
  }

  const float orig_c4x = LoadGuestFloatBE(buf + 0);
  const float orig_c4y = LoadGuestFloatBE(buf + 4);
  if (!std::isfinite(orig_c4x) || !std::isfinite(orig_c4y)) {
    return;
  }

  constexpr float kMaxVerticalDepthTol = 2.0f;
  const float new_c4y = std::min(orig_c4y, kMaxVerticalDepthTol);
  StoreGuestFloatBE(buf + 4, new_c4y);

  static std::atomic<bool> s_logged_param{false};
  if (!s_logged_param.exchange(true, std::memory_order_relaxed)) {
    REXLOG_INFO(
        "majin_sssm_fullres_fix active (SSSMParam): c4.x={:.3f}, c4.y={:.3f} -> {:.3f} "
        "(sigma preserved at default 2.0)",
        orig_c4x, orig_c4y, new_c4y);
  }
}

// 0x82196228 (sub_82195C58): unbind MRT1 (3456) in PS#1 so Pre-Z depth (base_tiles=720) is safe.
void Majin_SssmDisableMrt1(PPCRegister& r5) {
  if (!ShouldGuardFullResEdram()) {
    return;
  }
  r5.u64 = 0;
}

// 0x821963AC (sub_82195C58): skip Resolve(RT1) + redundant SetRenderTarget(1, null) -> 0x821963EC.
bool Majin_SssmSkipRt1Resolve(PPCRegister& r29) {
  (void)r29;
  return ShouldGuardFullResEdram();
}

// 0x82196448 (sub_82195C58): skip SetTexture(r25, 3456) in PS#2 -> 0x82196464 so tf1 keeps
// Full-Res Pre-Z depth 0x13238000 for both PS#2 and PS#3.
bool Majin_SssmKeepPreZDepthTf1(PPCRegister& r29) {
  if (!ShouldGuardFullResEdram()) {
    return false;
  }

  const bool enabled = REXCVAR_GET(majin_sssm_fullres_fix);
  uint32_t w = 0;
  uint32_t h = 0;
  ReadSssm3452Dimensions(r29.u32, w, h);

  uint32_t scale_x = 1;
  uint32_t scale_y = 1;
  GetActiveDrawResolutionScale(scale_x, scale_y);

  static std::atomic<uint64_t> s_logged_state{0};
  const uint64_t state =
      (static_cast<uint64_t>(w) << 32) | (static_cast<uint64_t>(h) << 16) |
      (static_cast<uint64_t>(scale_x) << 8) | static_cast<uint64_t>(scale_y) |
      (enabled ? (1ULL << 62) : 0ULL) |
      (s_fullres_3452_allocated.load(std::memory_order_relaxed) ? (1ULL << 63) : 0ULL);
  if (s_logged_state.exchange(state, std::memory_order_relaxed) != state) {
    if (enabled && s_fullres_3452_allocated.load(std::memory_order_relaxed)) {
      REXLOG_INFO(
          "majin_sssm_fullres_fix active: 3452={}x{}, RenderScale={}x{}, MRT1 disabled "
          "(Pre-Z depth base_tiles=720 preserved), PS#2/PS#3 reading Pre-Z depth 0x13238000 in tf1",
          w, h, scale_x, scale_y);
    } else if (enabled) {
      REXLOG_WARN(
          "majin_sssm_fullres_fix active (hot-reload; 3452 already allocated as {}x{}): "
          "RenderScale={}x{}, MRT1 disabled, tf1 keeps Pre-Z depth 0x13238000 — restart without "
          "--majin_sssm_fullres_fix=false for 1280x720 allocation",
          w, h, scale_x, scale_y);
    } else {
      REXLOG_WARN(
          "majin_sssm_fullres_fix disabled via hot-reload (keeping MRT1 disabled and tf1=0x13238000 "
          "to protect Pre-Z depth base_tiles=720 while 3452={}x{}; restart with "
          "--majin_sssm_fullres_fix=false for full original 640x360 behavior)",
          w, h);
    }
  }
  return true;
}

// 0x821965C4 (sub_82195C58): apply VPOS subpixel UV alignment to PS#3's 3 vertices in r10.
void Majin_SssmPs3UvAlign(PPCRegister& r10) {
  if (!REXCVAR_GET(majin_sssm_fullres_fix)) {
    return;
  }

  const uint32_t vb_addr = r10.u32;
  if (vb_addr < 0x10000) {
    return;
  }

  uint32_t scale_x = 1;
  uint32_t scale_y = 1;
  GetActiveDrawResolutionScale(scale_x, scale_y);

  const float shift_u =
      (static_cast<float>(scale_x) - 0.5f) / (1280.0f * static_cast<float>(scale_x));
  const float shift_v =
      (static_cast<float>(scale_y) - 0.5f) / (720.0f * static_cast<float>(scale_y));

  uint8_t* vb = REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(vb_addr);
  if (!vb) {
    return;
  }

  constexpr uint32_t kVertexStride = 32;
  constexpr uint32_t kUvUOffset = 12;
  constexpr uint32_t kUvVOffset = 16;

  float u[3];
  float v[3];
  for (uint32_t i = 0; i < 3; ++i) {
    u[i] = LoadGuestFloatBE(vb + i * kVertexStride + kUvUOffset);
    v[i] = LoadGuestFloatBE(vb + i * kVertexStride + kUvVOffset);
    if (!std::isfinite(u[i]) || !std::isfinite(v[i])) {
      return;
    }
  }

  for (uint32_t i = 0; i < 3; ++i) {
    StoreGuestFloatBE(vb + i * kVertexStride + kUvUOffset, u[i] - shift_u);
    StoreGuestFloatBE(vb + i * kVertexStride + kUvVOffset, v[i] - shift_v);
  }

  static std::atomic<uint32_t> s_logged_ps3_uv{0};
  const uint32_t packed = (scale_x << 8) | scale_y;
  if (s_logged_ps3_uv.exchange(packed, std::memory_order_relaxed) != packed) {
    REXLOG_INFO(
        "majin_sssm_fullres_fix active (PS#3 UV align): scale={}x{}, shift_u=-{:.6f}, "
        "shift_v=-{:.6f}",
        scale_x, scale_y, shift_u, shift_v);
  }
}
