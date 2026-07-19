#include "bolt/sprint/SpawnBudgets.hpp"
#include <algorithm>

namespace bolt {

SpawnBudgets SpawnBudgets::forQuality(int tier) {
  SpawnBudgets b;
  switch (tier) {
    case 0: // Low
      b.vegInstances = 400;
      b.forestBatches = 1;
      b.vegSpawnsPerTick = 1;
      b.pathCap = 2;
      b.detailCap = 4;
      break;
    case 1: // Med
      b.vegInstances = 1200;
      b.forestBatches = 2;
      b.vegSpawnsPerTick = 2;
      break;
    case 2: // High — dense Crystal Nebula forests (GPU cull carries load)
      b.vegInstances = 5500;
      b.forestBatches = 4;
      b.vegSpawnsPerTick = 5;
      b.detailCap = 22;
      break;
    default: // Max
      b.vegInstances = 9000;
      b.forestBatches = 5;
      b.vegSpawnsPerTick = 6;
      b.detailCap = 28;
      break;
  }
  return b;
}

void SpawnBudgets::applySprint(const SprintCore& sprint) {
  // Meaningful Sprint densifies forests hard; GPU cull keeps draw cheap
  densityMul = 0.9f + sprint.score * 0.85f;
  particleMul = 0.45f + sprint.score * 0.9f;
  if (sprint.score > 0.9f) {
    particleMul *= 0.9f;
  }
}

} // namespace bolt
