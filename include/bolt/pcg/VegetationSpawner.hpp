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
  /** For kind==4 crystal trees: 0..9 distinct mesh types (see PropMeshes kCrystalTreeTypes) */
  std::uint32_t treeVariant = 0;
};

/**
 * Crystal Nebula Plains VegetationGenerator.
 * Domain-warped density (organic thickets + resonance veins), CLEAR corridor respect,
 * sprint-reactive scale/density/kinds (stalks, ferns, crystal clusters, trees, floaters).
 *
 * kind: 0 stalk  1 flower  2 crystal  3 bush/fern  4 crystal tree (10 variants)  5 floater
 */
class VegetationSpawner {
public:
  /** 0..1 domain-warped forest/flora density at XZ (sprint strengthens forests). */
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

  /**
   * Fill instances that land inside one world chunk AABB.
   * CLEAR is evaluated against the real player (not chunk center).
   */
  std::vector<FoliageInstance> generateInChunk(
      int cx, int cz, float chunkSize, const SprintCore& sprint, const SpawnRules& rules,
      const HeightField& height, const SpawnBudgets& budgets, int maxNew) const;
};

} // namespace bolt
