#pragma once
#include "bolt/pcg/VegetationSpawner.hpp"
#include "bolt/sprint/SpawnBudgets.hpp"
#include "bolt/sprint/SprintCore.hpp"
#include "bolt/world/HeightField.hpp"
#include "bolt/world/SpawnRules.hpp"
#include <vector>

namespace bolt {

/**
 * Sparse hero ruins off the sprint corridor (web RuinGenerator spirit).
 * kind: 0 = pillar, 1 = arch
 */
struct RuinInstance {
  glm::vec3 position{0.f};
  float scale = 1.f;
  float yaw = 0.f;
  std::uint32_t kind = 0;
};

class RuinGenerator {
public:
  std::vector<RuinInstance> generate(const SprintCore& sprint, const SpawnRules& rules,
                                     const HeightField& height, const SpawnBudgets& budgets,
                                     int maxNew) const;
};

} // namespace bolt
