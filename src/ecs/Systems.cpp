#include "bolt/ecs/Systems.hpp"
#include "bolt/ecs/Components.hpp"
#include "bolt/core/Log.hpp"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <string>

namespace bolt {

void systemPlayerMovement(entt::registry& reg, EngineContext& ctx, float dt) {
  auto view = reg.view<Transform, Velocity, PlayerTag>();
  for (auto e : view) {
    auto& t = view.get<Transform>(e);
    auto& v = view.get<Velocity>(e);

    // Simple WASD filled externally via Velocity; integrate here
    t.position += v.linear * dt;

    // Stick to heightfield
    t.position.y = ctx.height.sample(t.position.x, t.position.z, ctx.sprint.score) + 0.9f;

    ctx.sprint.position = t.position;
    ctx.sprint.velocity = v.linear;
  }
}

void systemSprintUpdate(entt::registry& reg, EngineContext& ctx, float dt) {
  (void)reg;
  ctx.sprint.update(dt);
  ctx.budgets.applySprint(ctx.sprint);
}

void systemVegetation(entt::registry& reg, EngineContext& ctx, float dt) {
  (void)dt;
  if (!ctx.sprint.gateOpen && ctx.sprint.score < 0.35f) {
    // Still allow sparse pre-gate flora at low density
  }

  // Count existing foliage batches
  std::uint32_t totalInstances = 0;
  auto batches = reg.view<FoliageBatch>();
  for (auto e : batches) {
    totalInstances += batches.get<FoliageBatch>(e).instanceCount;
  }

  const auto& q = qualitySettings(ctx.quality);
  if (static_cast<int>(totalInstances) >= q.maxFoliageInstances) return;

  const int maxNew = ctx.budgets.vegSpawnsPerTick;
  auto fresh = ctx.vegetation.generate(
      ctx.sprint, ctx.rules, ctx.height, ctx.budgets, maxNew);
  if (fresh.empty()) return;

  // Merge into a single batch entity (GPU buffer upload = render layer TODO)
  entt::entity batchEntity = entt::null;
  for (auto e : batches) {
    batchEntity = e;
    break;
  }
  if (batchEntity == entt::null) {
    batchEntity = reg.create();
    FoliageBatch fb;
    fb.material = 1; // crystal_foliage — resolve via MaterialLibrary in app
    fb.mesh = 1;
    fb.instanceCount = 0;
    reg.emplace<FoliageBatch>(batchEntity, fb);
    reg.emplace<NameComponent>(batchEntity, "CrystalFoliageBatch");
    reg.emplace<ProceduralTag>(batchEntity);
  }

  auto& fb = reg.get<FoliageBatch>(batchEntity);
  fb.instanceCount += static_cast<std::uint32_t>(fresh.size());
  fb.lodBias = ctx.sprint.speed * 0.02f;

  // Store instances as a side buffer in future; for now log density
  if (fb.instanceCount % 64u < static_cast<std::uint32_t>(fresh.size())) {
    logInfo("VegetationSystem: instances~" + std::to_string(fb.instanceCount) +
            " score=" + std::to_string(ctx.sprint.score));
  }
}

void systemPaths(entt::registry& reg, EngineContext& ctx, float dt) {
  // Age existing
  auto view = reg.view<PathSegment, Fade>();
  for (auto e : view) {
    auto& p = view.get<PathSegment>(e);
    p.age += dt;
    auto& f = view.get<Fade>(e);
    if (p.age > p.life) f.fadingOut = true;
    if (f.fadingOut) {
      f.value = std::max(0.f, f.value - dt * 0.5f);
      if (f.value <= 0.f) reg.destroy(e);
    } else {
      f.value = std::min(1.f, f.value + dt * 1.2f);
    }
  }

  // Spawn path when gate open / sprinting
  int pathCount = 0;
  for (auto _ : reg.view<PathSegment>()) {
    (void)_;
    ++pathCount;
  }
  if (pathCount >= ctx.budgets.pathCap) return;
  if (!ctx.sprint.sprinting && ctx.sprint.score < 0.4f) return;

  auto pts = ctx.paths.generate(ctx.sprint, ctx.height, 36.f + ctx.sprint.score * 24.f, 12);
  if (pts.size() < 2) return;

  auto e = reg.create();
  PathSegment seg;
  seg.life = 10.f + ctx.sprint.score * 6.f;
  seg.material = 2; // crystal_path
  reg.emplace<PathSegment>(e, seg);
  reg.emplace<Fade>(e, Fade{0.2f, false});
  reg.emplace<ProceduralTag>(e);
  reg.emplace<NameComponent>(e, "EnergyPath");
}

void systemProceduralLifetime(entt::registry& reg, EngineContext& ctx, float dt) {
  (void)ctx;
  auto view = reg.view<ProceduralTag, Fade>();
  for (auto e : view) {
    auto& tag = view.get<ProceduralTag>(e);
    tag.spawnTime += dt;
    if (tag.spawnTime > tag.life) {
      view.get<Fade>(e).fadingOut = true;
    }
  }
}

void systemBoltAura(entt::registry& reg, EngineContext& ctx, float dt) {
  auto view = reg.view<BoltAura, PlayerTag>();
  for (auto e : view) {
    auto& a = view.get<BoltAura>(e);
    const float target =
        (ctx.sprint.sprinting ? 0.35f : 0.05f) + ctx.sprint.score * 0.55f + ctx.sprint.resonance * 0.2f;
    a.intensity += (target - a.intensity) * std::min(1.f, dt * 6.f);
    a.pulsePhase += dt * (3.f + a.intensity * 4.f);
  }
}

void runSimulationSystems(entt::registry& reg, EngineContext& ctx, float dt) {
  systemPlayerMovement(reg, ctx, dt);
  systemSprintUpdate(reg, ctx, dt);
  systemBoltAura(reg, ctx, dt);
}

void runSpawnSystems(entt::registry& reg, EngineContext& ctx, float dt) {
  systemVegetation(reg, ctx, dt);
  systemPaths(reg, ctx, dt);
  systemProceduralLifetime(reg, ctx, dt);
}

} // namespace bolt
