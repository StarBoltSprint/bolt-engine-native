#include "bolt/pcg/PathGenerator.hpp"
#include <cmath>

namespace bolt {

std::vector<PathPoint> PathGenerator::generate(
    const SprintCore& sprint,
    const HeightField& height,
    float length,
    int segments) const {
  std::vector<PathPoint> pts;
  if (segments < 2) return pts;

  const float fx = std::sin(sprint.yaw);
  const float fz = std::cos(sprint.yaw);
  // Start slightly ahead of Bolt
  const float startAhead = 12.f;
  glm::vec3 cursor = sprint.position + glm::vec3(fx, 0.f, fz) * startAhead;

  pts.reserve(static_cast<std::size_t>(segments + 1));
  for (int i = 0; i <= segments; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(segments);
    const float dist = t * length;
    // Gentle crystal S-curve
    const float side = std::sin(t * 3.14159f * 2.f + sprint.position.x * 0.01f) * 1.2f;
    const float rx = std::cos(sprint.yaw);
    const float rz = -std::sin(sprint.yaw);
    glm::vec3 p = cursor + glm::vec3(fx, 0.f, fz) * dist + glm::vec3(rx, 0.f, rz) * side;
    p.y = height.sample(p.x, p.z, sprint.score) + 0.12f;
    pts.push_back({p});
  }
  return pts;
}

} // namespace bolt
