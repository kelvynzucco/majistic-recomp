#pragma once

#include <cstdint>

#include <rex/cvar.h>

REXCVAR_DECLARE(int32_t, majin_scene_scale);
REXCVAR_DECLARE(bool, majin_native_ui);

namespace majin {

// Initialize scene resolution scaling CVar defaults from draw_resolution_scale
// if majin_scene_scale is set to 0 (auto).
void InitSceneResolution();

// Returns the effective 3D scene resolution scale (1, 2, ...).
int32_t GetEffectiveSceneScale();

// Returns true when native-resolution UI rendering is active and the current
// draw scale is > 1.
bool IsNativeUiActive();

}  // namespace majin
