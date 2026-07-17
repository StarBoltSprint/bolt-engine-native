#pragma once
#include <glm/glm.hpp>
#include <string>

namespace bolt {

/**
 * Crystal Nebula height — layered fBm + domain warp (web openworld.js spirit).
 * CPU sampling for physics; GPU shader will mirror these constants.
 */
class HeightField {
public:
  float sample(float x, float z) const;
  glm::vec3 normal(float x, float z, float eps = 0.75f) const;

  /** Sprint score slightly lifts amplitude for NEW chunks only (caller bakes). */
  float sample(float x, float z, float sprintScore) const;

  std::string biomeId() const { return "crystalNebula"; }
};

} // namespace bolt
