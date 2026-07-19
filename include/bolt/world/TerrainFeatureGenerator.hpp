#pragma once
#include <glm/glm.hpp>
#include "bolt/sprint/SprintCore.hpp"

namespace bolt {

/** Landform tags for spawn bias (moss / trees / ruins). */
enum class TerrainTag : int {
  Open = 0,      // gentle flats / soft slopes
  Valley = 1,    // low basins — thicker moss, denser small flora
  Ridge = 2,     // elevated spines — good for ruins / sparse trees
  CraterFloor = 3, // depression center — moss pools, small crystals
  RockShelf = 4, // outcrop — rock blend, cliff cling crystals
  Drama = 5,     // sprint look-ahead peaks / cliffs
};

/**
 * TerrainFeatureGenerator — Crystal Nebula Plains landforms.
 * Stamps craters, ridges, rock mounds, crystal veins on top of HeightField fBm.
 * Sprint look-ahead amplifies drama in Bolt's predicted path.
 */
class TerrainFeatureGenerator {
public:
  /** Call each frame (or on sprint change) so look-ahead drama tracks Bolt. */
  void updateLookAhead(const SprintCore& sprint);

  /** Extra height (meters) from discrete features + look-ahead boost. */
  float heightOffset(float x, float z, float sprintScore) const;

  /** Classify landform at XZ for vegetation / ruins / moss rules. */
  TerrainTag classify(float x, float z, float sprintScore) const;

  /** 0.5..1.6 — multiply vegetation density (valleys higher, ridges lower). */
  float vegetationDensityMul(float x, float z, float sprintScore) const;

  /** 0..1 — ruins prefer ridges / crater rims / drama. */
  float ruinSuitability(float x, float z, float sprintScore) const;

  /** Detail shards / moss favor valleys and crater floors. */
  float detailDensityMul(float x, float z, float sprintScore) const;

  bool lookAheadActive() const { return lookActive_; }

private:
  // Deterministic stamp fields (no storage — pure functions of x,z)
  float craterOffset(float x, float z, float score) const;
  float ridgeOffset(float x, float z, float score) const;
  float rockMoundOffset(float x, float z, float score) const;
  float veinOffset(float x, float z, float score) const;
  float lookAheadOffset(float x, float z, float score) const;

  float lookOriginX_ = 0.f;
  float lookOriginZ_ = 0.f;
  float lookYaw_ = 0.f;
  float lookScore_ = 0.f;
  float lookSpeed_ = 0.f;
  bool lookActive_ = false;
};

} // namespace bolt
