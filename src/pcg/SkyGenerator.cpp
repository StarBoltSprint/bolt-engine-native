#include "bolt/pcg/SkyGenerator.hpp"
#include <algorithm>
#include <cmath>

namespace bolt {

void SkyGenerator::update(float dt, const SprintCore& sprint) {
  const float score = std::clamp(sprint.score, 0.f, 1.4f);
  const float speed = std::clamp(sprint.speed / 40.f, 0.f, 1.f);
  const bool sprinting = sprint.sprinting;
  const float gate = score + (sprinting ? 0.15f + speed * 0.2f : 0.f);

  phase_ += dt * (0.05f + score * 0.12f);
  state_.sunDir = glm::normalize(glm::vec3(0.32f, 0.52f + score * 0.08f, 0.42f));

  // Low → calm; high → dramatic
  state_.nebulaSpeed = 0.06f + score * 0.22f + (sprinting ? speed * 0.08f : 0.f);
  state_.nebulaBright = 0.72f + score * 0.55f + (sprinting ? 0.08f : 0.f);
  state_.swirl = 0.28f + score * 0.55f;
  state_.godRayStrength = 0.32f + score * 0.55f + (sprinting ? speed * 0.2f : 0.f);
  state_.streamIntensity = 0.12f + score * 0.75f;
  state_.starVivid = 0.45f + score * 0.5f;
  state_.cyanShift = std::clamp(score * 0.55f + (sprinting ? 0.15f : 0.f), 0.f, 1.f);
  state_.aurora = score > 0.75f ? (score - 0.75f) * 2.2f + (sprinting ? 0.2f : 0.f) : 0.f;
  state_.horizonGlow = 0.55f + score * 0.45f;
  state_.gradeStrength = 0.4f + score * 0.55f + state_.aurora * 0.25f;

  // Soft pulse so sky never feels frozen
  const float breathe = 0.5f + 0.5f * std::sin(phase_ * 1.7f);
  state_.nebulaBright *= 0.92f + breathe * 0.08f;
  state_.streamIntensity *= 0.9f + breathe * 0.12f;

  // Clamp for weak hardware / overbright safety
  state_.godRayStrength = std::clamp(state_.godRayStrength, 0.2f, 1.35f);
  state_.nebulaBright = std::clamp(state_.nebulaBright, 0.5f, 1.6f);
  state_.aurora = std::clamp(state_.aurora, 0.f, 1.2f);
  (void)gate;
}

} // namespace bolt
