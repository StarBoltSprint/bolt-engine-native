#pragma once
#include "bolt/pcg/DetailSpawner.hpp"
#include "bolt/pcg/RuinGenerator.hpp"
#include "bolt/pcg/VegetationSpawner.hpp"
#include "bolt/sprint/SpawnBudgets.hpp"
#include "bolt/sprint/SprintCore.hpp"
#include "bolt/world/HeightField.hpp"
#include "bolt/world/SpawnRules.hpp"
#include "bolt/world/TerrainMesh.hpp"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace bolt {

struct WorldChunk {
  int cx = 0, cz = 0;
  std::vector<FoliageInstance> foliage;
  std::vector<FoliageInstance> details;
  std::vector<RuinInstance> ruins;
  float scoreBake = 0.f;
  bool loaded = false;
};

/**
 * OpenWorld-style chunk streaming around the player.
 * Chunk size meters; loadRadius in chunks (Chebyshev).
 * Regenerates content when sprint score drifts a lot.
 */
class WorldStreamer {
public:
  float chunkSize = 72.f;
  int loadRadius = 2; // 5×5 neighborhood

  void update(const SprintCore& sprint, const SpawnRules& rules, const HeightField& height,
              const SpawnBudgets& budgets, const VegetationSpawner& veg,
              const DetailSpawner& detail, const RuinGenerator& ruins);

  /** Snap terrain origin to chunk grid under player */
  void terrainOrigin(float playerX, float playerZ, float& outX, float& outZ) const;

  /** Gather all loaded instances (for GPU upload) */
  void gatherFoliage(std::vector<FoliageInstance>& out) const;
  void gatherDetails(std::vector<FoliageInstance>& out) const;
  void gatherRuins(std::vector<RuinInstance>& out) const;

  int loadedChunkCount() const { return static_cast<int>(chunks_.size()); }

private:
  static std::uint64_t key(int cx, int cz) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) << 32) |
           static_cast<std::uint32_t>(cz);
  }
  void loadChunk(int cx, int cz, const SprintCore& sprint, const SpawnRules& rules,
                 const HeightField& height, const SpawnBudgets& budgets,
                 const VegetationSpawner& veg, const DetailSpawner& detail,
                 const RuinGenerator& ruins);

  std::unordered_map<std::uint64_t, WorldChunk> chunks_;
  int lastCx_ = 0x7fffffff, lastCz_ = 0x7fffffff;
};

} // namespace bolt
