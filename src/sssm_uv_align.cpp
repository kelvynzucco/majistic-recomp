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

REXCVAR_DEFINE_BOOL(
    majin_shadow_texel_snap, true, "Majin/Graphics",
    "Snap orthographic shadow cascade XY frustum bounds to host shadow-map texel increments to "
    "eliminate shadow edge shimmering during camera movement")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_STRING(
    majin_shadow_sssm_mode, "auto", "Majin/Graphics",
    "Screen-space shadow blur (SSSM) mode: auto (off for 4x4/6x6 PCF, 0.5x for 2x2 PCF, full "
    "for original), original (1.0x), subtle (0.5x), or off (0.0x pure 3D PCF)")
    .allowed({"auto", "original", "subtle", "off"})
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

uint32_t GetActiveShadowScale() {
  const int32_t ss = QueryIntFlag("majin_shadow_scale", 0);
  if (ss >= 4) return 4u;
  if (ss >= 2) return 2u;
  if (ss >= 1) return 1u;
  uint32_t sx = 1, sy = 1;
  GetActiveDrawResolutionScale(sx, sy);
  return std::clamp(sx, 1u, 4u);
}

float GetActiveSssmBlurMultiplier() {
  std::string mode = REXCVAR_GET(majin_shadow_sssm_mode);
  std::transform(mode.begin(), mode.end(), mode.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  if (mode == "original" || mode == "full" || mode == "on") {
    return 1.0f;
  }
  if (mode == "subtle" || mode == "half" || mode == "reduced") {
    return 0.5f;
  }
  if (mode == "off" || mode == "none" || mode == "0") {
    return 0.0f;
  }
  // "auto": coordinate SSSM screen-space blur with 3D shadow PCF mode so flat floors and
  // steep stone walls have consistent softness without double-blurring or floor/wall mismatch.
  std::string pcf = rex::cvar::GetFlagByName("majin_shadow_pcf_mode");
  std::transform(pcf.begin(), pcf.end(), pcf.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  const std::string backend = rex::cvar::GetFlagByName("gpu_backend");
  if (!backend.empty() && backend != "vulkan") {
    // PCF SPIR-V enhancement runs on Vulkan; preserve full SSSM blur on D3D12.
    return 1.0f;
  }
  if (pcf == "original" || pcf == "native" || pcf == "off" || pcf == "1tap") {
    return 1.0f;
  }
  if (pcf == "bilinear" || pcf == "2x2" || pcf == "sharp") {
    return 0.5f;
  }
  // Default ("soft" 4x4 or "ultra" 6x6): let 3D world-space PCF define shadow softness.
  return 0.0f;
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

// 0x82195CD8 (sub_82195C58): scale GXDS_ShadowSize.zw by 1 / RenderScale and apply SSSM blur mode.
void Majin_SssmShadowSizeScale(PPCRegister& r5) {
  const bool fullres = REXCVAR_GET(majin_sssm_fullres_fix);
  const float blur_mult = GetActiveSssmBlurMultiplier();
  if (!fullres && blur_mult == 1.0f) {
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
  if (fullres) {
    GetActiveDrawResolutionScale(scale_x, scale_y);
  }

  const float new_w = orig_w * static_cast<float>(scale_x);
  const float new_h = orig_h * static_cast<float>(scale_y);
  const float new_inv_w = (orig_inv_w / static_cast<float>(scale_x)) * blur_mult;
  const float new_inv_h = (orig_inv_h / static_cast<float>(scale_y)) * blur_mult;

  StoreGuestFloatBE(buf + 0, new_w);
  StoreGuestFloatBE(buf + 4, new_h);
  StoreGuestFloatBE(buf + 8, new_inv_w);
  StoreGuestFloatBE(buf + 12, new_inv_h);

  static std::atomic<uint32_t> s_logged_size_scale{0};
  const uint32_t mult_code = static_cast<uint32_t>(blur_mult * 100.0f + 0.5f) & 0xFFu;
  const uint32_t packed =
      (static_cast<uint32_t>(orig_w) << 16) | (scale_x << 12) | (scale_y << 8) | mult_code;
  if (s_logged_size_scale.exchange(packed, std::memory_order_relaxed) != packed) {
    REXLOG_INFO(
        "majin_sssm active (ShadowSize): fullres={}, scale={}x{}, sssm_blur_mult={:.2f}, "
        "{:.0f}x{:.0f} -> {:.0f}x{:.0f} (step {:.6f},{:.6f} -> {:.6f},{:.6f})",
        fullres, scale_x, scale_y, blur_mult, orig_w, orig_h, new_w, new_h, orig_inv_w, orig_inv_h,
        new_inv_w, new_inv_h);
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

// 0x82193628 (sub_82193628 entry, right after `mflr r12`, < 0x821C0000: identical in PAL and USA):
// Snap orthographic shadow cascade XY AABB bounds (r4+0..24) to exact world-space host shadow-map
// texel increments so static shadow edges stay locked to world geometry during camera movement.
void Majin_ShadowCascadeTexelSnap(PPCRegister& r1, PPCRegister& r4, PPCRegister& r12) {
  if (!REXCVAR_GET(majin_shadow_texel_snap)) {
    return;
  }

  // Only snap pure orthographic PSSM cascades from sub_8218F0D0 (lr=0x8218F89C).
  // Leave sub_8218FE38's LiSPSM pre-warp (0x82191014) and post-warp (0x82192004) untouched so
  // non-linear perspective warping never jumps between quantized bounds.
  const uint32_t caller = r12.u32;
  if (caller != 0x8218F89Cu) {
    return;
  }

  const uint32_t stack_ptr = r1.u32;
  const uint32_t aabb_addr = r4.u32;
  if (stack_ptr < 0x10000 || aabb_addr < 0x10000) {
    return;
  }

  auto* mem = REX_KERNEL_MEMORY();
  uint8_t* aabb = mem->TranslateVirtual<uint8_t*>(aabb_addr);
  // In sub_8218F0D0, M_lightView is built at r1+208 by sub_82A037A8 (LookAtLH with eye = camera).
  // Row 3 holds world origin in light-view space: (+48 = -dot(eye, X), +52 = -dot(eye, Y), +60 = 1).
  uint8_t* light_view = mem->TranslateVirtual<uint8_t*>(stack_ptr + 208);
  if (!aabb || !light_view) {
    return;
  }

  const float origin_x = LoadGuestFloatBE(light_view + 48);
  const float origin_y = LoadGuestFloatBE(light_view + 52);
  const float m33 = LoadGuestFloatBE(light_view + 60);
  if (!std::isfinite(origin_x) || !std::isfinite(origin_y) || !std::isfinite(m33) ||
      std::fabs(m33 - 1.0f) > 1e-3f) {
    return;
  }

  float min_x = LoadGuestFloatBE(aabb + 0);
  float min_y = LoadGuestFloatBE(aabb + 4);
  float max_x = LoadGuestFloatBE(aabb + 16);
  float max_y = LoadGuestFloatBE(aabb + 20);

  const uint32_t shadow_scale = GetActiveShadowScale();
  const float map_res = 768.0f * static_cast<float>(shadow_scale);

  auto snap_axis = [map_res](float& mn, float& mx, float origin) -> bool {
    const float extent = mx - mn;
    if (!std::isfinite(mn) || !std::isfinite(mx) || extent < 0.05f || extent > 100000.0f) {
      return false;
    }
    // Quantize extent upwards in smooth 1/12-octave (~5.9%) steps with >= 2 texel margin
    // so snapping center by <= 0.5 texel never clips the frustum and texel size stays constant
    // across small camera rotations.
    const float min_extent = extent * (1.0f + 2.0f / map_res);
    const float q_extent = std::exp2(std::ceil(std::log2(min_extent) * 12.0f) / 12.0f);
    const float texel = q_extent / map_res;
    const float center = 0.5f * (mn + mx);
    // Anchor center to world-space light-view origin so camera translation cancels out and
    // static world vertices always land at a constant sub-texel phase.
    const float snapped_center =
        std::floor((center - origin) / texel + 0.5f) * texel + origin;
    mn = snapped_center - 0.5f * q_extent;
    mx = snapped_center + 0.5f * q_extent;
    return true;
  };

  if (!snap_axis(min_x, max_x, origin_x) || !snap_axis(min_y, max_y, origin_y)) {
    return;
  }

  StoreGuestFloatBE(aabb + 0, min_x);
  StoreGuestFloatBE(aabb + 4, min_y);
  StoreGuestFloatBE(aabb + 16, max_x);
  StoreGuestFloatBE(aabb + 20, max_y);

  static std::atomic<uint32_t> s_logged_snap{0};
  const uint32_t bit = 1u | (shadow_scale << 4);
  if (s_logged_snap.exchange(bit, std::memory_order_relaxed) != bit) {
    REXLOG_INFO(
        "majin_shadow_texel_snap active (world-anchored): caller=0x{:08X}, shadow_scale={}x "
        "({:.0f}px grid), light_origin=({:.2f}, {:.2f})",
        caller, shadow_scale, map_res, origin_x, origin_y);
  }
}
