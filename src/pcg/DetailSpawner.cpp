#include "bolt/pcg/DetailSpawner.hpp"
#include "bolt/world/TerrainFeatureGenerator.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace bolt {
namespace {

float hash2(float x, float z) {
  float s = std::sin(x * 53.1f + z * 91.7f) * 43758.5453f;
  return s - std::floor(s);
}

float hash3(float x, float z, float w) {
  float s = std::sin(x * 12.9898f + z * 78.233f + w * 37.719f) * 43758.5453f;
  return s - std::floor(s);
}

/** Pick detail kind from sprint score + local noise (design low/med/high). */
std::uint32_t pickKind(float score, float n, float n2) {
  // Low sprint: mostly shards, rare cluster
  if (score < 0.35f) {
    if (n > 0.88f) return DetailKind::Cluster;
    if (n > 0.78f && n2 > 0.6f) return DetailKind::Vent;
    return DetailKind::Shard;
  }
  // Medium: vents + clusters (air floaters owned by FlyingGenerator)
  if (score < 0.75f) {
    if (n > 0.68f) return DetailKind::Cluster;
    if (n > 0.45f) return DetailKind::Vent;
    if (n > 0.32f && n2 > 0.55f) return DetailKind::Rune;
    return DetailKind::Shard;
  }
  // High: ground mix — vents, clusters, runes (air = FlyingGenerator)
  if (n > 0.58f) return DetailKind::Cluster;
  if (n > 0.36f) return DetailKind::Vent;
  if (n > 0.22f) return DetailKind::Rune;
  return DetailKind::Shard;
}

FoliageInstance makeDetail(const glm::vec3& groundP, std::uint32_t kind, float score, float u1,
                           float u2, float clump) {
  FoliageInstance d;
  d.kind = kind;
  d.yaw = u1 * 6.28318f;
  d.treeVariant = 0;

  switch (kind) {
  case DetailKind::Cluster:
    d.position = groundP;
    d.scale = 0.55f + u2 * 0.7f + clump * 0.25f + score * 0.2f;
    break;
  case DetailKind::Vent:
    d.position = groundP;
    d.scale = 0.4f + u2 * 0.35f + score * 0.15f;
    break;
  case DetailKind::Float: {
    const float hover = 0.9f + u2 * 2.8f + score * 1.2f;
    d.position = groundP + glm::vec3(0.f, hover, 0.f);
    d.scale = 0.28f + u1 * 0.45f + score * 0.12f;
    d.treeVariant = static_cast<std::uint32_t>(std::clamp(hover * 10.f, 1.f, 255.f));
    break;
  }
  case DetailKind::Rune:
    d.position = groundP;
    d.scale = 0.35f + u2 * 0.4f;
    break;
  default: // Shard
    d.position = groundP;
    d.scale = 0.22f + u2 * 0.4f + clump * 0.15f;
    break;
  }
  return d;
}

} // namespace

std::vector<FoliageInstance> DetailSpawner::generate(const SprintCore& sprint,
                                                     const SpawnRules& rules,
                                                     const HeightField& height,
                                                     const SpawnBudgets& budgets,
                                                     int maxNew) const {
  // Legacy path: sample around prediction as a soft chunk-less fill
  std::vector<FoliageInstance> out;
  if (maxNew <= 0) return out;

  const glm::vec3 pred = sprint.predictedPosition(2.2f + sprint.score * 1.5f);
  const float yaw = sprint.yaw;
  const float fx = std::sin(yaw), fz = std::cos(yaw);
  const float rx = std::cos(yaw), rz = -std::sin(yaw);
  const float score = std::clamp(sprint.score, 0.f, 1.4f);

  std::mt19937 rng{static_cast<std::uint32_t>(
      std::hash<float>{}(pred.x * 3.1f + 9.f) ^ std::hash<float>{}(pred.z * 5.7f))};
  std::uniform_real_distribution<float> U(0.f, 1.f);

  const int target = std::max(4, static_cast<int>(maxNew * (0.7f + score * 0.9f)));
  int tries = target * 10;
  while (static_cast<int>(out.size()) < target && tries-- > 0) {
    const float sideSign = U(rng) > 0.5f ? 1.f : -1.f;
    const float along = (U(rng) - 0.35f) * 55.f;
    const float side =
        sideSign * (rules.clear().corridorHalf + 3.5f + U(rng) * (18.f + score * 12.f));
    glm::vec3 p;
    p.x = pred.x + fx * along + rx * side;
    p.z = pred.z + fz * along + rz * side;

    if (rules.tooCloseToPlayer(p, sprint.position, rules.clear().packClearSmall * 0.85f)) continue;
    if (rules.inRunCorridor(p, sprint.position, yaw)) continue;
    if (rules.nearEnergyPath(p, 1.5f)) continue;

    float clump = hash2(std::floor(p.x * 0.12f), std::floor(p.z * 0.12f));
    if (clump < 0.28f) continue;

    p.y = height.sample(p.x, p.z, score);
    const float n = U(rng);
    const float n2 = U(rng);
    const auto kind = pickKind(score, n, n2);
    out.push_back(makeDetail(p, kind, score, U(rng), U(rng), clump));
  }

  const int cap = std::max(6, budgets.detailCap * 4);
  if (static_cast<int>(out.size()) > cap) out.resize(static_cast<size_t>(cap));
  return out;
}

std::vector<FoliageInstance> DetailSpawner::generateInChunk(
    int cx, int cz, float chunkSize, const SprintCore& sprint, const SpawnRules& rules,
    const HeightField& height, const SpawnBudgets& budgets, int maxNew) const {
  std::vector<FoliageInstance> out;
  if (maxNew <= 0 || chunkSize <= 1.f) return out;

  const float x0 = cx * chunkSize;
  const float z0 = cz * chunkSize;
  const float x1 = x0 + chunkSize;
  const float z1 = z0 + chunkSize;
  const float yaw = sprint.yaw;
  const float score = std::clamp(sprint.score, 0.f, 1.4f);

  std::mt19937 rng{static_cast<std::uint32_t>(
      static_cast<std::uint32_t>(cx) * 83492791u ^ static_cast<std::uint32_t>(cz) * 2971215073u ^
      static_cast<std::uint32_t>(score * 1000.f) ^ 0xD17Au)};
  std::uniform_real_distribution<float> U(0.f, 1.f);

  // Sprint-reactive richness: low sparse → high dense
  const float densMul = 0.55f + score * 1.15f + budgets.densityMul * 0.35f;
  const int target = std::max(10, static_cast<int>(maxNew * densMul));
  int tries = target * 12;

  while (static_cast<int>(out.size()) < target && tries-- > 0) {
    glm::vec3 p;
    p.x = x0 + U(rng) * (x1 - x0);
    p.z = z0 + U(rng) * (z1 - z0);

    if (rules.tooCloseToPlayer(p, sprint.position, rules.clear().packClearSmall * 0.85f)) continue;
    if (rules.inRunCorridor(p, sprint.position, yaw)) continue;
    // Vents/shards clear of path; floaters can hover slightly closer to path energy
    const auto kindProbe = pickKind(score, U(rng), U(rng));
    const float pathMargin = (kindProbe == DetailKind::Float) ? 0.9f : 1.55f;
    if (rules.nearEnergyPath(p, pathMargin)) continue;

    float clump = hash2(std::floor(p.x * 0.12f), std::floor(p.z * 0.12f));
    if (height.features())
      clump *= height.features()->detailDensityMul(p.x, p.z, score);
    // Soft threshold: high score fills more
    const float thr = 0.32f - score * 0.12f;
    if (clump < thr) continue;

    p.y = height.sample(p.x, p.z, score);
    const float n = hash3(p.x, p.z, 1.7f);
    const float n2 = hash3(p.x, p.z, 9.3f);
    const auto kind = pickKind(score, n, n2);
    out.push_back(makeDetail(p, kind, score, U(rng), U(rng), clump));
  }

  const int cap = std::max(8, static_cast<int>(budgets.detailCap * (3.5f + score * 2.5f)));
  if (static_cast<int>(out.size()) > cap) out.resize(static_cast<size_t>(cap));
  return out;
}

std::vector<FoliageInstance> DetailSpawner::generateNearLandmark(const glm::vec3& landmark,
                                                                float plazaR,
                                                                const SprintCore& sprint,
                                                                const HeightField& height,
                                                                int maxNew) const {
  std::vector<FoliageInstance> out;
  if (maxNew <= 0) return out;
  const float score = std::clamp(sprint.score, 0.f, 1.4f);
  std::mt19937 rng{static_cast<std::uint32_t>(
      std::hash<float>{}(landmark.x * 17.f) ^ std::hash<float>{}(landmark.z * 31.f) ^ 0x52171u)};
  std::uniform_real_distribution<float> U(0.f, 1.f);

  const int n = std::max(2, maxNew);
  for (int i = 0; i < n; ++i) {
    const float ang = U(rng) * 6.28318f;
    // Ring just outside plaza
    const float rad = plazaR * (0.92f + U(rng) * 0.45f);
    glm::vec3 p;
    p.x = landmark.x + std::cos(ang) * rad;
    p.z = landmark.z + std::sin(ang) * rad;
    p.y = height.sample(p.x, p.z, score);

    const bool wantFloat = (i % 3 != 0) || score > 0.5f;
    const auto kind = wantFloat ? DetailKind::Float : DetailKind::Rune;
    auto d = makeDetail(p, kind, score, U(rng), U(rng), 0.7f);
    // Extra hover near ruins (energy field)
    if (kind == DetailKind::Float)
      d.position.y += 0.4f + score * 0.8f;
    out.push_back(d);
  }
  return out;
}

} // namespace bolt
