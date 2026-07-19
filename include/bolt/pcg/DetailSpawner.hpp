#pragma once
#include "bolt/pcg/VegetationSpawner.hpp"
#include "bolt/sprint/SpawnBudgets.hpp"
#include "bolt/sprint/SprintCore.hpp"
#include "bolt/world/HeightField.hpp"
#include "bolt/world/SpawnRules.hpp"
#include <vector>

namespace bolt {

/**
 * Detail kinds for Crystal Nebula Plains finishing layer.
 * GPU passes these as foliage instance kind (detail mesh batch).
 * High numbers avoid clash with veg (0–5) and ruins (10–13).
 */
namespace DetailKind {
constexpr std::uint32_t Shard = 20;   // small ground crystal / pebble
constexpr std::uint32_t Cluster = 21; // glowing crystal cluster
constexpr std::uint32_t Vent = 22;    // energy vent (particle source)
constexpr std::uint32_t Float = 23;   // floating crystal shard
constexpr std::uint32_t Rune = 24;    // subtle narrative rune stone
} // namespace DetailKind

/**
 * DetailGenerator — atmospheric & storytelling micro-props.
 *
 * - Glowing crystal clusters (pulse / proximity shards in Application)
 * - Energy vents (particle streams, stronger at high sprint)
 * - Floating shards (airborne, near energy / ruins)
 * - Ground shards + rune stones
 * - Sprint-scaled density: sparse low → rich high
 *
 * Reuses FoliageInstance layout (kind = DetailKind::*).
 */
class DetailSpawner {
public:
  std::vector<FoliageInstance> generate(const SprintCore& sprint, const SpawnRules& rules,
                                        const HeightField& height, const SpawnBudgets& budgets,
                                        int maxNew) const;

  /** Sample details inside one chunk AABB; CLEAR vs real player. */
  std::vector<FoliageInstance> generateInChunk(int cx, int cz, float chunkSize,
                                               const SprintCore& sprint, const SpawnRules& rules,
                                               const HeightField& height,
                                               const SpawnBudgets& budgets, int maxNew) const;

  /**
   * Narrative / energy details around a ruin plaza (floaters + runes).
   * Call after plaza clear so storytelling remains near landmarks.
   */
  std::vector<FoliageInstance> generateNearLandmark(const glm::vec3& landmark, float plazaR,
                                                    const SprintCore& sprint,
                                                    const HeightField& height, int maxNew) const;
};

} // namespace bolt
