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

/** Hard exclusion — port of CLEAR from procedural.js */
struct ClearRules {
  float playerRadius = 36.f;
  float corridorHalf = 14.f;
  float corridorAhead = 100.f;
  float corridorBehind = 18.f;
  float vegMin = 34.f;
  float forestMin = 64.f;
};

} // namespace bolt
