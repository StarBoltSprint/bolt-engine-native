#include "bolt/pcg/RuinGenerator.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace bolt {
namespace {
float hash2(float x, float z) {
  float s = std::sin(x * 91.7f + z * 47.3f) * 43758.5453f;
  return s - std::floor(s);
}
} // namespace

std::vector<RuinInstance> RuinGenerator::generate(const SprintCore& sprint,
                                                  const SpawnRules& rules,
                                                  const HeightField& height,
                                                  const SpawnBudgets& budgets,
                                                  int maxNew) const {
  std::vector<RuinInstance> out;
  if (maxNew <= 0) return out;

  const glm::vec3 pred = sprint.predictedPosition(4.f + sprint.score * 3.f);
  const float yaw = sprint.yaw;
  const float fx = std::sin(yaw), fz = std::cos(yaw);
  const float rx = std::cos(yaw), rz = -std::sin(yaw);

  std::mt19937 rng{static_cast<std::uint32_t>(
      std::hash<float>{}(std::floor(pred.x / 40.f) * 17.f) ^
      std::hash<float>{}(std::floor(pred.z / 40.f) * 31.f))};
  std::uniform_real_distribution<float> U(0.f, 1.f);

  int tries = maxNew * 10;
  while (static_cast<int>(out.size()) < maxNew && tries-- > 0) {
    // Far flanks only — hero scale props
    const float sideSign = U(rng) > 0.5f ? 1.f : -1.f;
    const float r = 55.f + U(rng) * (70.f + sprint.score * 40.f);
    const float along = (U(rng) - 0.2f) * 50.f;
    const float side = sideSign * (rules.clear().corridorHalf + 22.f + U(rng) * 40.f);
    glm::vec3 p;
    p.x = pred.x + fx * along + rx * side;
    p.z = pred.z + fz * along + rz * side;
    p.y = 0.f;

    if (!rules.canSpawnSolid(p, sprint.position, yaw)) continue;
    if (rules.tooCloseToPlayer(p, sprint.position, rules.clear().forestMin * 0.85f)) continue;

    // Sparse: noise gate
    if (hash2(std::floor(p.x * 0.03f), std::floor(p.z * 0.03f)) < 0.62f) continue;

    p.y = height.sample(p.x, p.z, sprint.score);
    RuinInstance rui;
    rui.position = p;
    rui.scale = 1.1f + U(rng) * 1.4f + sprint.score * 0.4f;
    rui.yaw = U(rng) * 6.28318f;
    rui.kind = U(rng) > 0.55f ? 1u : 0u; // arch / pillar
    out.push_back(rui);
  }

  const int cap = std::max(1, budgets.detailCap / 3);
  if (static_cast<int>(out.size()) > cap) out.resize(static_cast<size_t>(cap));
  return out;
}

} // namespace bolt
