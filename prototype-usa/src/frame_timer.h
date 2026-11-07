// Majistic Recomp - engine frame timer header
// Based on ArthurMS15/MajinRecomp

#pragma once

namespace majin {

// The last frame's dt in seconds after the engine's scaling and clamping (and
// majin_dt_smooth), the same value the game stores at +0x150F4; 0 before the
// first frame.
float GameFrameTime();

}  // namespace majin
