#include "bolt/world/WorldStreamer.hpp"
#include <algorithm>
#include <cmath>

namespace bolt {

void WorldStreamer::terrainOrigin(float playerX, float playerZ, float& outX, float& outZ) const {
  const float cs = chunkSize;
  outX = std::floor(playerX / cs + 0.5f) * cs;
  outZ = std::floor(playerZ / cs + 0.5f) * cs;
}

void WorldStreamer::loadChunk(int cx, int cz, const SprintCore& sprint, const SpawnRules& rules,
                              const HeightField& height, const SpawnBudgets& budgets,
                              const VegetationSpawner& veg, const DetailSpawner& detail,
                              const RuinGenerator& ruins) {
  WorldChunk ch;
  ch.cx = cx;
  ch.cz = cz;
  ch.scoreBake = sprint.score;
  ch.loaded = true;

  // Temporarily aim prediction at chunk center for local fill
  SprintCore local = sprint;
  const float cs = chunkSize;
  local.position = glm::vec3((cx + 0.5f) * cs, sprint.position.y, (cz + 0.5f) * cs);
  // Bias yaw toward real player so flanks still make sense
  local.yaw = sprint.yaw;

  const int vegN = std::max(6, budgets.vegSpawnsPerTick * 8);
  const int detN = std::max(8, budgets.detailCap * 2);
  const int ruiN = std::max(1, budgets.detailCap / 4);

  ch.foliage = veg.generate(local, rules, height, budgets, vegN);
  ch.details = detail.generate(local, rules, height, budgets, detN);
  ch.ruins = ruins.generate(local, rules, height, budgets, ruiN);

  // Filter to this chunk AABB (generators use prediction radius)
  const float x0 = cx * cs, z0 = cz * cs;
  const float x1 = x0 + cs, z1 = z0 + cs;
  auto inChunk = [&](const glm::vec3& p) {
    return p.x >= x0 && p.x < x1 && p.z >= z0 && p.z < z1;
  };
  ch.foliage.erase(std::remove_if(ch.foliage.begin(), ch.foliage.end(),
                                  [&](const FoliageInstance& f) { return !inChunk(f.position); }),
                   ch.foliage.end());
  ch.details.erase(std::remove_if(ch.details.begin(), ch.details.end(),
                                  [&](const FoliageInstance& f) { return !inChunk(f.position); }),
                   ch.details.end());
  ch.ruins.erase(std::remove_if(ch.ruins.begin(), ch.ruins.end(),
                                [&](const RuinInstance& r) { return !inChunk(r.position); }),
                 ch.ruins.end());

  chunks_[key(cx, cz)] = std::move(ch);
}

void WorldStreamer::update(const SprintCore& sprint, const SpawnRules& rules,
                           const HeightField& height, const SpawnBudgets& budgets,
                           const VegetationSpawner& veg, const DetailSpawner& detail,
                           const RuinGenerator& ruins) {
  const float cs = chunkSize;
  const int pcx = static_cast<int>(std::floor(sprint.position.x / cs));
  const int pcz = static_cast<int>(std::floor(sprint.position.z / cs));

  // Desired set
  std::vector<std::pair<int, int>> want;
  want.reserve(static_cast<size_t>((loadRadius * 2 + 1) * (loadRadius * 2 + 1)));
  for (int dz = -loadRadius; dz <= loadRadius; ++dz)
    for (int dx = -loadRadius; dx <= loadRadius; ++dx)
      want.emplace_back(pcx + dx, pcz + dz);

  // Unload far chunks
  for (auto it = chunks_.begin(); it != chunks_.end();) {
    const int cx = it->second.cx, cz = it->second.cz;
    if (std::abs(cx - pcx) > loadRadius + 1 || std::abs(cz - pcz) > loadRadius + 1)
      it = chunks_.erase(it);
    else
      ++it;
  }

  // Load missing / refresh if score jumped a lot
  for (auto [cx, cz] : want) {
    const auto k = key(cx, cz);
    auto it = chunks_.find(k);
    if (it == chunks_.end()) {
      loadChunk(cx, cz, sprint, rules, height, budgets, veg, detail, ruins);
    } else if (std::abs(it->second.scoreBake - sprint.score) > 0.35f) {
      // Resample density when sprint meaningfully changes
      loadChunk(cx, cz, sprint, rules, height, budgets, veg, detail, ruins);
    }
  }

  lastCx_ = pcx;
  lastCz_ = pcz;
}

void WorldStreamer::gatherFoliage(std::vector<FoliageInstance>& out) const {
  out.clear();
  for (const auto& kv : chunks_) {
    out.insert(out.end(), kv.second.foliage.begin(), kv.second.foliage.end());
  }
}

void WorldStreamer::gatherDetails(std::vector<FoliageInstance>& out) const {
  out.clear();
  for (const auto& kv : chunks_) {
    out.insert(out.end(), kv.second.details.begin(), kv.second.details.end());
  }
}

void WorldStreamer::gatherRuins(std::vector<RuinInstance>& out) const {
  out.clear();
  for (const auto& kv : chunks_) {
    out.insert(out.end(), kv.second.ruins.begin(), kv.second.ruins.end());
  }
}

} // namespace bolt
