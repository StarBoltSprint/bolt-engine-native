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
    case 2: // High
      b.vegInstances = 3500;
      b.forestBatches = 3;
      b.vegSpawnsPerTick = 3;
      b.detailCap = 16;
      break;
    default: // Max
      b.vegInstances = 6000;
      b.forestBatches = 4;
      b.vegSpawnsPerTick = 4;
      b.detailCap = 20;
      break;
  }
  return b;
}

void SpawnBudgets::applySprint(const SprintCore& sprint) {
  // High score: more large drama, controlled small clutter (web lesson)
  densityMul = 0.75f + sprint.score * 0.55f;
  particleMul = 0.4f + sprint.score * 0.8f;
  if (sprint.score > 0.85f) {
    // Prefer fewer spam particles at very high dens
    particleMul *= 0.85f;
  }
}

} // namespace bolt
