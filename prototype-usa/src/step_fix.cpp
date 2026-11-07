// Majistic Recomp - characters stuck on small edges and holes at high frame rates
// Adapted from ArthurMS15/MajinRecomp
//
// sub_82344F98 moves a character: the motion system advances the animation by
// obj+0x5D0 * obj+0x10 (dt in 30 fps frames) and writes the root-motion target
// position to r1+0x58..0x60; the target is then stored at obj+0xE0. Later in
// the frame the collision proxy (sub_822A9550, sub_829C0DE8) pushes the
// character out of whatever it entered.
//
// At 30 or 60 fps a step into the lip of a small edge goes deep enough that
// the shortest way out is up, and the character climbs it. Above ~80 fps the
// step is a fraction of that, the way out is backwards, and the edge acts as
// an invisible wall (a long frame, such as a hitch, gets the character out).
//
// The hook runs at 0x823450A4, right after the target is computed and before
// it is stored. When the character tried to move last frame and barely moved,
// this frame's horizontal step is scaled up to what it would be at 30 fps, so
// the collision resolves the edge as it does on the console. While it stays
// blocked (it moved less than 30% of the way, so sliding along a wall doesn't
// count) for 0.05 s, the step grows from one 30 fps frame up to
// majin_step_fix_max (default 4) 0.1 s later (what a hitch does, which always
// got the character out). The grace period keeps brushing against stairs or
// a ledge from lifting the character onto them. Free movement is
// untouched; against a real wall the character stays blocked either way.

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>

REXCVAR_DEFINE_BOOL(majin_step_fix, true, "Majin",
                    "Let characters climb small edges at high frame rates (bigger step when blocked)");
REXCVAR_DEFINE_DOUBLE(majin_step_fix_max, 4.0, "Majin",
                      "Largest blocked step, in 30 fps frames (higher climbs edges more eagerly)");
REXCVAR_DECLARE(bool, majin_log_dt);

namespace {

struct Track {
  float start[3] = {};   // position before last frame's move
  float target[3] = {};  // where last frame's move wanted to go
  uint32_t frame = 0;    // frame counter of the last update, to spot gaps
  float blocked_s = 0;   // how long the character has been blocked, seconds
};

std::mutex tracks_mutex;
std::unordered_map<uint32_t, Track> tracks;
uint32_t frame_counter = 0;
uint32_t boosts = 0;
int64_t boost_second = 0;

float LoadFloat(uint32_t address) {
  uint32_t v;
  std::memcpy(&v, REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(address), 4);
  return std::bit_cast<float>(std::byteswap(v));
}

void StoreFloat(uint32_t address, float value) {
  const uint32_t v = std::byteswap(std::bit_cast<uint32_t>(value));
  std::memcpy(REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(address), &v, 4);
}

}  // namespace

void Majin_MoveStep(PPCRegister& r1, PPCRegister& r28) {
  const uint32_t obj = r28.u32;
  const uint32_t sp = r1.u32;
  float pos[3], target[3];
  for (int i = 0; i < 3; ++i) {
    pos[i] = LoadFloat(obj + 0xE0 + 4 * i);
    target[i] = LoadFloat(sp + 0x58 + 4 * i);
  }
  // dt in 30 fps frames: 1 at 30 fps, 0.5 at 60, 0.25 at 120.
  const float frames = LoadFloat(obj + 0x10);

  std::lock_guard<std::mutex> lock(tracks_mutex);
  if (tracks.size() > 512) tracks.clear();  // characters come and go
  Track& t = tracks[obj];
  ++frame_counter;
  bool blocked = false;
  if (t.frame && frame_counter - t.frame < 64) {
    // Horizontal (x, z) movement wanted last frame vs what happened.
    const float want_x = t.target[0] - t.start[0], want_z = t.target[2] - t.start[2];
    const float got_x = pos[0] - t.start[0], got_z = pos[2] - t.start[2];
    const float want = std::sqrt(want_x * want_x + want_z * want_z);
    const float got = std::sqrt(got_x * got_x + got_z * got_z);
    // The wanted step is compared per second (dt = frames / 30), so that slow
    // walking at 240 fps still counts.
    blocked = want > 0.005f * frames && got < want * 0.3f;
  }
  t.blocked_s = blocked ? t.blocked_s + frames / 30.0f : 0.0f;
  t.frame = frame_counter;
  std::memcpy(t.start, pos, sizeof(pos));
  std::memcpy(t.target, target, sizeof(target));

  constexpr float kGrace = 0.05f;
  if (!blocked || t.blocked_s < kGrace || !REXCVAR_GET(majin_step_fix) || !(frames > 0.0f) ||
      frames >= 1.0f) {
    return;
  }
  // 30 fps frames to step: 1 after the grace period, the maximum (4 by
  // default) 0.1 s later.
  const float max_steps = std::max(1.0f, float(REXCVAR_GET(majin_step_fix_max)));
  const float steps = std::min(1.0f + (t.blocked_s - kGrace) * 30.0f, max_steps);
  const float scale = std::min(steps / frames, 64.0f);
  StoreFloat(sp + 0x58, pos[0] + (target[0] - pos[0]) * scale);
  StoreFloat(sp + 0x60, pos[2] + (target[2] - pos[2]) * scale);
  // Track what was actually asked for, so the next frame compares with it.
  t.target[0] = pos[0] + (target[0] - pos[0]) * scale;
  t.target[2] = pos[2] + (target[2] - pos[2]) * scale;

  if (REXCVAR_GET(majin_log_dt)) {
    ++boosts;
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    if (now != boost_second) {
      REXLOG_INFO("majin step fix: {} blocked steps scaled x{:.1f} last second", boosts, scale);
      boosts = 0;
      boost_second = now;
    }
  }
}
