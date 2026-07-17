#pragma once
#include <algorithm>
#include <vector>
#include <glm/glm.hpp>
#include "bolt/sprint/SprintCore.hpp"
#include "bolt/world/HeightField.hpp"

namespace bolt {

struct PathPoint {
  glm::vec3 position{0.f};
};

class PathGenerator {
public:
  /**
   * Energy path ahead of Bolt — meandering crystal corridor.
   * Wider meander than simple S-curve; length grows with sprint score.
   */
  std::vector<PathPoint> generate(const SprintCore& sprint, const HeightField& height,
                                  float length = 48.f, int segments = 24) const;

  /** Half-width of ribbon mesh (meters). */
  float ribbonHalfWidth(float sprintScore) const {
    return 3.2f + std::clamp(sprintScore, 0.f, 1.2f) * 1.8f;
  }
};

} // namespace bolt
