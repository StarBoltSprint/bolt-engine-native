#include "bolt/pcg/VegetationSpawner.hpp"
#include "bolt/world/TerrainFeatureGenerator.hpp"
#include "bolt/world/TerrainMesh.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace bolt {
namespace {

float hash2(float x, float z) {
  const float s = std::sin(x * 127.1f + z * 311.7f) * 43758.5453f;
  return s - std::floor(s);
}

float hash2c(float x, float z) {
  // Continuous-ish hash for domain warp (value noise sample at floor)
  return hash2(std::floor(x), std::floor(z));
}

float valueNoise(float x, float z) {
  const float ix = std::floor(x), iz = std::floor(z);
  const float fx = x - ix, fz = z - iz;
  const float ux = fx * fx * (3.f - 2.f * fx);
  const float uz = fz * fz * (3.f - 2.f * fz);
  const float a = hash2c(ix, iz);
  const float b = hash2c(ix + 1.f, iz);
  const float c = hash2c(ix, iz + 1.f);
  const float d = hash2c(ix + 1.f, iz + 1.f);
  return a + (b - a) * ux + (c - a) * uz + (a - b - c + d) * ux * uz;
}

float fbm(float x, float z, int oct) {
  float v = 0.f, a = 0.5f, f = 1.f;
  for (int i = 0; i < oct; ++i) {
    v += a * valueNoise(x * f, z * f);
    a *= 0.5f;
    f *= 2.05f;
  }
  return v;
}

float smoothstep(float e0, float e1, float x) {
  const float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
  return t * t * (3.f - 2.f * t);
}

/** Domain warp: push sample domain through noise so thickets flow organically. */
void domainWarp(float& x, float& z, float strength, float score) {
  const float s = strength * (0.85f + score * 0.55f);
  const float n1 = fbm(x * 0.7f + 1.3f, z * 0.7f + 2.1f, 3);
  const float n2 = fbm(x * 0.7f + 5.2f, z * 0.7f + 9.8f, 3);
  x += (n1 * 2.f - 1.f) * s;
  z += (n2 * 2.f - 1.f) * s;
}

/**
 * Kind palette for Crystal Nebula Plains:
 * 0 stalk/sapling spear, 1 glow flower/bud, 2 crystal cluster,
 * 3 crystal fern/bush, 4 crystal-infused tree (tall), 5 floating shard
 *
 * Biased away from pure cone-spam: prefer ferns / crystals / trees.
 */
std::uint32_t pickKind(float dens, float score, float slope, float u, float micro) {
  if (slope > 0.42f) {
    return u < 0.4f ? 2u : 0u; // cling crystals on cliffs
  }

  // Weighted bands — stalks are minority
  // trees: thickets + medium density
  if (dens > 0.42f && u < (0.22f + dens * 0.18f + score * 0.12f))
    return 4u;
  // crystal clusters
  if (u < (0.38f + dens * 0.12f + score * 0.08f))
    return 2u;
  // ferns / bushes
  if (u < (0.58f + dens * 0.1f))
    return 3u;
  // flowers
  if (u < (0.72f + micro * 0.05f))
    return 1u;
  // floaters when score high or open air
  if (u < (0.82f + score * 0.08f) && dens < 0.72f)
    return 5u;
  // residual thin spears (not the whole field)
  return 0u;
}

float scaleForKind(std::uint32_t kind, float dens, float score, float micro, float slope) {
  float sc = 0.45f + dens * 0.85f + micro * 0.4f;
  switch (kind) {
  case 4u: // crystal tree — hero scale, less cone
    sc = 1.35f + dens * 1.6f + score * 1.0f + micro * 0.55f;
    break;
  case 3u: // fern bush
    sc = 0.7f + dens * 0.85f + micro * 0.4f + score * 0.2f;
    break;
  case 2u: // crystal cluster — varied sizes
    sc = 0.65f + dens * 1.1f + score * 0.4f + micro * 0.45f;
    break;
  case 1u: // flower bud
    sc = 0.35f + dens * 0.4f + micro * 0.25f;
    break;
  case 5u: // floater
    sc = 0.4f + dens * 0.5f + score * 0.45f + micro * 0.3f;
    break;
  default: // stalk — keep modest so they don't dominate
    sc = 0.35f + dens * 0.55f + micro * 0.25f + score * 0.12f;
    break;
  }
  if (slope > 0.35f) sc *= 0.72f;
  return sc;
}

void placeInstance(std::vector<FoliageInstance>& out, const glm::vec3& p, float dens, float score,
                   float slope, float yaw, float u, const HeightField& height) {
  const float micro = hash2(p.x * 0.4f, p.z * 0.4f);
  FoliageInstance inst;
  inst.position = p;
  inst.kind = pickKind(dens, score, slope, u, micro);
  inst.scale = scaleForKind(inst.kind, dens, score, micro, slope);
  inst.yaw = yaw;
  inst.treeVariant = 0;

  // 10 Crystal Nebula tree species — pick by domain so neighborhoods share a type
  if (inst.kind == 4u) {
    const float h = hash2(std::floor(p.x * 0.07f) + 3.1f, std::floor(p.z * 0.07f) + 1.7f);
    const float h2 = hash2(p.x * 0.21f, p.z * 0.19f);
    // Bias: spears & ferns more common, halo/floating rarer
    float w = h * 0.65f + h2 * 0.35f + dens * 0.08f;
    int v = 0;
    if (w < 0.14f)
      v = 0; // Resonance Spear
    else if (w < 0.26f)
      v = 1; // Twisted Conduit
    else if (w < 0.38f)
      v = 2; // Nebula Cap
    else if (w < 0.48f)
      v = 3; // Amethyst Spire
    else if (w < 0.58f)
      v = 4; // Weeping Crystal
    else if (w < 0.66f)
      v = 5; // Floating Crown
    else if (w < 0.75f)
      v = 6; // Rooted Prism
    else if (w < 0.85f)
      v = 7; // Lumen Fern-Tree
    else if (w < 0.93f)
      v = 8; // Broken Sentinel
    else
      v = 9; // Halo Bloom
    inst.treeVariant = static_cast<std::uint32_t>(v);
    // Per-species scale flavor
    static const float kScaleMul[10] = {1.15f, 1.05f, 0.9f, 1.2f, 1.0f,
                                        1.1f,  0.95f, 1.0f, 0.92f, 1.08f};
    inst.scale *= kScaleMul[v];
  }

  // Floating shards lift into the Resonance air column
  if (inst.kind == 5u) {
    const float lift = 1.2f + dens * 1.8f + score * 2.2f + micro * 1.5f;
    inst.position.y += lift;
  }

  out.push_back(inst);

  // Crystal clusters + trees often grow as small families (domain clumps)
  if ((inst.kind == 2u || inst.kind == 4u) && out.size() < 8000) {
    const float twinChance = inst.kind == 4u ? (0.28f + score * 0.15f) : (0.4f + score * 0.1f);
    if (micro < twinChance) {
      FoliageInstance twin = inst;
      const float ox = (hash2(p.x + 1.f, p.z) - 0.5f) * (inst.kind == 4u ? 2.4f : 1.3f);
      const float oz = (hash2(p.x, p.z + 1.f) - 0.5f) * (inst.kind == 4u ? 2.4f : 1.3f);
      twin.position.x += ox;
      twin.position.z += oz;
      if (inst.kind != 5u)
        twin.position.y = height.sample(twin.position.x, twin.position.z, score);
      twin.scale *= 0.55f + micro * 0.35f;
      twin.yaw = yaw + 1.7f;
      if (inst.kind == 4u) {
        // Same species clump — small neighbors stay same tree type
        twin.kind = 4u;
        twin.treeVariant = inst.treeVariant;
      } else {
        twin.kind = micro > 0.45f ? 0u : 2u;
        twin.treeVariant = 0;
      }
      out.push_back(twin);
    }
  }
}

} // namespace

float VegetationSpawner::densityAt(float x, float z, float sprintScore) const {
  const float score = std::clamp(sprintScore, 0.f, 1.4f);

  // Domain-warped sample coords — flowing thickets vs clearings
  float wx = x * 0.009f;
  float wz = z * 0.009f;
  domainWarp(wx, wz, 0.85f + score * 0.55f, score);

  const float n = fbm(wx, wz, 4);
  // Macro biome: large clearings (path-friendly) vs crystal forests
  const float macro = fbm(x * 0.0022f + 3.1f, z * 0.0022f + 7.4f, 3);
  // Resonance veins — thin high-density corridors of crystal growth
  const float vein = fbm(x * 0.018f + 11.f, z * 0.018f + 4.f, 2);
  const float veinBand = smoothstep(0.55f, 0.78f, vein);

  float d = smoothstep(0.08f, 0.78f, n);
  d *= smoothstep(0.12f, 0.88f, macro + 0.22f);
  d = std::max(d, veinBand * (0.55f + score * 0.25f)); // veins always somewhat lush

  // Sprint makes forests denser and veins stronger (world "celebrates")
  const float scoreMul = 0.78f + score * 0.55f;
  return std::min(1.f, d * scoreMul);
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
  const float score = sprint.score;

  std::mt19937 rng{static_cast<std::uint32_t>(std::hash<float>{}(pred.x * 12.1f) ^
                                               std::hash<float>{}(pred.z * 7.7f) ^
                                               std::hash<float>{}(score * 100.f))};
  std::uniform_real_distribution<float> U(0.f, 1.f);

  // More attempts at higher sprint (denser world ahead)
  const int want = std::max(maxNew, static_cast<int>(maxNew * (0.9f + score * 0.6f)));
  int tries = want * 10;
  while (static_cast<int>(out.size()) < want && tries-- > 0) {
    const float ring = U(rng);
    const float sideSign = U(rng) > 0.5f ? 1.f : -1.f;
    float ang, r;
    if (ring < 0.4f) {
      ang = sideSign * (3.14159265f * 0.5f + (U(rng) - 0.5f) * 0.4f);
      r = rules.clear().vegMin + U(rng) * (32.f + score * 22.f);
    } else if (ring < 0.78f) {
      ang = sideSign * (0.85f + U(rng) * 0.85f);
      r = 38.f + U(rng) * (55.f + score * 35.f);
    } else {
      ang = sideSign * (0.55f + U(rng) * 1.1f);
      r = 75.f + U(rng) * (60.f + score * 40.f);
    }
    glm::vec3 p;
    p.x = pred.x + fx * (std::cos(ang) * r) + rx * (std::sin(ang) * r);
    p.z = pred.z + fz * (std::cos(ang) * r) + rz * (std::sin(ang) * r);
    p.y = 0.f;

    if (!rules.canSpawnSolid(p, sprint.position, yaw)) continue;

    const float dens = densityAt(p.x, p.z, score);
    // Threshold drops with sprint so forests bloom when you run well
    const float densMin = 0.20f - score * 0.06f;
    if (dens < densMin) continue;

    p.y = height.sample(p.x, p.z, score);
    const float slope = 1.f - std::abs(height.normal(p.x, p.z).y);
    placeInstance(out, p, dens, score, slope, U(rng) * 6.28318f, U(rng), height);
  }

  const std::size_t cap = static_cast<std::size_t>(
      std::max(8, static_cast<int>(budgets.vegSpawnsPerTick * budgets.densityMul * 18.f *
                                   (1.f + score * 0.5f))));
  if (out.size() > cap) out.resize(cap);
  return out;
}

std::vector<FoliageInstance> VegetationSpawner::generateInChunk(
    int cx, int cz, float chunkSize, const SprintCore& sprint, const SpawnRules& rules,
    const HeightField& height, const SpawnBudgets& budgets, int maxNew) const {
  std::vector<FoliageInstance> out;
  if (maxNew <= 0 || chunkSize <= 1.f) return out;

  const float x0 = cx * chunkSize;
  const float z0 = cz * chunkSize;
  const float x1 = x0 + chunkSize;
  const float z1 = z0 + chunkSize;
  const float yaw = sprint.yaw;
  const float score = sprint.score;

  // Deterministic per-chunk seed — stable until score bake jumps
  std::mt19937 rng{static_cast<std::uint32_t>(
      static_cast<std::uint32_t>(cx) * 73856093u ^ static_cast<std::uint32_t>(cz) * 19349663u ^
      static_cast<std::uint32_t>(score * 1000.f))};
  std::uniform_real_distribution<float> U(0.f, 1.f);

  // Dense thickets: fill most of maxNew (GPU cull drops off-screen)
  const float scoreMul = 0.85f + budgets.densityMul * 0.65f + score * 0.95f;
  const int target = std::max(48, static_cast<int>(maxNew * scoreMul));
  int tries = target * 10;

  // Stratified jittered grid for even coverage (less clumpy RNG only)
  const int grid = std::max(4, static_cast<int>(std::sqrt(static_cast<float>(target)) + 0.5f));
  const float cellW = chunkSize / static_cast<float>(grid);
  const float cellH = chunkSize / static_cast<float>(grid);

  auto tryPlace = [&](float px, float pz) {
    glm::vec3 p{px, 0.f, pz};
    if (!rules.canSpawnSolid(p, sprint.position, yaw)) return;
    if (rules.nearEnergyPath(p, 2.2f)) return; // hard path exclusion (trees/bushes)

    float dens = densityAt(p.x, p.z, score);
    // Terrain features: valleys / crater floors denser; ridges / rock sparser
    if (height.features()) dens *= height.features()->vegetationDensityMul(p.x, p.z, score);

    const float densMin = 0.08f - score * 0.035f;
    if (dens < densMin) return;

    // Accept more mid-density placements for thicker forests
    if (U(rng) > dens * (0.88f + score * 0.35f) + 0.08f) return;

    p.y = height.sample(p.x, p.z, score);
    const glm::vec3 n = heightNormalAt(height, p.x, p.z, score);
    const float slope = 1.f - std::abs(n.y);
    if (slope > 0.55f && dens < 0.5f) return; // bare cliffs stay open

    // Prefer trees on open/ridge edges; fewer on pure rock shelves
    if (height.features()) {
      const TerrainTag tag = height.features()->classify(p.x, p.z, score);
      if (tag == TerrainTag::RockShelf && U(rng) > 0.45f) return;
    }

    placeInstance(out, p, dens, score, slope, U(rng) * 6.28318f, U(rng), height);
  };

  // Pass 1: jittered grid (organic domain still filters via densityAt)
  for (int gz = 0; gz < grid && static_cast<int>(out.size()) < target; ++gz) {
    for (int gx = 0; gx < grid && static_cast<int>(out.size()) < target; ++gx) {
      const float jx = (static_cast<float>(gx) + U(rng)) * cellW;
      const float jz = (static_cast<float>(gz) + U(rng)) * cellH;
      tryPlace(x0 + jx, z0 + jz);
    }
  }

  // Pass 2: extra random fills for high-sprint forests
  while (static_cast<int>(out.size()) < target && tries-- > 0) {
    tryPlace(x0 + U(rng) * (x1 - x0), z0 + U(rng) * (z1 - z0));
  }

  // Keep twins inside chunk AABB
  out.erase(std::remove_if(out.begin(), out.end(),
                           [&](const FoliageInstance& f) {
                             return f.position.x < x0 || f.position.x >= x1 || f.position.z < z0 ||
                                    f.position.z >= z1;
                           }),
            out.end());

  return out;
}

} // namespace bolt
