#pragma once
#include <glm/glm.hpp>
#include <vector>
#include "bolt/sprint/SprintCore.hpp"
#include "bolt/world/HeightField.hpp"

namespace bolt {

struct PathPoint {
  glm::vec3 position;
};

class PathGenerator {
public:
  /** Energy path ahead of Bolt along yaw, sampling height. */
  std::vector<PathPoint> generate(
      const SprintCore& sprint,
      const HeightField& height,
      float length = 40.f,
      int segments = 16) const;
};

} // namespace bolt
