#include "bolt/pcg/RuinGenerator.hpp"
#include "bolt/world/TerrainFeatureGenerator.hpp"
#include "bolt/world/TerrainMesh.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace bolt {
namespace {

float hash2(float x, float z) {
  float s = std::sin(x * 91.7f + z * 47.3f) * 43758.5453f;
  return s - std::floor(s);
}

float fbm(float x, float z) {
  float v = 0.f, a = 0.5f, f = 1.f;
  for (int i = 0; i < 3; ++i) {
    v += a * hash2(std::floor(x * f), std::floor(z * f));
    a *= 0.5f;
    f *= 2.1f;
  }
  return v;
}

std::uint32_t pickRuinKind(float score, float u, float cell) {
  const float s = std::clamp(score, 0.f, 1.35f);
  // Even mix of grand types — not a field of mini pillars
  float wMonolith = 0.30f - s * 0.04f;
  float wArch = 0.28f + s * 0.02f;
  float wObs = 0.22f + s * 0.04f;
  float wTemple = 0.20f + s * 0.06f;
  const float sum = wMonolith + wArch + wObs + wTemple;
  wMonolith /= sum;
  wArch /= sum;
  wObs /= sum;
  float t = std::clamp(u + (cell - 0.5f) * 0.08f, 0.f, 0.999f);
  if (t < wMonolith) return 0u;
  t -= wMonolith;
  if (t < wArch) return 1u;
  t -= wArch;
  if (t < wObs) return 2u;
  return 3u;
}

/** Hero landmark scales — readable from far, not vegetation-sized. */
float scaleForKind(std::uint32_t kind, float score, float u, float micro) {
  const float s = std::clamp(score, 0.f, 1.35f);
  // Mesh unit ~3–4m tall; scale 6–14 ⇒ ~20–50m monuments
  switch (kind) {
  case 0u: // Resonance Monolith — towering
    return 8.5f + u * 5.5f + s * 3.5f + micro * 1.2f;
  case 1u: // Floating Archway — huge span
    return 7.0f + u * 4.0f + s * 2.8f + micro * 1.0f;
  case 2u: // Crystal Observatory — wide landmark
    return 7.5f + u * 4.5f + s * 3.0f + micro * 1.1f;
  default: // Buried Temple — massive sunk mass
    return 9.0f + u * 5.0f + s * 3.2f + micro * 1.2f;
  }
}

void appendPrimary(std::vector<RuinInstance>& out, glm::vec3 p, std::uint32_t kind, float scale,
                   float yaw, float score, float u) {
  RuinInstance main;
  main.position = p;
  main.kind = kind;
  main.scale = scale;
  main.yaw = yaw;

  // Dramatic vertical placement — important, not planted like grass
  if (kind == 1u) {
    // Floating arch: clear air gap under keystone
    main.position.y += 1.2f + score * 1.8f + u * 1.0f;
  } else if (kind == 2u) {
    main.position.y += 0.25f + score * 0.4f;
  } else if (kind == 3u) {
    // Temple: slightly sunk into moss
    main.position.y -= 0.4f;
  } else {
    // Monolith: slight pedestal lift
    main.position.y += 0.15f;
  }

  out.push_back(main);
  // No satellite spam — one hero structure per site
}

} // namespace

std::vector<RuinInstance> RuinGenerator::generate(const SprintCore& sprint,
                                                  const SpawnRules& rules,
                                                  const HeightField& height,
                                                  const SpawnBudgets& budgets,
                                                  int maxNew) const {
  std::vector<RuinInstance> out;
  if (maxNew <= 0) return out;

  const glm::vec3 pred = sprint.predictedPosition(5.f + sprint.score * 4.f);
  const float yaw = sprint.yaw;
  const float fx = std::sin(yaw), fz = std::cos(yaw);
  const float rx = std::cos(yaw), rz = -std::sin(yaw);
  const float score = sprint.score;

  std::mt19937 rng{static_cast<std::uint32_t>(
      std::hash<float>{}(std::floor(pred.x / 48.f) * 17.f) ^
      std::hash<float>{}(std::floor(pred.z / 48.f) * 31.f) ^
      static_cast<std::uint32_t>(score * 40.f))};
  std::uniform_real_distribution<float> U(0.f, 1.f);

  // Few hero landmarks ahead on flanks
  const int want = std::max(1, std::min(maxNew, static_cast<int>(1 + score * 2.5f)));
  int tries = want * 20;
  while (static_cast<int>(out.size()) < want && tries-- > 0) {
    const float sideSign = U(rng) > 0.5f ? 1.f : -1.f;
    const float r = 55.f + U(rng) * (80.f + score * 55.f);
    const float along = (U(rng) - 0.1f) * 60.f;
    const float side = sideSign * (rules.clear().corridorHalf + 28.f + U(rng) * 50.f);
    glm::vec3 p;
    p.x = pred.x + fx * along + rx * side;
    p.z = pred.z + fz * along + rz * side;

    if (!rules.canSpawnSolid(p, sprint.position, yaw)) continue;
    if (rules.tooCloseToPlayer(p, sprint.position, rules.clear().forestMin * 0.9f)) continue;

    float cell = hash2(std::floor(p.x * 0.018f), std::floor(p.z * 0.018f));
    if (height.features()) {
      const float suit = height.features()->ruinSuitability(p.x, p.z, score);
      cell = cell * 0.45f + suit * 0.55f;
    }
    if (cell < 0.52f - score * 0.1f) continue;

    p.y = height.sample(p.x, p.z, score);
    if (heightNormalAt(height, p.x, p.z, score).y < 0.55f) continue;

    // Spacing between heroes
    bool near = false;
    for (const auto& e : out) {
      const float dx = e.position.x - p.x, dz = e.position.z - p.z;
      if (dx * dx + dz * dz < 55.f * 55.f) {
        near = true;
        break;
      }
    }
    if (near) continue;

    const float micro = fbm(p.x * 0.04f, p.z * 0.04f);
    const std::uint32_t kind = pickRuinKind(score, U(rng), cell);
    appendPrimary(out, p, kind, scaleForKind(kind, score, U(rng), micro), U(rng) * 6.28318f, score,
                  U(rng));
  }

  const int cap = std::max(1, static_cast<int>(2 + score * 3.f));
  if (static_cast<int>(out.size()) > cap) out.resize(static_cast<size_t>(cap));
  (void)budgets;
  return out;
}

std::vector<RuinInstance> RuinGenerator::generateInChunk(
    int cx, int cz, float chunkSize, const SprintCore& sprint, const SpawnRules& rules,
    const HeightField& height, const SpawnBudgets& budgets, int maxNew) const {
  std::vector<RuinInstance> out;
  if (maxNew <= 0 || chunkSize <= 1.f) return out;

  const float x0 = cx * chunkSize;
  const float z0 = cz * chunkSize;
  const float x1 = x0 + chunkSize;
  const float z1 = z0 + chunkSize;
  const float yaw = sprint.yaw;
  const float score = sprint.score;

  std::mt19937 rng{static_cast<std::uint32_t>(
      static_cast<std::uint32_t>(cx) * 2654435761u ^ static_cast<std::uint32_t>(cz) * 2246822519u ^
      static_cast<std::uint32_t>(score * 60.f))};
  std::uniform_real_distribution<float> U(0.f, 1.f);

  // 0–1 hero per chunk (2 at high sprint) — sparse landmarks
  const int sitesWanted = std::min(maxNew, (score > 0.75f ? 2 : 1));
  int tries = sitesWanted * 40;

  while (static_cast<int>(out.size()) < sitesWanted && tries-- > 0) {
    const float m = 0.18f; // keep away from chunk edges
    glm::vec3 p;
    p.x = x0 + chunkSize * (m + U(rng) * (1.f - 2.f * m));
    p.z = z0 + chunkSize * (m + U(rng) * (1.f - 2.f * m));

    if (!rules.canSpawnSolid(p, sprint.position, yaw)) continue;
    if (rules.tooCloseToPlayer(p, sprint.position, rules.clear().forestMin * 0.85f)) continue;

    // Large cells — one landmark region per ~35m
    float cell = hash2(std::floor(p.x * 0.016f), std::floor(p.z * 0.016f));
    // Prefer ridges / drama / rock shelves for hero ruins
    if (height.features()) {
      const float suit = height.features()->ruinSuitability(p.x, p.z, score);
      cell = cell * 0.45f + suit * 0.55f;
    }
    if (cell < 0.52f - score * 0.12f) continue;

    bool near = false;
    for (const auto& e : out) {
      const float dx = e.position.x - p.x, dz = e.position.z - p.z;
      if (dx * dx + dz * dz < 48.f * 48.f) {
        near = true;
        break;
      }
    }
    if (near) continue;

    p.y = height.sample(p.x, p.z, score);
    if (heightNormalAt(height, p.x, p.z, score).y < 0.55f) continue;

    const float micro = fbm(p.x * 0.04f, p.z * 0.04f);
    const std::uint32_t kind = pickRuinKind(score, U(rng), cell);
    appendPrimary(out, p, kind, scaleForKind(kind, score, U(rng), micro), U(rng) * 6.28318f, score,
                  U(rng));
  }

  (void)x1;
  (void)z1;
  (void)budgets;
  return out;
}

} // namespace bolt
