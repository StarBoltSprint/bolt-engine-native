#pragma once
#include "bolt/sprint/SprintCore.hpp"
#include <glm/glm.hpp>

namespace bolt {

/**
 * Sprint-reactive sky atmosphere for Crystal Nebula Plains.
 * Drives procedural sky.frag + post god rays / grade (not a second sky mesh).
 */
struct SkyAtmosphere {
  float nebulaSpeed = 0.08f;     // cloud scroll multiplier
  float nebulaBright = 0.85f;    // cloud emission scale
  float swirl = 0.35f;           // rotational swirl strength
  float godRayStrength = 0.4f;   // post volumetric shafts
  float streamIntensity = 0.25f; // distant energy rivers
  float starVivid = 0.55f;       // star / fleck brightness
  float cyanShift = 0.0f;        // 0 purple → 1 cyan/teal
  float aurora = 0.0f;           // high-sprint aurora bands
  float horizonGlow = 0.7f;
  float gradeStrength = 0.5f;    // post color grade
  glm::vec3 sunDir{0.32f, 0.55f, 0.42f};
};

/**
 * SkyGenerator — dynamic twilight nebula sky controller.
 *
 * Visual layers (implemented in sky.frag using these params):
 *  1. Swirling purple/pink/teal nebula clouds
 *  2. Star field + crystal flecks
 *  3. Distant cyan energy streams
 *  4. Soft god-ray shafts + post god rays
 *  5. High-sprint aurora response
 *
 * Coordinates with FlyingGenerator (air shards) and post pipeline.
 */
class SkyGenerator {
public:
  void update(float dt, const SprintCore& sprint);

  const SkyAtmosphere& state() const { return state_; }

  /** Pack for FrameUBO.sprintScore_flags.w (sky energy 0..1.5). */
  float skyEnergyPack() const {
    return state_.nebulaBright * 0.45f + state_.streamIntensity * 0.25f + state_.aurora * 0.35f;
  }

private:
  SkyAtmosphere state_{};
  float phase_ = 0.f;
};

} // namespace bolt
