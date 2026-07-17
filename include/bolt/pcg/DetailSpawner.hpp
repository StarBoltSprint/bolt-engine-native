#pragma once
#include "bolt/pcg/VegetationSpawner.hpp"
#include "bolt/sprint/SpawnBudgets.hpp"
#include "bolt/sprint/SprintCore.hpp"
#include "bolt/world/HeightField.hpp"
#include "bolt/world/SpawnRules.hpp"
#include <vector>

namespace bolt {

/**
 * Small props PCG — crystal shards, pebbles, frost nodules near flanks.
 * Reuses FoliageInstance layout (kind = detail subtype).
 */
class DetailSpawner {
public:
  std::vector<FoliageInstance> generate(const SprintCore& sprint, const SpawnRules& rules,
                                        const HeightField& height, const SpawnBudgets& budgets,
                                        int maxNew) const;
};

} // namespace bolt
