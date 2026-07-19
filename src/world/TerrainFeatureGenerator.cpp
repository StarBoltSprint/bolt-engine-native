#include "bolt/world/TerrainFeatureGenerator.hpp"
#include <algorithm>
#include <cmath>

namespace bolt {
namespace {

float hash2(float x, float z) {
  const float s = std::sin(x * 127.1f + z * 311.7f) * 43758.5453f;
  return s - std::floor(s);
}

float smoothstep(float e0, float e1, float x) {
  const float t = std::clamp((x - e0) / (e1 - e0 + 1e-6f), 0.f, 1.f);
  return t * t * (3.f - 2.f * t);
}

float distToSegment2(float px, float pz, float ax, float az, float bx, float bz) {
  const float abx = bx - ax, abz = bz - az;
  const float apx = px - ax, apz = pz - az;
  const float ab2 = abx * abx + abz * abz + 1e-6f;
  float t = (apx * abx + apz * abz) / ab2;
  t = std::clamp(t, 0.f, 1.f);
  const float qx = ax + abx * t - px;
  const float qz = az + abz * t - pz;
  return qx * qx + qz * qz;
}

} // namespace

void TerrainFeatureGenerator::updateLookAhead(const SprintCore& sprint) {
  lookOriginX_ = sprint.position.x;
  lookOriginZ_ = sprint.position.z;
  lookYaw_ = sprint.yaw;
  lookScore_ = std::clamp(sprint.score, 0.f, 1.4f);
  lookSpeed_ = sprint.speed;
  lookActive_ = true;
}

float TerrainFeatureGenerator::craterOffset(float x, float z, float score) const {
  // More frequent, deeper bowls + glowing rims — must read from a distance
  const float cell = 72.f;
  const float cx = std::floor(x / cell);
  const float cz = std::floor(z / cell);
  const float h = hash2(cx * 3.1f, cz * 7.7f);
  if (h < 0.28f - score * 0.1f) return 0.f;

  const float ox = (cx + 0.3f + hash2(cx, cz + 9.f) * 0.4f) * cell;
  const float oz = (cz + 0.3f + hash2(cx + 5.f, cz) * 0.4f) * cell;
  const float dx = x - ox, dz = z - oz;
  const float dist = std::sqrt(dx * dx + dz * dz);
  const float R = 18.f + hash2(cx + 1.f, cz + 2.f) * 22.f + score * 10.f;
  if (dist > R * 1.4f) return 0.f;

  const float t = dist / R;
  // Deep amphitheater bowl + sharp crystal rim
  const float bowl = -2.8f * (1.f - smoothstep(0.f, 0.5f, t));
  const float rim = 2.4f * std::exp(-std::pow((t - 0.7f) * 4.2f, 2.f));
  const float amp = (1.35f + score * 1.15f) * (0.85f + h * 0.55f);
  return (bowl + rim) * amp;
}

float TerrainFeatureGenerator::ridgeOffset(float x, float z, float score) const {
  // Tall crystal spines — visible wall-like landforms
  const float cell = 85.f;
  const float cx = std::floor(x / cell);
  const float cz = std::floor(z / cell);
  float sum = 0.f;

  for (int oz = -1; oz <= 1; ++oz) {
    for (int ox = -1; ox <= 1; ++ox) {
      const float ix = cx + static_cast<float>(ox);
      const float iz = cz + static_cast<float>(oz);
      const float h = hash2(ix * 2.3f, iz * 4.1f);
      if (h < 0.38f - score * 0.12f) continue;

      const float midX = (ix + 0.5f) * cell;
      const float midZ = (iz + 0.5f) * cell;
      const float ang = h * 6.28318f;
      const float halfLen = 32.f + h * 42.f + score * 18.f;
      const float ca = std::cos(ang), sa = std::sin(ang);
      const float ax = midX - ca * halfLen, az = midZ - sa * halfLen;
      const float bx = midX + ca * halfLen, bz = midZ + sa * halfLen;
      const float d2 = distToSegment2(x, z, ax, az, bx, bz);
      const float halfW = 5.5f + h * 4.5f;
      if (d2 > halfW * halfW * 5.f) continue;
      const float d = std::sqrt(d2);
      // Narrow sharp crest (cliffs on sides)
      const float profile = std::exp(-(d * d) / (halfW * halfW * 0.35f));
      const float crest = profile * profile * profile;
      sum += crest * (4.2f + score * 3.5f) * (0.75f + h * 0.7f);
    }
  }
  return sum;
}

float TerrainFeatureGenerator::rockMoundOffset(float x, float z, float score) const {
  // Crystal-infused boulders — tall enough to silhouette against moss
  const float cell = 36.f;
  const float cx = std::floor(x / cell);
  const float cz = std::floor(z / cell);
  float sum = 0.f;
  for (int oz = -1; oz <= 1; ++oz) {
    for (int ox = -1; ox <= 1; ++ox) {
      const float ix = cx + static_cast<float>(ox);
      const float iz = cz + static_cast<float>(oz);
      const float h = hash2(ix * 11.f, iz * 13.f);
      if (h < 0.48f - score * 0.14f) continue;
      const float mx = (ix + hash2(ix, iz + 3.f)) * cell;
      const float mz = (iz + hash2(ix + 2.f, iz)) * cell;
      const float dx = x - mx, dz = z - mz;
      const float d2 = dx * dx + dz * dz;
      const float R = 6.5f + h * 11.f + score * 4.5f;
      if (d2 > R * R) continue;
      const float t = 1.f - std::sqrt(d2) / R;
      // Steep crystal mound (almost pillar-like peak)
      const float mound = std::pow(std::max(t, 0.f), 1.35f);
      sum += mound * (2.8f + h * 3.2f + score * 1.8f);
    }
  }
  return sum;
}

float TerrainFeatureGenerator::veinOffset(float x, float z, float score) const {
  // Visible crystal veins breaking the surface
  const float n1 =
      std::sin(x * 0.038f + z * 0.018f + hash2(std::floor(x * 0.02f), std::floor(z * 0.02f)) * 6.f);
  const float n2 = std::sin(x * 0.018f - z * 0.042f);
  const float line = 1.f - std::abs(n1 * 0.7f + n2 * 0.3f);
  const float thin = std::pow(std::clamp(line, 0.f, 1.f), 5.5f);
  return thin * (0.85f + score * 0.95f);
}

float TerrainFeatureGenerator::lookAheadOffset(float x, float z, float score) const {
  if (!lookActive_) return 0.f;
  const float dx = x - lookOriginX_;
  const float dz = z - lookOriginZ_;
  const float dist = std::sqrt(dx * dx + dz * dz);
  if (dist < 28.f || dist > 180.f + lookScore_ * 50.f) return 0.f;

  const float fx = std::sin(lookYaw_);
  const float fz = std::cos(lookYaw_);
  const float along = dx * fx + dz * fz;
  if (along < 22.f) return 0.f;
  const float rx = std::cos(lookYaw_);
  const float rz = -std::sin(lookYaw_);
  const float side = std::abs(dx * rx + dz * rz);
  const float halfCone = 16.f + along * 0.25f + lookScore_ * 12.f;
  if (side > halfCone) return 0.f;

  const float alongN = smoothstep(28.f, 80.f, along) * (1.f - smoothstep(145.f, 185.f, along));
  const float sideN = 1.f - side / (halfCone + 1e-3f);
  const float cone = alongN * sideN * sideN;

  const float dramaNoise =
      hash2(std::floor(x * 0.07f), std::floor(z * 0.07f)) * 0.55f +
      hash2(x * 0.025f, z * 0.025f) * 0.45f;
  // Stronger peaks (world breathes ahead)
  const float signedDrama = (dramaNoise * 2.f - 0.7f);
  const float sprintMul =
      0.55f + lookScore_ * 2.0f + std::min(lookSpeed_ / 160.f, 1.1f);
  return cone * signedDrama * sprintMul * (2.2f + score * 1.2f);
}

float TerrainFeatureGenerator::heightOffset(float x, float z, float sprintScore) const {
  const float score = std::clamp(sprintScore, 0.f, 1.4f);
  float h = 0.f;
  h += craterOffset(x, z, score);
  h += ridgeOffset(x, z, score);
  h += rockMoundOffset(x, z, score);
  h += veinOffset(x, z, score);
  h += lookAheadOffset(x, z, score);
  return h;
}

TerrainTag TerrainFeatureGenerator::classify(float x, float z, float sprintScore) const {
  const float score = std::clamp(sprintScore, 0.f, 1.4f);
  const float crater = craterOffset(x, z, score);
  const float ridge = ridgeOffset(x, z, score);
  const float rock = rockMoundOffset(x, z, score);
  const float look = lookAheadOffset(x, z, score);
  const float vein = veinOffset(x, z, score);

  if (look > 1.2f) return TerrainTag::Drama;
  if (rock > 1.1f) return TerrainTag::RockShelf;
  if (crater < -1.0f) return TerrainTag::CraterFloor;
  if (ridge > 1.4f || (ridge > 0.8f && vein > 0.3f)) return TerrainTag::Ridge;
  if (crater < -0.35f || (ridge < 0.2f && rock < 0.25f && hash2(x * 0.02f, z * 0.02f) < 0.38f))
    return TerrainTag::Valley;
  return TerrainTag::Open;
}

float TerrainFeatureGenerator::vegetationDensityMul(float x, float z, float sprintScore) const {
  switch (classify(x, z, sprintScore)) {
  case TerrainTag::Valley:
  case TerrainTag::CraterFloor:
    return 1.55f;
  case TerrainTag::Open:
    return 1.05f;
  case TerrainTag::Ridge:
    return 0.65f;
  case TerrainTag::RockShelf:
    return 0.45f;
  case TerrainTag::Drama:
    return 0.75f;
  }
  return 1.f;
}

float TerrainFeatureGenerator::ruinSuitability(float x, float z, float sprintScore) const {
  switch (classify(x, z, sprintScore)) {
  case TerrainTag::Ridge:
    return 0.98f;
  case TerrainTag::Drama:
    return 0.92f;
  case TerrainTag::RockShelf:
    return 0.8f;
  case TerrainTag::CraterFloor:
    return 0.6f;
  case TerrainTag::Valley:
    return 0.22f;
  case TerrainTag::Open:
    return 0.38f;
  }
  return 0.35f;
}

float TerrainFeatureGenerator::detailDensityMul(float x, float z, float sprintScore) const {
  switch (classify(x, z, sprintScore)) {
  case TerrainTag::Valley:
  case TerrainTag::CraterFloor:
    return 1.7f;
  case TerrainTag::Open:
    return 1.1f;
  case TerrainTag::Ridge:
    return 0.55f;
  case TerrainTag::RockShelf:
    return 1.35f;
  case TerrainTag::Drama:
    return 0.85f;
  }
  return 1.f;
}

} // namespace bolt
