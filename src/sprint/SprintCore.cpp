#include "bolt/sprint/SprintCore.hpp"
#include <algorithm>
#include <cmath>

namespace bolt {

glm::vec3 SprintCore::predictedPosition(float horizonSec) const {
  glm::vec3 v = velocity;
  const float sp = glm::length(glm::vec2(v.x, v.z));
  if (sp > 0.5f) {
    v.x = velocity.x;
    v.z = velocity.z;
  } else {
    v = glm::vec3(std::sin(yaw), 0.f, std::cos(yaw)) * std::max(sp, 8.f);
  }
  const float h = horizonSec * (1.f + score * 0.85f);
  return position + v * h;
}

void SprintCore::update(float dt) {
  speed = glm::length(glm::vec2(velocity.x, velocity.z));

  if (sprinting && speed > 2.f) {
    momentum = std::min(1.f, momentum + dt * 0.35f);
  } else {
    momentum = std::max(0.f, momentum - dt * 0.45f);
  }

  // Meaningful blend; top sprint ~300–430 u/s with momentum
  const float speedF = std::clamp(speed / 280.f, 0.f, 1.25f);
  const float momF = momentum;
  const float intF = intention;
  const float resF = resonance * 0.35f;
  float s = speedF * 0.45f + momF * 0.35f + intF * 0.15f + resF;
  if (sprinting) s += 0.08f;
  score = std::clamp(s, 0.f, 1.35f);
  gateOpen = score >= gateThreshold;
}

} // namespace bolt
