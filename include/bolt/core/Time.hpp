#pragma once

namespace bolt {

/** Fixed-step physics (matches web freeze-fix lessons). */
struct Time {
  static constexpr float kFixedDt = 1.f / 60.f;
  static constexpr int   kMaxSubsteps = 5;

  float realDt = 0.f;       // last frame wall clock
  float accumulator = 0.f;
  double elapsed = 0.0;
  float fpsSmooth = 60.f;
  bool  lagging = false;

  void beginFrame(float rawDt);
  /** Returns how many fixed steps to run this frame. */
  int consumeFixedSteps();
};

} // namespace bolt
