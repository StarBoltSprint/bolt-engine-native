#include "bolt/world/SpawnRules.hpp"
#include <cmath>

namespace bolt {

bool SpawnRules::tooCloseToPlayer(const glm::vec3& pos, const glm::vec3& player, float minR) const {
  const float dx = pos.x - player.x;
  const float dz = pos.z - player.z;
  return dx * dx + dz * dz < minR * minR;
}

bool SpawnRules::inRunCorridor(const glm::vec3& pos, const glm::vec3& player, float yaw) const {
  const float dx = pos.x - player.x;
  const float dz = pos.z - player.z;
  const float fx = std::sin(yaw);
  const float fz = std::cos(yaw);
  const float rx = std::cos(yaw);
  const float rz = -std::sin(yaw);
  const float along = dx * fx + dz * fz;
  const float side = dx * rx + dz * rz;
  if (along < -clear_.corridorBehind || along > clear_.corridorAhead) return false;
  return std::abs(side) < clear_.corridorHalf;
}

bool SpawnRules::canSpawnSolid(const glm::vec3& pos, const glm::vec3& player, float yaw) const {
  if (tooCloseToPlayer(pos, player, clear_.playerRadius)) return false;
  if (inRunCorridor(pos, player, yaw)) return false;
  if (nearEnergyPath(pos, 1.8f)) return false; // keep path corridor clear of trees
  return true;
}

bool SpawnRules::nearEnergyPath(const glm::vec3& pos, float extraMargin) const {
  return PathGenerator::nearPath(pos, pathNet_, extraMargin);
}

} // namespace bolt
