// Majistic Recomp - hook on the engine's frame timer (sub_82162238)
// Adapted from ArthurMS15/MajinRecomp
//
// The timer measures the real time since the last frame, scales it, clamps it
// to 0.1 s and stores it at +0x150F4 (dt in seconds) and dt * rate at +0x150F8
// (the rate at +0x15104, frames of reference). The hook runs at 0x82162360,
// just before the store, with the dt in f0.
//
// The character gets stuck on small edges and holes when frames are short or
// uneven (above ~80 fps, or at 60 with frame generation, whose proxy swap
// chain makes the frame times jitter). --majin_dt_smooth replaces the measured
// dt with a smoothed one: steps are even, and the difference is carried over so
// that game time still follows real time. --majin_log_dt logs the dt spread
// once per second.

#include "frame_timer.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>

REXCVAR_DEFINE_BOOL(majin_dt_smooth, true, "Majin",
                    "Even out the game's frame time steps (keeps game time in step with real time)");
REXCVAR_DEFINE_BOOL(majin_log_dt, false, "Majin",
                    "Log the game's frame time (dt) spread once per second (diagnostics)");

namespace {

// Read by animation hooks, which may run on other threads.
std::atomic<float> game_dt{0.0f};

// Only touched by the game's main thread, which runs the timer.
double smoothed = 0.0;  // average step, seconds
double carry = 0.0;     // real time not yet handed to the game, seconds

struct Stats {
  int64_t second = 0;
  uint32_t frames = 0;
  double min_in = 1.0, max_in = 0.0, min_out = 1.0, max_out = 0.0, sum_in = 0.0;
} stats;

void LogStats(double in, double out) {
  const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
  if (now != stats.second) {
    if (stats.frames) {
      REXLOG_INFO("majin dt: {} frames, real {:.2f}..{:.2f} ms (avg {:.2f}), game {:.2f}..{:.2f} ms",
                  stats.frames, stats.min_in * 1e3, stats.max_in * 1e3,
                  stats.sum_in * 1e3 / stats.frames, stats.min_out * 1e3, stats.max_out * 1e3);
    }
    stats = Stats{};
    stats.second = now;
  }
  ++stats.frames;
  stats.sum_in += in;
  stats.min_in = std::min(stats.min_in, in);
  stats.max_in = std::max(stats.max_in, in);
  stats.min_out = std::min(stats.min_out, out);
  stats.max_out = std::max(stats.max_out, out);
}

double Smooth(double dt) {
  // Loading screens (dt clamped to 0.1 s) and the first frame (dt 0): no
  // smoothing, start over. A single long frame is not a reason: with frame
  // generation a 50 ms frame is followed by a 0.5 ms one, and that pair is
  // exactly what has to be spread out.
  if (dt <= 0.0 || dt >= 0.1 || smoothed <= 0.0) {
    smoothed = dt > 0.0 && dt < 0.1 ? dt : 0.0;
    carry = 0.0;
    return dt;
  }
  // Slow average: follows frame rate changes within a fraction of a second,
  // ignores frame-to-frame jitter.
  smoothed += (dt - smoothed) * 0.05;
  carry += dt;
  // Hand out the average step plus a small share of the backlog, so the
  // backlog never grows and game time stays within a few ms of real time.
  double step = smoothed + (carry - smoothed) * 0.1;
  step = std::clamp(step, smoothed * 0.75, smoothed * 1.25);
  // Fell far behind (a sudden drop in frame rate): catch up at once.
  if (carry - step > 0.05) step = carry;
  // May run ahead of real time by a few steps (up to 0.1 s), repaid by the
  // next steps: short frames come in bursts (a few 2-3 ms frames at 30 fps
  // with frame generation), and each would otherwise get a short step.
  step = std::min(step, carry + std::min(smoothed * 3.0, 0.1));
  carry -= step;
  return step;
}

}  // namespace

void Majin_FrameDt(PPCRegister& f0) {
  const double in = f0.f64;
  double out = in;
  if (REXCVAR_GET(majin_dt_smooth)) {
    out = Smooth(in);
    // The game reads it back as a single.
    f0.f64 = double(float(out));
  }
  if (REXCVAR_GET(majin_log_dt)) {
    LogStats(in, out);
  }
  game_dt.store(float(f0.f64), std::memory_order_relaxed);
}

float majin::GameFrameTime() {
  return game_dt.load(std::memory_order_relaxed);
}
