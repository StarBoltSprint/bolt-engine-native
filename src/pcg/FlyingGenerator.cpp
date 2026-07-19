#include "bolt/pcg/FlyingGenerator.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace bolt {
namespace {

float hash2(float x, float z) {
  float s = std::sin(x * 127.1f + z * 311.7f) * 43758.5453f;
  return s - std::floor(s);
}

float hash3(float x, float z, float w) {
  float s = std::sin(x * 12.9898f + z * 78.233f + w * 45.164f) * 43758.5453f;
  return s - std::floor(s);
}

std::uint32_t pickFlyingKind(float score, float n) {
  if (score < 0.3f) {
    if (n > 0.92f) return FlyingKind::ShardMed;
    if (n > 0.75f) return FlyingKind::ShardTiny;
    return FlyingKind::ShardTiny;
  }
  if (score < 0.7f) {
    if (n > 0.88f) return FlyingKind::SkyMote;
    if (n > 0.7f) return FlyingKind::ShardMed;
    if (n > 0.45f) return FlyingKind::ShardTiny;
    return FlyingKind::ShardTiny;
  }
  // High sprint — richer mix
  if (n > 0.82f) return FlyingKind::SkyMote;
  if (n > 0.62f) return FlyingKind::Debris;
  if (n > 0.38f) return FlyingKind::ShardMed;
  return FlyingKind::ShardTiny;
}

FoliageInstance makeFlyer(const glm::vec3& ground, std::uint32_t kind, float score, float u1,
                          float u2) {
  FoliageInstance f;
  f.kind = kind;
  f.yaw = u1 * 6.28318f;
  f.treeVariant = 0;

  float hover = 1.2f;
  float sc = 0.3f;
  switch (kind) {
  case FlyingKind::ShardMed:
    hover = 1.5f + u2 * 3.5f + score * 1.5f;
    sc = 0.45f + u1 * 0.55f + score * 0.15f;
    break;
  case FlyingKind::Debris:
    hover = 2.0f + u2 * 4.0f + score * 2.0f;
    sc = 0.6f + u2 * 0.8f;
    break;
  case FlyingKind::SkyMote:
    hover = 8.f + u2 * 18.f + score * 6.f;
    sc = 0.35f + u1 * 0.4f;
    break;
  default: // Tiny
    hover = 0.8f + u2 * 2.2f + score * 0.8f;
    sc = 0.18f + u1 * 0.28f;
    break;
  }

  f.position = ground + glm::vec3(0.f, hover, 0.f);
  f.scale = sc;
  // Encode phase seed in treeVariant for stable bob (0–255)
  f.treeVariant = static_cast<std::uint32_t>(std::clamp(hash3(ground.x, ground.z, hover) * 255.f, 0.f, 255.f));
  return f;
}

} // namespace

std::vector<FoliageInstance> FlyingGenerator::generateInChunk(
    int cx, int cz, float chunkSize, const SprintCore& sprint, const SpawnRules& rules,
    const HeightField& height, const SpawnBudgets& budgets, int maxNew) const {
  std::vector<FoliageInstance> out;
  if (maxNew <= 0 || chunkSize <= 1.f) return out;

  const float score = std::clamp(sprint.score, 0.f, 1.4f);
  const float x0 = cx * chunkSize;
  const float z0 = cz * chunkSize;
  const float yaw = sprint.yaw;

  std::mt19937 rng{static_cast<std::uint32_t>(
      static_cast<std::uint32_t>(cx) * 2654435761u ^ static_cast<std::uint32_t>(cz) * 2246822519u ^
      0xF1A7u)};
  std::uniform_real_distribution<float> U(0.f, 1.f);

  // Low sparse → high dense
  const float dens = 0.4f + score * 1.4f + budgets.densityMul * 0.25f;
  const int target = std::max(6, static_cast<int>(maxNew * dens));
  int tries = target * 12;

  while (static_cast<int>(out.size()) < target && tries-- > 0) {
    glm::vec3 p;
    p.x = x0 + U(rng) * chunkSize;
    p.z = z0 + U(rng) * chunkSize;

    // Allow flyers over path (airborne) but keep a soft player bubble
    if (rules.tooCloseToPlayer(p, sprint.position, 8.f)) continue;
    // Prefer not spawning sky clutter in deep corridor at low score
    if (score < 0.25f && rules.inRunCorridor(p, sprint.position, yaw)) continue;

    const float clump = hash2(std::floor(p.x * 0.08f), std::floor(p.z * 0.08f));
    const float thr = 0.38f - score * 0.18f;
    if (clump < thr) continue;

    p.y = height.sample(p.x, p.z, score);
    const float n = hash3(p.x, p.z, 2.1f);
    const auto kind = pickFlyingKind(score, n);
    out.push_back(makeFlyer(p, kind, score, U(rng), U(rng)));
  }

  const int cap = std::max(8, static_cast<int>(18 + score * 40.f));
  if (static_cast<int>(out.size()) > cap) out.resize(static_cast<size_t>(cap));
  return out;
}

std::vector<FoliageInstance> FlyingGenerator::generateNearRuin(const RuinInstance& ruin,
                                                              const SprintCore& sprint,
                                                              const HeightField& height,
                                                              int maxNew) const {
  std::vector<FoliageInstance> out;
  if (maxNew <= 0) return out;
  const float score = std::clamp(sprint.score, 0.f, 1.4f);

  std::mt19937 rng{static_cast<std::uint32_t>(
      std::hash<float>{}(ruin.position.x * 13.f) ^ std::hash<float>{}(ruin.position.z * 29.f) ^
      0xDEB15u)};
  std::uniform_real_distribution<float> U(0.f, 1.f);

  const int n = std::max(2, maxNew);
  for (int i = 0; i < n; ++i) {
    const float ang = U(rng) * 6.28318f;
    const float rad = 4.f + U(rng) * (10.f + ruin.scale * 0.4f);
    glm::vec3 p;
    p.x = ruin.position.x + std::cos(ang) * rad;
    p.z = ruin.position.z + std::sin(ang) * rad;
    p.y = height.sample(p.x, p.z, score);

    // Prefer debris + medium shards around ruins
    const auto kind = (i % 3 == 0) ? FlyingKind::Debris
                      : (U(rng) > 0.45f) ? FlyingKind::ShardMed
                                         : FlyingKind::ShardTiny;
    auto f = makeFlyer(p, kind, score, U(rng), U(rng));
    // Lift slightly more — high Resonance near monuments
    f.position.y += 0.6f + score * 1.2f;
    out.push_back(f);
  }
  return out;
}

void FlyingGenerator::animateForRender(const std::vector<FoliageInstance>& src,
                                       std::vector<FoliageInstance>& out, float timeSec,
                                       float score) {
  out.clear();
  out.reserve(src.size());
  const float speedMul = 0.55f + score * 0.9f;
  for (const auto& s : src) {
    FoliageInstance a = s;
    const float phase = static_cast<float>(s.treeVariant) / 255.f * 6.28318f;
    float bobAmp = 0.25f;
    float spin = 0.15f;
    float lateral = 0.08f;
    if (s.kind == FlyingKind::ShardMed) {
      bobAmp = 0.4f;
      spin = 0.22f;
    } else if (s.kind == FlyingKind::Debris) {
      bobAmp = 0.55f;
      spin = 0.12f;
      lateral = 0.15f;
    } else if (s.kind == FlyingKind::SkyMote) {
      bobAmp = 0.9f;
      spin = 0.08f;
      lateral = 0.35f;
    }
    bobAmp *= (0.85f + score * 0.45f);
    spin *= speedMul;
    a.position.y += std::sin(timeSec * (0.7f + speedMul * 0.4f) + phase) * bobAmp;
    a.position.x += std::sin(timeSec * 0.35f + phase * 1.7f) * lateral;
    a.position.z += std::cos(timeSec * 0.28f + phase * 1.3f) * lateral;
    a.yaw += timeSec * spin + phase * 0.1f;
    out.push_back(a);
  }
}

void FlyingGenerator::harvestParticles(std::vector<FlyingParticleSpawn>& out, float dt,
                                       const SprintCore& sprint, const glm::vec3& playerPos,
                                       float groundY, const std::vector<FoliageInstance>& flying,
                                       const std::vector<RuinInstance>& ruins) {
  const float score = std::clamp(sprint.score, 0.f, 1.4f);
  const float speed = sprint.speed;
  const bool sprinting = sprint.sprinting;

  // —— Star Moss spores (disturbance trails) ——
  if (sprinting && speed > 8.f) {
    const float rate = 2.5f + score * 7.f + speed * 0.04f;
    sporeAccum_ += dt * rate;
    while (sporeAccum_ >= 1.f && out.size() < 64) {
      sporeAccum_ -= 1.f;
      const float rx = (hash3(playerPos.x + sporeAccum_, playerPos.z, 1.f) - 0.5f) * 2.2f;
      const float rz = (hash3(playerPos.x, playerPos.z + sporeAccum_, 2.f) - 0.5f) * 2.2f;
      FlyingParticleSpawn p;
      p.pos = glm::vec3(playerPos.x + rx, groundY + 0.1f, playerPos.z + rz);
      p.vel = glm::vec3(rx * 0.25f, 0.85f + score * 0.7f, rz * 0.25f);
      p.color = glm::vec3(0.35f, 0.92f, 0.88f);
      p.size = 0.07f + score * 0.05f;
      p.life = 0.65f + score * 0.35f;
      p.particleKind = 0;
      out.push_back(p);
    }
  } else {
    sporeAccum_ *= 0.9f;
  }

  // —— Motions around nearby flyers ——
  flyerAccum_ += dt * (3.f + score * 8.f);
  while (flyerAccum_ >= 1.f && out.size() < 80) {
    flyerAccum_ -= 1.f;
    if (flying.empty()) break;
    // Pick a few near player
    for (int attempt = 0; attempt < 5 && out.size() < 80; ++attempt) {
      const size_t i = static_cast<size_t>(
          hash3(playerPos.x, playerPos.z, flyerAccum_ + float(attempt)) * flying.size());
      const auto& f = flying[i % flying.size()];
      const float dx = f.position.x - playerPos.x;
      const float dz = f.position.z - playerPos.z;
      if (dx * dx + dz * dz > 42.f * 42.f) continue;

      FlyingParticleSpawn p;
      p.pos = f.position;
      const float ang = hash3(f.position.x, f.position.z, 3.f) * 6.28318f;
      if (f.kind == FlyingKind::SkyMote) {
        // Distant streamlet
        p.vel = glm::vec3(std::cos(ang) * 0.6f, 0.2f, std::sin(ang) * 0.6f);
        p.color = glm::vec3(0.55f, 0.45f, 0.95f);
        p.size = 0.12f;
        p.life = 0.8f;
        p.particleKind = 3;
      } else {
        p.vel = glm::vec3(std::cos(ang) * 0.35f, 0.4f + score * 0.3f, std::sin(ang) * 0.35f);
        p.color = glm::vec3(0.45f, 0.85f, 1.1f);
        p.size = 0.08f + score * 0.04f;
        p.life = 0.45f;
        p.particleKind = 2;
      }
      out.push_back(p);
    }
  }

  // —— Energy wisps near ruins (high score) ——
  if (score > 0.5f) {
    wispAccum_ += dt * (1.2f + score * 3.5f);
    if (wispAccum_ >= 1.f && out.size() < 90) {
      wispAccum_ = 0.f;
      for (const auto& r : ruins) {
        const float dx = r.position.x - playerPos.x;
        const float dz = r.position.z - playerPos.z;
        if (dx * dx + dz * dz > 60.f * 60.f) continue;
        // Arc of sparks — temporary symbol / ring
        const int N = 6 + static_cast<int>(score * 8.f);
        const float base = hash3(r.position.x, r.position.z, score) * 6.28318f + score * 2.f;
        for (int k = 0; k < N && out.size() < 96; ++k) {
          const float a = base + static_cast<float>(k) / static_cast<float>(N) * 6.28318f;
          const float rad = 2.0f + r.scale * 0.1f;
          const float yOff = 1.1f + std::sin(a * 2.f) * 0.6f + std::cos(a * 3.f) * 0.25f;
          FlyingParticleSpawn p;
          p.pos = r.position + glm::vec3(std::cos(a) * rad, yOff, std::sin(a) * rad);
          p.vel = glm::vec3(-std::sin(a) * 0.7f, 0.15f, std::cos(a) * 0.7f);
          p.color = glm::vec3(0.75f, 0.5f, 1.0f);
          p.size = 0.1f;
          p.life = 0.5f + score * 0.2f;
          p.particleKind = 3;
          out.push_back(p);
        }
        break;
      }
    }
  } else {
    wispAccum_ *= 0.95f;
  }

  // —— High-sprint crystal swarm burst (micro flux-like) ——
  if (score > 0.95f && sprinting && speed > 16.f) {
    swarmAccum_ += dt * 4.f;
    if (swarmAccum_ >= 1.f && out.size() < 100) {
      swarmAccum_ = 0.f;
      for (int i = 0; i < 10 && out.size() < 100; ++i) {
        const float a = static_cast<float>(i) / 10.f * 6.28318f;
        FlyingParticleSpawn p;
        p.pos = playerPos + glm::vec3(std::cos(a) * 2.5f, 1.2f + hash3(a, score, 1.f),
                                      std::sin(a) * 2.5f);
        p.vel = glm::vec3(std::cos(a + 1.2f) * 3.f, 1.5f, std::sin(a + 1.2f) * 3.f);
        p.color = glm::vec3(0.5f, 0.95f, 1.15f);
        p.size = 0.14f;
        p.life = 0.4f;
        p.particleKind = 2;
        out.push_back(p);
      }
    }
  }
}

void FlyingGenerator::lodCull(std::vector<FoliageInstance>& list, const glm::vec3& player,
                              size_t maxKeep) {
  if (list.size() <= maxKeep) return;
  std::partial_sort(list.begin(), list.begin() + static_cast<std::ptrdiff_t>(maxKeep), list.end(),
                    [&](const FoliageInstance& a, const FoliageInstance& b) {
                      const float da = (a.position.x - player.x) * (a.position.x - player.x) +
                                       (a.position.z - player.z) * (a.position.z - player.z);
                      const float db = (b.position.x - player.x) * (b.position.x - player.x) +
                                       (b.position.z - player.z) * (b.position.z - player.z);
                      return da < db;
                    });
  list.resize(maxKeep);
}

} // namespace bolt
