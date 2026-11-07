#include "scene_resolution.h"
#include "generated/default/majin_pch.h"

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
  REXLOG_INFO("majin: Scene resolution initialized (majin_scene_scale={}, majin_native_ui={})",
              scene_scale, native_ui);
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
// Guest D3D9 / UI Render Frame Telemetry Hooks
// ============================================================================
// Verified Xbox 360 D3D9 / Engine symbols in majin_recomp:
//   - sub_82475748 : D3DDevice_Present (end-of-frame swap/present)
//   - sub_824684F0 : D3DDevice_Resolve (EDRAM -> texture resolve + clear)
//   - sub_8246C4F0 : D3DDevice_SetViewport (writes D3DVIEWPORT9 to r3 + 12824..12844)
//   - sub_8246BAC0 : D3DDevice_SetScissorRect (clamps scissor against Viewport.X/Y/W/H)
//   - sub_82985580 : CGameProject::RenderUI (2D UI / HUD render entry point)
//
// NOTE:
// Do NOT overwrite r3 + 12824 / r3 + 12828 in sub_8246BAC0 (those are Viewport.X
// and Viewport.Y, not Width/Height; setting them to 1280x720 collapses the
// clamped PA_SC_WINDOW_SCISSOR to a 0x0 rectangle and causes a black screen).
// Do NOT force r4 + 8 / r4 + 12 to 1280x720 in sub_8246C4F0 (breaks shadow maps
// and sub-resolution post-processing passes), and do NOT overwrite f1 in
// sub_824684F0 (corrupts ClearZ during D3DDevice_Resolve).
// ReXGlue's draw_util::GetHostViewportInfo natively scales both 3D viewports
// (vport_xy_scale_enable = true) and 2D screen-space UI passes
// (vport_xy_scale_enable = false) alongside PA_SC_WINDOW_SCISSOR.

REX_EXTERN(__imp__sub_82475748);

// D3DDevice_Present (Frame End)
REX_EXTERN(sub_82475748) {
  static std::atomic<uint32_t> s_present_count{0};
  const uint32_t count = ++s_present_count;
  if (count == 1 || (count % 600) == 0) {
    REXLOG_INFO("majin: D3DDevice_Present frame={} (effective_scale={}x)", count,
                majin::GetEffectiveSceneScale());
  }
  __imp__sub_82475748(ctx, base);
}
