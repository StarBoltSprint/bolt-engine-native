#pragma once
#include <glm/glm.hpp>

namespace bolt {

/**
 * Meaningful Sprint Score — port of Three.js MeaningfulSprintScore spirit.
 * Drives PCG density, budgets, LOD, warp, particles.
 */
struct SprintCore {
  float speed = 0.f;          // horizontal u/s
  float momentum = 0.f;       // 0..1 builds while sprinting
  float intention = 0.f;      // 0..1 toward landmarks (later)
  float resonance = 0.f;      // 0..1
  bool  sprinting = false;

  float score = 0.f;          // 0..1+ meaningful density driver
  float gateThreshold = 0.65f;
  bool  gateOpen = false;

  glm::vec3 position{0.f};
  glm::vec3 velocity{0.f};
  float yaw = 0.f;

  /** Predicted world position for PCG (seconds ahead scales with speed). */
  glm::vec3 predictedPosition(float horizonSec) const;

  void update(float dt);

  bool isGateOpen() const { return gateOpen; }
};

} // namespace bolt
