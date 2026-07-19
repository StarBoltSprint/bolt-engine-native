#pragma once
#include "bolt/sprint/SprintCore.hpp"

namespace bolt {

/** Live caps — HIGH/MAX richer flora; Low stays light (web PERF lesson). */
struct SpawnBudgets {
  int pathCap = 3;
  int terrainCap = 4;
  int vegInstances = 2000;   // GPU instances, not entities
  int forestBatches = 3;
  int detailCap = 12;
  int vegSpawnsPerTick = 2;

  float densityMul = 1.f;    // from sprint score
  float particleMul = 1.f;

  static SpawnBudgets forQuality(int tier /*0 low .. 3 max*/);
  void applySprint(const SprintCore& sprint);
};

/** Hard exclusion — dense forest on flanks, open personal bubble around Bolt */
struct ClearRules {
  float playerRadius = 28.f;   // solid veg keep-out around player (spawn-time)
  float corridorHalf = 14.f;   // run lane half-width (clear ahead path)
  float corridorAhead = 90.f;
  float corridorBehind = 16.f;
  float vegMin = 24.f;
  float forestMin = 48.f;
  /** Live pack-time bubble (moves with player; flanks stay dense) */
  float packClearSmall = 22.f; // stalks / clusters / floaters / detail
  float packClearBush = 26.f;
  float packClearTree = 36.f;  // tall crystal trees — keep body-scale open
};

} // namespace bolt
