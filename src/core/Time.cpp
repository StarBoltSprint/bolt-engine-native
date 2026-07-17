#include "bolt/core/Time.hpp"
#include <algorithm>
#include <cmath>

namespace bolt {

void Time::beginFrame(float rawDt) {
  if (!std::isfinite(rawDt) || rawDt < 0.f) rawDt = kFixedDt;
  if (rawDt > 0.25f) rawDt = 0.25f;
  realDt = rawDt;
  elapsed += rawDt;
  accumulator += rawDt;

  const float instFps = rawDt > 1e-5f ? 1.f / rawDt : 60.f;
  fpsSmooth = fpsSmooth * 0.92f + instFps * 0.08f;
  lagging = fpsSmooth < 24.f;
}

int Time::consumeFixedSteps() {
  int steps = 0;
  const int maxS = lagging ? kMaxSubsteps : std::min(kMaxSubsteps, 4);
  while (accumulator >= kFixedDt && steps < maxS) {
    accumulator -= kFixedDt;
    ++steps;
  }
  if (accumulator > kFixedDt * 3.f) accumulator = 0.f;
  return steps;
}

} // namespace bolt
