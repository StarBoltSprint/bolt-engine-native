#include "bolt/pcg/DetailSpawner.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace bolt {
namespace {
float hash2(float x, float z) {
  float s = std::sin(x * 53.1f + z * 91.7f) * 43758.5453f;
  return s - std::floor(s);
}
} // namespace

std::vector<FoliageInstance> DetailSpawner::generate(const SprintCore& sprint,
                                                     const SpawnRules& rules,
                                                     const HeightField& height,
                                                     const SpawnBudgets& budgets,
                                                     int maxNew) const {
  std::vector<FoliageInstance> out;
  if (maxNew <= 0) return out;

  const glm::vec3 pred = sprint.predictedPosition(2.2f + sprint.score * 1.5f);
  const float yaw = sprint.yaw;
  const float fx = std::sin(yaw), fz = std::cos(yaw);
  const float rx = std::cos(yaw), rz = -std::sin(yaw);

  std::mt19937 rng{static_cast<std::uint32_t>(
      std::hash<float>{}(pred.x * 3.1f + 9.f) ^ std::hash<float>{}(pred.z * 5.7f))};
  std::uniform_real_distribution<float> U(0.f, 1.f);

  int tries = maxNew * 8;
  while (static_cast<int>(out.size()) < maxNew && tries-- > 0) {
    // Closer to corridor than big veg, but not on path
    const float sideSign = U(rng) > 0.5f ? 1.f : -1.f;
    const float along = (U(rng) - 0.35f) * 55.f;
    const float side =
        sideSign * (rules.clear().corridorHalf + 3.5f + U(rng) * (18.f + sprint.score * 10.f));
    glm::vec3 p;
    p.x = pred.x + fx * along + rx * side;
    p.z = pred.z + fz * along + rz * side;

    if (rules.tooCloseToPlayer(p, sprint.position, 12.f)) continue;
    if (rules.inRunCorridor(p, sprint.position, yaw)) continue;

    // Clumpy noise — detail clusters
    const float clump = hash2(std::floor(p.x * 0.12f), std::floor(p.z * 0.12f));
    if (clump < 0.35f) continue;

    p.y = height.sample(p.x, p.z, sprint.score);
    FoliageInstance d;
    d.position = p;
    d.scale = 0.25f + U(rng) * 0.55f + clump * 0.2f;
    d.yaw = U(rng) * 6.28318f;
    d.kind = U(rng) > 0.6f ? 1u : 0u; // shard subtypes
    out.push_back(d);
  }

  const int cap = std::max(4, budgets.detailCap * 3);
  if (static_cast<int>(out.size()) > cap) out.resize(static_cast<size_t>(cap));
  return out;
}

} // namespace bolt
