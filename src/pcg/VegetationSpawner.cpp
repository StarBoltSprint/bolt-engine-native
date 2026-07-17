#include "bolt/pcg/VegetationSpawner.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace bolt {
namespace {

float hash2(float x, float z) {
  const float s = std::sin(x * 127.1f + z * 311.7f) * 43758.5453f;
  return s - std::floor(s);
}

float fbm(float x, float z, int oct) {
  float v = 0.f, a = 0.5f, f = 1.f;
  for (int i = 0; i < oct; ++i) {
    const float n = hash2(std::floor(x * f), std::floor(z * f));
    v += a * n;
    a *= 0.5f;
    f *= 2.05f;
  }
  return v;
}

float smoothstep(float e0, float e1, float x) {
  const float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
  return t * t * (3.f - 2.f * t);
}

} // namespace

float VegetationSpawner::densityAt(float x, float z, float sprintScore) const {
  const float score = std::clamp(sprintScore, 0.f, 1.4f);
  const float strength = 5.5f * (0.75f + score * 0.65f);
  const float px = x * 0.008f;
  const float pz = z * 0.008f;
  const int oct = 3;
  const float q1 = fbm(px, pz, oct);
  const float q2 = fbm(px + 5.2f, pz + 1.3f, oct);
  const float r1 = fbm(px + strength * q1 + 1.7f, pz + strength * q2 + 9.2f, oct);
  const float r2 = fbm(px + strength * q1 + 8.3f, pz + strength * q2 + 2.8f, oct);
  float d = fbm(px + strength * r1, pz + strength * r2, oct);
  // Second macro layer — clearings vs thickets
  const float macro = fbm(x * 0.0025f + 3.f, z * 0.0025f + 7.f, 2);
  d = smoothstep(-0.02f, 0.72f, d) * smoothstep(0.15f, 0.85f, macro + 0.25f);
  return std::min(1.f, d * (0.92f + score * 0.12f));
}

std::vector<FoliageInstance> VegetationSpawner::generate(
    const SprintCore& sprint, const SpawnRules& rules, const HeightField& height,
    const SpawnBudgets& budgets, int maxNew) const {
  std::vector<FoliageInstance> out;
  if (maxNew <= 0) return out;

  const glm::vec3 pred = sprint.predictedPosition(2.8f + sprint.score * 2.f);
  const float yaw = sprint.yaw;
  const float fx = std::sin(yaw);
  const float fz = std::cos(yaw);
  const float rx = std::cos(yaw);
  const float rz = -std::sin(yaw);

  std::mt19937 rng{static_cast<std::uint32_t>(std::hash<float>{}(pred.x * 12.1f) ^
                                               std::hash<float>{}(pred.z * 7.7f) ^
                                               std::hash<float>{}(sprint.score * 100.f))};
  std::uniform_real_distribution<float> U(0.f, 1.f);

  int tries = maxNew * 8;
  while (static_cast<int>(out.size()) < maxNew && tries-- > 0) {
    // Mixed rings: near flanks + far thicket
    const float ring = U(rng);
    const float sideSign = U(rng) > 0.5f ? 1.f : -1.f;
    float ang, r;
    if (ring < 0.45f) {
      // Close flank corridor edge
      ang = sideSign * (3.14159265f * 0.5f + (U(rng) - 0.5f) * 0.35f);
      r = rules.clear().vegMin + U(rng) * (28.f + sprint.score * 18.f);
    } else if (ring < 0.8f) {
      // Mid field
      ang = sideSign * (0.9f + U(rng) * 0.7f);
      r = 40.f + U(rng) * (50.f + sprint.score * 30.f);
    } else {
      // Far sparse drama
      ang = sideSign * (0.6f + U(rng) * 1.0f);
      r = 70.f + U(rng) * 55.f;
    }
    const float localFwd = std::cos(ang) * r;
    const float localSide = std::sin(ang) * r;
    glm::vec3 p;
    p.x = pred.x + fx * localFwd + rx * localSide;
    p.z = pred.z + fz * localFwd + rz * localSide;
    p.y = 0.f;

    if (!rules.canSpawnSolid(p, sprint.position, yaw)) continue;

    const float dens = densityAt(p.x, p.z, sprint.score);
    if (dens < 0.22f) continue;

    p.y = height.sample(p.x, p.z, sprint.score);
    const float slope = 1.f - std::abs(height.normal(p.x, p.z).y);

    FoliageInstance inst;
    inst.position = p;
    // Scale clusters by density + micro hash
    const float micro = hash2(p.x * 0.4f, p.z * 0.4f);
    inst.scale = 0.4f + dens * 0.9f + micro * 0.45f + U(rng) * 0.2f;
    if (slope > 0.35f) inst.scale *= 0.75f; // shorter on cliffs
    inst.yaw = U(rng) * 6.28318f;

    // Kind palette — more variety:
    // 0 stalk, 1 flower, 2 crystal cluster, 3 bush, 4 tall crystal, 5 floater shard
    const float k = U(rng);
    if (dens > 0.7f && k < 0.25f)
      inst.kind = 4u; // tall crystal in thickets
    else if (dens > 0.55f && k < 0.45f)
      inst.kind = 2u; // crystal
    else if (k < 0.2f)
      inst.kind = 3u; // bush
    else if (k < 0.35f)
      inst.kind = 1u; // flower
    else if (k < 0.42f && dens < 0.5f)
      inst.kind = 5u; // floater / small
    else
      inst.kind = 0u; // stalk

    // Occasional twin offset for clusters
    out.push_back(inst);
    if (inst.kind == 2u && U(rng) < 0.35f && static_cast<int>(out.size()) < maxNew) {
      FoliageInstance twin = inst;
      twin.position.x += (U(rng) - 0.5f) * 1.2f;
      twin.position.z += (U(rng) - 0.5f) * 1.2f;
      twin.position.y = height.sample(twin.position.x, twin.position.z, sprint.score);
      twin.scale *= 0.7f + U(rng) * 0.3f;
      twin.yaw = U(rng) * 6.28318f;
      twin.kind = U(rng) > 0.5f ? 0u : 2u;
      out.push_back(twin);
    }
  }

  const std::size_t cap = static_cast<std::size_t>(
      std::max(1, static_cast<int>(budgets.vegSpawnsPerTick * budgets.densityMul * 12)));
  if (out.size() > cap) out.resize(cap);
  return out;
}

} // namespace bolt
