// Majistic Recomp - Unified Scene Resolution & D3D9 Present Telemetry
#include "scene_resolution.h"
#include "generated/default/majin_pch.h"
#include "edition_offsets.h"

#include <atomic>
#include <cstdlib>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/function.h>

REXCVAR_DEFINE_INT32(majin_scene_scale, 0, "Majin/Graphics",
                     "3D scene resolution scale override (0 = auto/use draw_resolution_scale, "
                     "1 = 720p, 2 = 1440p)")
    .range(0, 8)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(majin_native_ui, true, "Majin/Graphics",
                    "Keep 2D UI and HUD passes at native 1280x720 coordinate space while scaling 3D")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace majin {

void InitSceneResolution() {
  const int32_t scene_scale = REXCVAR_GET(majin_scene_scale);
  const bool native_ui = REXCVAR_GET(majin_native_ui);
  REXLOG_INFO("majin: Scene resolution initialized (majin_scene_scale={}, majin_native_ui={}, edition={})",
              scene_scale, native_ui, MAJIN_EDITION_NAME);
}

int32_t GetEffectiveSceneScale() {
  const int32_t scene_scale = REXCVAR_GET(majin_scene_scale);
  if (scene_scale > 0) {
    return scene_scale;
  }
  const std::string draw_scale_str = rex::cvar::GetFlagByName("draw_resolution_scale_x");
  if (!draw_scale_str.empty()) {
    const int32_t draw_scale = std::atoi(draw_scale_str.c_str());
    if (draw_scale > 0) {
      return draw_scale;
    }
  }
  return 1;
}

bool IsNativeUiActive() {
  return REXCVAR_GET(majin_native_ui) && GetEffectiveSceneScale() > 1;
}

}  // namespace majin

// ============================================================================
// D3DDevice_Present Hook (PAL: sub_824757B8 / USA: sub_82475748)
// ============================================================================
REX_EXTERN(MAJIN_IMP_D3D_PRESENT);

REX_EXTERN(MAJIN_FN_D3D_PRESENT) {
  static std::atomic<uint32_t> s_present_count{0};
  const uint32_t count = ++s_present_count;
  if (count == 1 || (count % 600) == 0) {
    REXLOG_INFO("majin: D3DDevice_Present frame={} (effective_scale={}x, {})", count,
                majin::GetEffectiveSceneScale(), MAJIN_EDITION_NAME);
  }
  MAJIN_IMP_D3D_PRESENT(ctx, base);
}
