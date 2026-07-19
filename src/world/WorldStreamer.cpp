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
                              const FlyingGenerator& flying, const RuinGenerator& ruins) {
  WorldChunk ch;
  ch.cx = cx;
  ch.cz = cz;
  ch.scoreBake = sprint.score;
  ch.loaded = true;

  const float scoreMul = 1.15f + sprint.score * 1.35f;
  const int vegN =
      std::max(110, static_cast<int>(budgets.vegSpawnsPerTick * 72.f * budgets.densityMul * scoreMul));
  const int detN =
      std::max(40, static_cast<int>(budgets.detailCap * 5.f * (1.f + sprint.score * 0.5f)));
  const int flyN =
      std::max(12, static_cast<int>(18.f + sprint.score * 36.f + budgets.densityMul * 8.f));
  const int ruiN = std::max(1, static_cast<int>(1 + sprint.score * 1.5f));

  ch.ruins = ruins.generateInChunk(cx, cz, chunkSize, sprint, rules, height, budgets, ruiN);
  ch.foliage = veg.generateInChunk(cx, cz, chunkSize, sprint, rules, height, budgets, vegN);
  const int detBoost =
      std::max(detN, static_cast<int>(detN * (1.1f + sprint.score * 0.85f)));
  ch.details = detail.generateInChunk(cx, cz, chunkSize, sprint, rules, height, budgets, detBoost);
  ch.flying = flying.generateInChunk(cx, cz, chunkSize, sprint, rules, height, budgets, flyN);

  constexpr float kRuinClearR = 18.f;
  if (!ch.ruins.empty()) {
    auto farFromRuins = [&](const glm::vec3& p) {
      for (const auto& r : ch.ruins) {
        const float dx = p.x - r.position.x;
        const float dz = p.z - r.position.z;
        const float rr = kRuinClearR * (0.85f + std::min(r.scale, 14.f) * 0.06f);
        if (dx * dx + dz * dz < rr * rr) return false;
      }
      return true;
    };
    ch.foliage.erase(std::remove_if(ch.foliage.begin(), ch.foliage.end(),
                                    [&](const FoliageInstance& f) {
                                      return !farFromRuins(f.position);
                                    }),
                     ch.foliage.end());
    // Ground details: keep runes/float narrative; clear clutter
    ch.details.erase(std::remove_if(ch.details.begin(), ch.details.end(),
                                    [&](const FoliageInstance& f) {
                                      if (f.kind == DetailKind::Float || f.kind == DetailKind::Rune)
                                        return false;
                                      return !farFromRuins(f.position);
                                    }),
                     ch.details.end());
    for (const auto& r : ch.ruins) {
      const float rr = kRuinClearR * (0.85f + std::min(r.scale, 14.f) * 0.06f);
      const int n = 3 + static_cast<int>(sprint.score * 4.f);
      auto extra = detail.generateNearLandmark(r.position, rr, sprint, height, n);
      ch.details.insert(ch.details.end(), extra.begin(), extra.end());
      // FlyingGenerator: levitating debris / shards around monument
      const int fn = 4 + static_cast<int>(sprint.score * 5.f);
      auto flyExtra = flying.generateNearRuin(r, sprint, height, fn);
      ch.flying.insert(ch.flying.end(), flyExtra.begin(), flyExtra.end());
    }
  }

  chunks_[key(cx, cz)] = std::move(ch);
}

bool WorldStreamer::update(const SprintCore& sprint, const SpawnRules& rules,
                           const HeightField& height, const SpawnBudgets& budgets,
                           const VegetationSpawner& veg, const DetailSpawner& detail,
                           const FlyingGenerator& flying, const RuinGenerator& ruins) {
  const float cs = chunkSize;
  const int pcx = static_cast<int>(std::floor(sprint.position.x / cs));
  const int pcz = static_cast<int>(std::floor(sprint.position.z / cs));
  bool dirty = false;

  std::vector<std::pair<int, int>> want;
  want.reserve(static_cast<size_t>((loadRadius * 2 + 1) * (loadRadius * 2 + 1)));
  for (int dz = -loadRadius; dz <= loadRadius; ++dz)
    for (int dx = -loadRadius; dx <= loadRadius; ++dx)
      want.emplace_back(pcx + dx, pcz + dz);

  for (auto it = chunks_.begin(); it != chunks_.end();) {
    const int cx = it->second.cx, cz = it->second.cz;
    if (std::abs(cx - pcx) > loadRadius + 1 || std::abs(cz - pcz) > loadRadius + 1) {
      it = chunks_.erase(it);
      dirty = true;
    } else {
      ++it;
    }
  }

  for (auto [cx, cz] : want) {
    const auto k = key(cx, cz);
    auto it = chunks_.find(k);
    if (it == chunks_.end()) {
      loadChunk(cx, cz, sprint, rules, height, budgets, veg, detail, flying, ruins);
      dirty = true;
    } else if (std::abs(it->second.scoreBake - sprint.score) > 0.55f) {
      loadChunk(cx, cz, sprint, rules, height, budgets, veg, detail, flying, ruins);
      dirty = true;
    }
  }

  lastCx_ = pcx;
  lastCz_ = pcz;
  return dirty;
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

void WorldStreamer::gatherFlying(std::vector<FoliageInstance>& out) const {
  out.clear();
  for (const auto& kv : chunks_) {
    out.insert(out.end(), kv.second.flying.begin(), kv.second.flying.end());
  }
}

void WorldStreamer::gatherRuins(std::vector<RuinInstance>& out) const {
  out.clear();
  for (const auto& kv : chunks_) {
    out.insert(out.end(), kv.second.ruins.begin(), kv.second.ruins.end());
  }
}

} // namespace bolt
