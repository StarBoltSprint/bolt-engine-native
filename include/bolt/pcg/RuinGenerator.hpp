#pragma once
#include "bolt/pcg/VegetationSpawner.hpp"
#include "bolt/sprint/SpawnBudgets.hpp"
#include "bolt/sprint/SprintCore.hpp"
#include "bolt/world/HeightField.hpp"
#include "bolt/world/SpawnRules.hpp"
#include <vector>

namespace bolt {

/**
 * Crystal Nebula Plains RuinGenerator — ancient Resonance structures.
 *
 * kind:
 *   0 = Resonance Monolith   (tall crystal pillar + runes)
 *   1 = Floating Archway     (partially levitating arch)
 *   2 = Crystal Observatory  (domed ruin with lenses)
 *   3 = Buried Temple        (half-sunk mass with crystal growth)
 *
 * Placement respects CLEAR (path + player bubble). Density/scale/complexity
 * grow with Meaningful Sprint Score.
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

  /** Sparse ruins inside one chunk AABB; CLEAR vs real player. */
  std::vector<RuinInstance> generateInChunk(int cx, int cz, float chunkSize,
                                            const SprintCore& sprint, const SpawnRules& rules,
                                            const HeightField& height,
                                            const SpawnBudgets& budgets, int maxNew) const;
};

} // namespace bolt
