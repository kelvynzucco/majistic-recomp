// SPDX-License-Identifier: GPL-3.0-only
// Majistic Recomp - Dynamic Draw Distance Multiplier (Issue #25)

#include "generated/default/majin_pch.h"
#include "edition_offsets.h"

#include <rex/cvar.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

REXCVAR_DEFINE_DOUBLE(
    majin_draw_distance_scale, 1.0, "Majin",
    "Draw distance multiplier (1.0 = original)")
    .range(0.5, 3.0);

namespace {

inline bool ValidGuestPtr(uint32_t addr) {
  return addr >= 0x10000u && addr < 0xF0000000u;
}

inline double GetClampedDrawDistanceScale() {
  const double scale = REXCVAR_GET(majin_draw_distance_scale);
  if (!std::isfinite(scale)) return 1.0;
  return std::clamp(scale, 0.5, 3.0);
}

// Quadratic curve for instanced vegetation (mapXXX_ins):
//   0.5x -> 0.25x (clearly trims near/mid foliage even on 70m-80m maps like map107)
//   1.0x -> 1.00x (exact vanilla)
//   2.0x -> 4.00x (20m grass in map409 reaches 80m)
//   3.0x -> 9.00x (15m-20m foliage in map407/map409 reaches 135m-180m, eliminating pop-in)
inline double GetFoliageDrawDistanceScale() {
  const double scale = GetClampedDrawDistanceScale();
  return scale * scale;
}

uint32_t LoadU32(uint32_t address) {
  if (!ValidGuestPtr(address)) return 0;
  uint32_t v = 0;
  std::memcpy(&v, REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(address), 4);
  return std::byteswap(v);
}

float LoadF32(uint32_t address) {
  return std::bit_cast<float>(LoadU32(address));
}

void StoreF32(uint32_t address, float value) {
  if (!ValidGuestPtr(address)) return;
  const uint32_t be = std::byteswap(std::bit_cast<uint32_t>(value));
  std::memcpy(REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(address), &be, 4);
}

// Inspects a 608-byte CMaterial descriptor (sub_82340768 / sub_829AC6D0 / sub_829AC7D0):
//   +296 (uint32_t Big-Endian): blend_mode
//     0 = Opaque
//     1 = Alpha-Test Cutout (foliage leaves, grass, bushes)
//     2 = Translucent Alpha-Blend (SRCALPHA / INVSRCALPHA)
//     3 = Alpha-Test Dither / Foliage Fade
//     4 = Additive Blend (SRCALPHA / ONE, BLENDOP_ADD - magic barriers, energy fields, auras)
//     5 = Reverse-Subtract Blend (BLENDOP_REVSUBTRACT - dark corruption barriers/auras)
// Keep modes 2, 4, 5 protected for Issue #25 magic barriers/auras while allowing mode 3
// (Alpha-Test Dither / Foliage Fade) so instanced foliage actually scales its GPU fade.
bool IsTranslucentOrEffectMaterial(uint32_t mat_addr) {
  if (!ValidGuestPtr(mat_addr)) return false;
  const uint32_t blend_mode = LoadU32(mat_addr + 296);
  return blend_mode == 2 || blend_mode == 4 || blend_mode == 5;
}

// Open-world adjacent-zone streaming upgrade state:
// SysKbMap has 4 map slots (stride 10768 at SysKbMap+48). When draw distance > 1.0x,
// KB/data/grid/world neighbor commands requesting L0-only (mask = 2, _1.pak) are promoted
// to FULL+L0 (mask = 3, _0.pak + _1.pak) so distant visible zones display high-detail meshes,
// HD textures, and mapXXX_ins vegetation immediately instead of bare L0 shells.
// Promoted slots are tracked so sub_82379160 binds their FULL mesh and mapXXX_ins visuals
// without prematurely spawning enemies/gimmicks/events (sub_82375B20) or _0.pak triggers
// until the player actually enters a native mask-3 border box.
struct SlotStreamState {
  uint32_t slot_addr = 0;
  int32_t map_id = -1;
  bool promoted_full_only = false;
};

constexpr size_t kMaxMapSlots = 8;
std::mutex g_stream_mutex;
std::array<SlotStreamState, kMaxMapSlots> g_slot_states{};
std::atomic<bool> g_last_stream_upgrade_enabled{false};
thread_local bool g_in_world_box_apply = false;

bool MapSupportsFullUpgrade(int32_t map_id) {
  if (map_id <= 0) return false;
  // map501 and map508 only ship _1.pak (no _0.pak); map601 only ships _0.pak (no _1.pak);
  // map605 is the dynamic castle/boss slot with custom offset/rotation rules.
  if (map_id == 501 || map_id == 508 || map_id == 601 || map_id == 605) {
    return false;
  }
  return true;
}

void SetSlotPromotedState(uint32_t slot_addr, int32_t map_id, bool promoted) {
  std::lock_guard<std::mutex> lock(g_stream_mutex);
  SlotStreamState* free_entry = nullptr;
  for (auto& entry : g_slot_states) {
    if (entry.slot_addr == slot_addr) {
      entry.map_id = map_id;
      entry.promoted_full_only = promoted;
      return;
    }
    if (entry.slot_addr == 0 && !free_entry) {
      free_entry = &entry;
    }
  }
  if (free_entry) {
    free_entry->slot_addr = slot_addr;
    free_entry->map_id = map_id;
    free_entry->promoted_full_only = promoted;
  }
}

bool IsSlotPromotedFullOnly(uint32_t slot_addr, int32_t map_id) {
  std::lock_guard<std::mutex> lock(g_stream_mutex);
  for (const auto& entry : g_slot_states) {
    if (entry.slot_addr == slot_addr) {
      return entry.promoted_full_only && entry.map_id == map_id;
    }
  }
  return false;
}

}  // namespace

// Character & Gimmick CPU distance culling thresholds in sub_8233DE98 (PAL) / sub_8233DE28 (USA)
// (model+644 == 0: 636(r31) per-object and 120(r11) global 60m threshold)
void Majin_DrawDistanceScale(PPCRegister& f) {
  const double scale = GetClampedDrawDistanceScale();
  if (scale != 1.0 && std::isfinite(f.f64) && f.f64 > 0.0) {
    // Round back to IEEE-754 single precision to match the game's native lfs/fcmpu math.
    f.f64 = double(float(f.f64 * scale));
  }
}

// Instanced vegetation (mapXXX_ins, model+644 != 0) CPU distance culling in sub_8233DE98 / sub_8233DE28
// Uses the same quadratic scale curve as Majin_FoliageFadeScale so CPU cull and GPU fade stay synced.
void Majin_InstanceDrawDistanceScale(PPCRegister& f) {
  const double scale = GetFoliageDrawDistanceScale();
  if (scale != 1.0 && std::isfinite(f.f64) && f.f64 > 0.0) {
    f.f64 = double(float(f.f64 * scale));
  }
}

// Foliage / Bush / Grass GPU shader fade-out threshold (636(r28) -> constant 3164(r25)) in sub_8218B600
void Majin_FoliageFadeScale(PPCRegister& f, PPCRegister& mat, PPCRegister& model) {
  const double scale = GetFoliageDrawDistanceScale();
  if (scale != 1.0 && std::isfinite(f.f64) && f.f64 > 0.0) {
    // Skip translucent/additive/subtractive materials and forced-translucent models (+544)
    // so near-camera proximity fade on barriers/auras is never scaled.
    if (IsTranslucentOrEffectMaterial(mat.u32) ||
        (ValidGuestPtr(model.u32) && LoadU32(model.u32 + 544) != 0)) {
      return;
    }
    f.f64 = double(float(f.f64 * scale));
  }
}

// Instanced vegetation with isCullByDistance == 0 (628(r28) == 0, e.g. 114/177 plants in map107
// and 126/239 in map111) hits loc_8218B910 in sub_8218B600, uploading infinite clip values.
// When scale < 1.0x, apply a smooth GPU shader fade using a 70m virtual base distance (matching
// the culled plants in map107/map111 -> 17.5m at 0.5x) while leaving >= 1.0x untouched.
void Majin_FoliageCullOffFade(PPCRegister& ins_clip_vec,
                              PPCRegister& mat,
                              PPCRegister& model,
                              PPCRegister& render_cfg) {
  const double raw_scale = GetClampedDrawDistanceScale();
  if (raw_scale >= 1.0) return;
  if (!ValidGuestPtr(ins_clip_vec.u32) || !ValidGuestPtr(render_cfg.u32)) return;
  if (IsTranslucentOrEffectMaterial(mat.u32) ||
      (ValidGuestPtr(model.u32) && LoadU32(model.u32 + 544) != 0)) {
    return;
  }

  const float fade_start_mul = LoadF32(render_cfg.u32 + 492);
  const float fade_end_mul = LoadF32(render_cfg.u32 + 1252);
  if (!std::isfinite(fade_start_mul) || !std::isfinite(fade_end_mul) ||
      fade_start_mul <= 0.0f || fade_end_mul <= 0.0f) {
    return;
  }

  constexpr double kVirtualCullOffBaseDist = 7000.0;
  const double dist = kVirtualCullOffBaseDist * (raw_scale * raw_scale);
  StoreF32(ins_clip_vec.u32 + 0, static_cast<float>(dist * fade_start_mul));
  StoreF32(ins_clip_vec.u32 + 4, static_cast<float>(dist * fade_end_mul));
}

// Per-zone lod%03d model swapper: sub_822517D0 (PAL) / sub_82251760 (USA)
// TaskMap+2780 holds the lod%03d OBB state evaluated in sub_8237A3D0/sub_8237A440:
//   0 = Full map (+880) with fallback to L0 (+884)
//   1 = Force low-poly L0 (+884)
//   2 = Force NULL (completely hide the loaded zone)
// When draw distance > 1.0x, temporarily override non-zero states to 0 so loaded adjacent
// zones are never hidden or trapped in L0 until the player is right on top of them.
REX_EXTERN(MAJIN_IMP_TASKMAP_UPDATE_LOD);
REX_EXTERN(MAJIN_FN_TASKMAP_UPDATE_LOD) {
  const double scale = GetClampedDrawDistanceScale();
  const uint32_t task_map = ctx.r4.u32;
  if (scale > 1.0 && ValidGuestPtr(task_map)) {
    const uint32_t orig_lod_state = REX_LOAD_U32(task_map + 2780);
    if (orig_lod_state != 0u) {
      REX_STORE_U32(task_map + 2780, 0u);
      MAJIN_IMP_TASKMAP_UPDATE_LOD(ctx, base);
      REX_STORE_U32(task_map + 2780, orig_lod_state);
      return;
    }
  }
  MAJIN_IMP_TASKMAP_UPDATE_LOD(ctx, base);
}

// Per-frame World Grid PVS update: sub_82175700 (identical in PAL and USA)
// Called from sub_821720B0 every frame with r3 = SysKbMap*.
// 1. Sets SysKbMap+47296 = 1 when scale > 1.0x to bypass the 100m cell PVS.
// 2. When scale crosses 1.0x live, clears SysKbMap+43144 (last active WorldBox*) so
//    sub_821720B0 immediately re-runs sub_82173FF8 on this frame without needing to move.
REX_EXTERN(MAJIN_IMP_WORLD_PVS_UPDATE);
REX_EXTERN(MAJIN_FN_WORLD_PVS_UPDATE) {
  const uint32_t sys_map = ctx.r3.u32;
  MAJIN_IMP_WORLD_PVS_UPDATE(ctx, base);
  if (ValidGuestPtr(sys_map)) {
    const bool want_upgrade = (GetClampedDrawDistanceScale() > 1.0);
    if (want_upgrade) {
      REX_STORE_U32(sys_map + 47296, 1u);
    }
    const bool prev_upgrade =
        g_last_stream_upgrade_enabled.exchange(want_upgrade, std::memory_order_relaxed);
    if (prev_upgrade != want_upgrade && REX_LOAD_U32(sys_map + 43144) != 0u) {
      REX_STORE_U32(sys_map + 43144, 0u);
    }
  }
}

// World streaming box applier: sub_82173FF8 (identical in PAL and USA)
// Scopes g_in_world_box_apply so MapSlot unload/load promotion only applies to world-grid
// box updates (never manual shutdown/teardown calls).
REX_EXTERN(MAJIN_IMP_WORLD_BOX_APPLY);
REX_EXTERN(MAJIN_FN_WORLD_BOX_APPLY) {
  g_last_stream_upgrade_enabled.store(GetClampedDrawDistanceScale() > 1.0,
                                      std::memory_order_relaxed);
  const bool prev_in_apply = g_in_world_box_apply;
  g_in_world_box_apply = true;
  MAJIN_IMP_WORLD_BOX_APPLY(ctx, base);
  g_in_world_box_apply = prev_in_apply;
}

// MapSlot pack unload: sub_82378A78 (PAL) / sub_82378A08 (USA)
// Called from sub_82173FF8 with r3 = MapSlot*, r4 = ~(immediateMask | asyncMask).
// Records whether each slot natively requested FULL (_0.pak, bit 0) vs L0-only (_1.pak, bit 1),
// and preserves bit 0 when promoting an eligible L0-only neighbor at scale > 1.0x.
REX_EXTERN(MAJIN_IMP_MAPSLOT_UNLOAD);
REX_EXTERN(MAJIN_FN_MAPSLOT_UNLOAD) {
  const uint32_t slot = ctx.r3.u32;
  if (g_in_world_box_apply && ValidGuestPtr(slot)) {
    const int32_t map_id = static_cast<int32_t>(REX_LOAD_U32(slot + 1564));
    const uint32_t unload_mask = ctx.r4.u32;
    const uint32_t native_mask = ~unload_mask;
    const bool native_has_full = (native_mask & 1u) != 0u;
    const bool native_has_l0 = (native_mask & 2u) != 0u;
    const bool promote = (GetClampedDrawDistanceScale() > 1.0) &&
                         !native_has_full && native_has_l0 &&
                         MapSupportsFullUpgrade(map_id);
    SetSlotPromotedState(slot, map_id, promote);
    if (promote) {
      ctx.r4.u64 = static_cast<uint64_t>(unload_mask & ~1u);
    }
  } else if (ValidGuestPtr(slot)) {
    SetSlotPromotedState(slot, -1, false);
  }
  MAJIN_IMP_MAPSLOT_UNLOAD(ctx, base);
}

// MapSlot pack load: sub_823788C0 (PAL) / sub_82378850 (USA)
// Called from sub_82173FF8 with r3 = MapSlot*, r4 = load_mask, r5 = is_async_streaming.
// When a neighbor slot was marked promoted_full_only, upgrade mask 2 -> 3 to stream _0.pak
// (FULL mesh + HD textures + mapXXX_ins vegetation).
REX_EXTERN(MAJIN_IMP_MAPSLOT_LOAD);
REX_EXTERN(MAJIN_FN_MAPSLOT_LOAD) {
  const uint32_t slot = ctx.r3.u32;
  if (g_in_world_box_apply && ValidGuestPtr(slot)) {
    const int32_t map_id = static_cast<int32_t>(REX_LOAD_U32(slot + 1564));
    const uint32_t load_mask = ctx.r4.u32;
    if ((load_mask & 2u) != 0u && (load_mask & 1u) == 0u &&
        IsSlotPromotedFullOnly(slot, map_id)) {
      ctx.r4.u64 = static_cast<uint64_t>(load_mask | 1u);
    }
  }
  MAJIN_IMP_MAPSLOT_LOAD(ctx, base);
}

// MapSlot per-frame resource binder: sub_82379160 (PAL) / sub_823790F0 (USA)
// In sub_82379160, all visual resources (FULL mesh r4=10, L0 mesh r4=11, mapXXX_ins r4=17)
// bind via sub_8237AA30 without reading MapSlot+10536. Only two gameplay blocks read +10536 == 3:
//   1. loc_82379528: placer/enemy/gimmick/event spawn (sub_82375B20) / despawn (sub_82375F28)
//   2. loc_82379620: _0.pak sound/battle/event script activation (sub_8237B9E8)
// For promoted visual-only neighbor slots, temporarily mask +10536 (3 -> 1) across this call
// so FULL geometry and vegetation display at distance while enemies/events stay strictly vanilla.
REX_EXTERN(MAJIN_IMP_MAPSLOT_APPLY_RES);
REX_EXTERN(MAJIN_FN_MAPSLOT_APPLY_RES) {
  const uint32_t slot = ctx.r3.u32;
  if (ValidGuestPtr(slot)) {
    const int32_t map_id = static_cast<int32_t>(REX_LOAD_U32(slot + 1564));
    if (map_id != 605 && IsSlotPromotedFullOnly(slot, map_id)) {
      const uint32_t orig_state = REX_LOAD_U32(slot + 10536);
      if (orig_state == 3u) {
        REX_STORE_U32(slot + 10536, 1u);
        MAJIN_IMP_MAPSLOT_APPLY_RES(ctx, base);
        REX_STORE_U32(slot + 10536, orig_state);
        return;
      }
    }
  }
  MAJIN_IMP_MAPSLOT_APPLY_RES(ctx, base);
}

// Shadow cascade split update: sub_8218DD60 (identical in PAL and USA)
// Scales gameplay shadow far (env+3484, default 4000.0 = 40m) smoothly across [0.5x..2.0x]
// (20m..80m) while leaving cutscene demoShadowFar (env+3476, 15m) untouched.
// For shadow_scale > 1.0, gently increases PSSM lambda (env+3972, default 0.50 -> up to 0.65)
// so cascade 0 stays sharp near the player while cascade 2 reaches further.
REX_EXTERN(MAJIN_IMP_SHADOW_CASCADE_UPDATE);
REX_EXTERN(MAJIN_FN_SHADOW_CASCADE_UPDATE) {
  const double scale = GetClampedDrawDistanceScale();
  const uint32_t env = ctx.r3.u32;
  if (scale != 1.0 && ValidGuestPtr(env)) {
    const uint32_t orig_far_bits = REX_LOAD_U32(env + 3484);
    const float orig_far = std::bit_cast<float>(orig_far_bits);
    if (std::isfinite(orig_far) && orig_far > 0.0f) {
      const double shadow_scale =
          (scale < 1.0) ? scale : (1.0 + (scale - 1.0) * 0.5);
      const float scaled_far = static_cast<float>(static_cast<double>(orig_far) * shadow_scale);
      REX_STORE_U32(env + 3484, std::bit_cast<uint32_t>(scaled_far));

      uint32_t orig_lambda_bits = 0;
      bool modified_lambda = false;
      if (shadow_scale > 1.0) {
        orig_lambda_bits = REX_LOAD_U32(env + 3972);
        const float orig_lambda = std::bit_cast<float>(orig_lambda_bits);
        if (std::isfinite(orig_lambda) && orig_lambda >= 0.0f && orig_lambda < 0.80f) {
          const float boosted_lambda = static_cast<float>(
              std::min(0.80, static_cast<double>(orig_lambda) + 0.15 * (shadow_scale - 1.0)));
          REX_STORE_U32(env + 3972, std::bit_cast<uint32_t>(boosted_lambda));
          modified_lambda = true;
        }
      }

      MAJIN_IMP_SHADOW_CASCADE_UPDATE(ctx, base);

      REX_STORE_U32(env + 3484, orig_far_bits);
      if (modified_lambda) {
        REX_STORE_U32(env + 3972, orig_lambda_bits);
      }
      return;
    }
  }
  MAJIN_IMP_SHADOW_CASCADE_UPDATE(ctx, base);
}
