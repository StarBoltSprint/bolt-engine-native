#include "bolt/pcg/PathGenerator.hpp"
#include <algorithm>
#include <cmath>

namespace bolt {

std::vector<PathPoint> PathGenerator::generate(const SprintCore& sprint,
                                               const HeightField& height, float length,
                                               int segments) const {
  std::vector<PathPoint> pts;
  if (segments < 2) return pts;

  const float fx = std::sin(sprint.yaw);
  const float fz = std::cos(sprint.yaw);
  const float rx = std::cos(sprint.yaw);
  const float rz = -std::sin(sprint.yaw);
  const float startAhead = 8.f;
  glm::vec3 cursor = sprint.position + glm::vec3(fx, 0.f, fz) * startAhead;

  // Seed meander from world pos so path is coherent when regenerated
  const float seed = sprint.position.x * 0.017f + sprint.position.z * 0.013f;

  pts.reserve(static_cast<std::size_t>(segments + 1));
  for (int i = 0; i <= segments; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(segments);
    const float dist = t * length;
    // Multi-frequency meander (crystal energy path)
    const float side = std::sin(t * 6.28318f * 1.1f + seed) * 3.8f +
                       std::sin(t * 6.28318f * 2.7f + seed * 2.1f) * 1.4f +
                       std::sin(t * 6.28318f * 0.35f + seed * 0.5f) * 2.2f;
    glm::vec3 p = cursor + glm::vec3(fx, 0.f, fz) * dist + glm::vec3(rx, 0.f, rz) * side;
    p.y = height.sample(p.x, p.z, sprint.score) + 0.1f;
    pts.push_back({p});
  }
  return pts;
}

} // namespace bolt
