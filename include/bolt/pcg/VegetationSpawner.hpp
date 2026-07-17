#pragma once
#include <glm/glm.hpp>
#include <vector>
#include "bolt/sprint/SprintCore.hpp"
#include "bolt/sprint/SpawnBudgets.hpp"
#include "bolt/world/SpawnRules.hpp"
#include "bolt/world/HeightField.hpp"

namespace bolt {

struct FoliageInstance {
  glm::vec3 position;
  float scale = 1.f;
  float yaw = 0.f;
  std::uint32_t kind = 0;
};

/**
 * CPU placement for Crystal vegetation.
 * Domain-warped density + flank-only solids (web forestDensity / CLEAR).
 */
class VegetationSpawner {
public:
  /** 0..1 domain-warped forest/flora density at XZ. */
  float densityAt(float x, float z, float sprintScore) const;

  /**
   * Fill instances around predicted position.
   * Never places inside player bubble or run corridor.
   */
  std::vector<FoliageInstance> generate(
      const SprintCore& sprint,
      const SpawnRules& rules,
      const HeightField& height,
      const SpawnBudgets& budgets,
      int maxNew) const;
};

} // namespace bolt
