#pragma once
#include <glm/glm.hpp>
#include <string>

namespace bolt {

class TerrainFeatureGenerator;

/**
 * Crystal Nebula height — layered fBm + domain warp + TerrainFeatureGenerator stamps.
 * CPU sampling for physics / mesh / spawn placement.
 */
class HeightField {
public:
  float sample(float x, float z) const;
  glm::vec3 normal(float x, float z, float eps = 0.75f) const;

  /** Sprint score lifts base amplitude; features add craters/ridges/look-ahead. */
  float sample(float x, float z, float sprintScore) const;

  /** Optional landform stamps (craters, ridges, rock mounds, sprint drama). */
  void setFeatures(const TerrainFeatureGenerator* features) { features_ = features; }
  const TerrainFeatureGenerator* features() const { return features_; }

  std::string biomeId() const { return "crystalNebula"; }

private:
  const TerrainFeatureGenerator* features_ = nullptr;
};

} // namespace bolt
