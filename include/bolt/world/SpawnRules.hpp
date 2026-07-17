#pragma once
#include <glm/glm.hpp>
#include "bolt/sprint/SpawnBudgets.hpp"

namespace bolt {

class SpawnRules {
public:
  explicit SpawnRules(ClearRules clear = {}) : clear_(clear) {}

  bool tooCloseToPlayer(const glm::vec3& pos, const glm::vec3& player, float minR) const;
  bool inRunCorridor(const glm::vec3& pos, const glm::vec3& player, float yaw) const;

  /** true if solids (trees/rocks) may spawn here */
  bool canSpawnSolid(const glm::vec3& pos, const glm::vec3& player, float yaw) const;

  const ClearRules& clear() const { return clear_; }

private:
  ClearRules clear_;
};

} // namespace bolt
