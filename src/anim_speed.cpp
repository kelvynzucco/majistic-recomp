// Majistic Recomp - partial animations at their speed above 30 fps
// Adapted from ArthurMS15/MajinRecomp
//
// A skeleton has two sub-motion slots (pointers at +244 and +248) for partial
// animations played over the body's motion: Majin's idle face, Tepeu's
// gestures when giving Majin orders, Majin healing Tepeu. sub_822BBEB8 updates
// them every frame: it adds the slot's speed (slot+60, in 30 fps frames) to its
// time (slot+36) and samples the curves there (sub_82289EC8 stores the new
// time). The step is not scaled by the frame time, unlike the body's motion
// (obj+0x10 * speed), so these animations ran at frame rate / 30 times their
// speed: four times as fast at 120 fps.
//
// The hooks at 0x822BC1A4 and 0x822BC23C (both branches, just before the add)
// scale the speed by the frame's dt in 30 fps frames.
//
// Some world objects have the same problem with their main motion: the update
// at sub_822421C8 steps the motion player with sub_822BCE90 (time += speed,
// +264 += speed) at the speed set when the motion started, while characters
// set that speed to obj+0x5D0 * obj+0x10 every frame first (sub_82344F98).
// The hook before the call (0x822421E4) records the object's dt in 30 fps
// frames (obj+0x10), and the hooks in sub_822BCE90 scale the speed only for
// that call.

#include "frame_timer.h"

#include <rex/cvar.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include <bit>
#include <cstdint>
#include <cstring>

REXCVAR_DEFINE_BOOL(majin_submotion_fix, false, "Majin",
                    "Play partial animations (Majin's face, Tepeu's gestures, healing) and some "
                    "world objects' animations at their speed above 30 fps");

void Majin_SubMotionStep(PPCRegister& speed) {
  const float dt = majin::GameFrameTime();
  if (!REXCVAR_GET(majin_submotion_fix) || !(dt > 0.0f)) {
    return;
  }
  // The game adds it as a single.
  speed.f64 = double(float(speed.f64 * dt * 30.0f));
}

namespace {

// The dt (30 fps frames) of the object whose motion sub_822BCE90 is stepping
// for sub_822421C8, 0 otherwise. Set and consumed on the same thread.
thread_local float object_step_scale = 0.0f;

float LoadFloat(uint32_t address) {
  uint32_t v;
  std::memcpy(&v, REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(address), 4);
  return std::bit_cast<float>(std::byteswap(v));
}

}  // namespace

void Majin_ObjectMotionStepBegin(PPCRegister& r31) {
  const float frames = LoadFloat(r31.u32 + 0x10);
  object_step_scale =
      REXCVAR_GET(majin_submotion_fix) && frames > 0.0f && frames < 10.0f ? frames : 0.0f;
}

void Majin_MotionStepTime(PPCRegister& speed) {
  if (object_step_scale > 0.0f) {
    speed.f64 = double(float(speed.f64 * object_step_scale));
  }
}

void Majin_MotionStepTotal(PPCRegister& speed) {
  if (object_step_scale > 0.0f) {
    speed.f64 = double(float(speed.f64 * object_step_scale));
    object_step_scale = 0.0f;
  }
}
