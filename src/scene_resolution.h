#pragma once
#include <cstdint>

namespace majin {

void InitSceneResolution();
int32_t GetEffectiveSceneScale();
int32_t GetEffectiveShadowScale();
bool IsNativeUiActive();

}  // namespace majin
