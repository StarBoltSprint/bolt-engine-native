#pragma once
#include <glm/glm.hpp>
#include <vector>
#include "bolt/sprint/SpawnBudgets.hpp"
#include "bolt/pcg/PathGenerator.hpp"

namespace bolt {

class SpawnRules {
public:
  explicit SpawnRules(ClearRules clear = {}) : clear_(clear) {}

  bool tooCloseToPlayer(const glm::vec3& pos, const glm::vec3& player, float minR) const;
  bool inRunCorridor(const glm::vec3& pos, const glm::vec3& player, float yaw) const;

  /** true if solids (trees/rocks) may spawn here */
  bool canSpawnSolid(const glm::vec3& pos, const glm::vec3& player, float yaw) const;

  /** Energy path exclusion — no vegetation on path ribbons */
  void setPathNetwork(const PathNetwork& net) { pathNet_ = net; }
  void clearPathNetwork() { pathNet_ = {}; }
  bool nearEnergyPath(const glm::vec3& pos, float extraMargin = 1.5f) const;

  const ClearRules& clear() const { return clear_; }
  const PathNetwork& pathNetwork() const { return pathNet_; }

private:
  ClearRules clear_;
  PathNetwork pathNet_{};
};

} // namespace bolt
