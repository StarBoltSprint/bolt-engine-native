#pragma once
#include <entt/entt.hpp>
#include "bolt/sprint/SprintCore.hpp"
#include "bolt/sprint/SpawnBudgets.hpp"
#include "bolt/world/SpawnRules.hpp"
#include "bolt/world/HeightField.hpp"
#include "bolt/pcg/VegetationSpawner.hpp"
#include "bolt/pcg/PathGenerator.hpp"
#include "bolt/render/QualityTier.hpp"

namespace bolt {

struct EngineContext {
  SprintCore sprint;
  SpawnBudgets budgets;
  SpawnRules rules;
  HeightField height;
  QualityTier quality = QualityTier::Medium;
  VegetationSpawner vegetation;
  PathGenerator paths;
  double elapsed = 0.0;
};

/** Player movement — fixed dt only. */
void systemPlayerMovement(entt::registry& reg, EngineContext& ctx, float dt);

/** Sync SprintCore from player entity. */
void systemSprintUpdate(entt::registry& reg, EngineContext& ctx, float dt);

/**
 * VegetationSystem — example sprint-aware PCG system.
 * Spawns/despawns GPU foliage batches in prediction cone; respects CLEAR rules.
 */
void systemVegetation(entt::registry& reg, EngineContext& ctx, float dt);

/** Path ribbons ahead of Bolt. */
void systemPaths(entt::registry& reg, EngineContext& ctx, float dt);

/** Fade / despawn aged procedural entities. */
void systemProceduralLifetime(entt::registry& reg, EngineContext& ctx, float dt);

/** Bolt lightning aura intensity from sprint score. */
void systemBoltAura(entt::registry& reg, EngineContext& ctx, float dt);

/** Ordered frame: call after fixed physics steps. */
void runSimulationSystems(entt::registry& reg, EngineContext& ctx, float dt);
void runSpawnSystems(entt::registry& reg, EngineContext& ctx, float dt);

} // namespace bolt
