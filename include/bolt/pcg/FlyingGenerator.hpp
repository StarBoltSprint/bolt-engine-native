#pragma once
#include "bolt/pcg/VegetationSpawner.hpp"
#include "bolt/pcg/RuinGenerator.hpp"
#include "bolt/sprint/SpawnBudgets.hpp"
#include "bolt/sprint/SprintCore.hpp"
#include "bolt/world/HeightField.hpp"
#include "bolt/world/SpawnRules.hpp"
#include <vector>

namespace bolt {

/**
 * Flying kinds — airborne instanced meshes (detail batch, high kind ids).
 * Complements DetailKind (20–24 ground layer).
 */
namespace FlyingKind {
constexpr std::uint32_t ShardTiny = 25;  // small translucent shard
constexpr std::uint32_t ShardMed = 26;   // medium floating crystal
constexpr std::uint32_t Debris = 27;     // levitating ruin / crystal debris
constexpr std::uint32_t SkyMote = 28;    // high sky energy mote
} // namespace FlyingKind

/** One-shot particle spawn request from FlyingGenerator atmosphere. */
struct FlyingParticleSpawn {
  glm::vec3 pos{0.f};
  glm::vec3 vel{0.f};
  glm::vec3 color{0.5f, 0.85f, 1.f};
  float size = 0.1f;
  float life = 0.5f;
  int particleKind = 2; // 0 dust, 2 crystal, 3 aura
};

/**
 * FlyingGenerator — air / sky living layer for Crystal Nebula Plains.
 *
 * - Floating crystal shards (tiny → medium)
 * - Levitating debris near ruins
 * - High sky motes / energy streams (sparse)
 * - Sprint-reactive density + motion
 * - GPU particle harvest (spores, wisps, trails) for Application
 *
 * Instanced meshes reuse the detail prop mesh; styled by kind in foliage.frag.
 */
class FlyingGenerator {
public:
  /** Ambient flying field inside one chunk. */
  std::vector<FoliageInstance> generateInChunk(int cx, int cz, float chunkSize,
                                               const SprintCore& sprint, const SpawnRules& rules,
                                               const HeightField& height,
                                               const SpawnBudgets& budgets, int maxNew) const;

  /** Extra levitating debris / shards around a ruin. */
  std::vector<FoliageInstance> generateNearRuin(const RuinInstance& ruin, const SprintCore& sprint,
                                                const HeightField& height, int maxNew) const;

  /**
   * Apply bob / spin / hover for pack-time (does not mutate source).
   * Returns GPU-ready instances with animated pose.
   */
  static void animateForRender(const std::vector<FoliageInstance>& src,
                               std::vector<FoliageInstance>& out, float timeSec, float score);

  /**
   * Spawn atmospheric particles: spores, wisps, floater motes, sky streamlets.
   * Call each frame; rate scales with sprint score / speed.
   */
  void harvestParticles(std::vector<FlyingParticleSpawn>& out, float dt, const SprintCore& sprint,
                        const glm::vec3& playerPos, float groundY,
                        const std::vector<FoliageInstance>& flying,
                        const std::vector<RuinInstance>& ruins);

  /** Distance LOD: keep nearest N by XZ to player. */
  static void lodCull(std::vector<FoliageInstance>& list, const glm::vec3& player, size_t maxKeep);

private:
  // Frame accumulators for particle rates
  mutable float sporeAccum_ = 0.f;
  mutable float flyerAccum_ = 0.f;
  mutable float wispAccum_ = 0.f;
  mutable float swarmAccum_ = 0.f;
};

} // namespace bolt
