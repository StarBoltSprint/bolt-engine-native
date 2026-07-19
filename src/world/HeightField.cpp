#include "bolt/world/HeightField.hpp"
#include "bolt/world/TerrainFeatureGenerator.hpp"
#include <cmath>
#include <algorithm>

namespace bolt {
namespace {

float hash2(float x, float z) {
  const float s = std::sin(x * 127.1f + z * 311.7f) * 43758.5453123f;
  return s - std::floor(s);
}

float fade5(float t) {
  return t * t * t * (t * (t * 6.f - 15.f) + 10.f);
}

float valueNoise(float x, float z) {
  const float x0 = std::floor(x);
  const float z0 = std::floor(z);
  const float fx = x - x0;
  const float fz = z - z0;
  const float u = fade5(fx);
  const float v = fade5(fz);
  const float a = hash2(x0, z0);
  const float b = hash2(x0 + 1.f, z0);
  const float c = hash2(x0, z0 + 1.f);
  const float d = hash2(x0 + 1.f, z0 + 1.f);
  const float ab = a + (b - a) * u;
  const float cd = c + (d - c) * u;
  return ab + (cd - ab) * v;
}

float fbm(float x, float z, int oct = 5) {
  float val = 0.f, amp = 1.f, freq = 1.f, mx = 0.f;
  for (int i = 0; i < oct; ++i) {
    val += amp * valueNoise(x * freq, z * freq);
    mx += amp;
    amp *= 0.5f;
    freq *= 2.03f;
  }
  return mx > 0.f ? val / mx : 0.f;
}

float ridge(float x, float z, int oct = 4) {
  float val = 0.f, amp = 1.f, freq = 1.f, mx = 0.f, w = 1.f;
  for (int i = 0; i < oct; ++i) {
    float n = valueNoise(x * freq, z * freq);
    n = 1.f - std::abs(n * 2.f - 1.f);
    n *= n * w;
    w = std::clamp(n * 1.85f, 0.f, 1.f);
    val += n * amp;
    mx += amp;
    amp *= 0.5f;
    freq *= 2.07f;
  }
  return mx > 0.f ? val / mx : 0.f;
}

} // namespace

float HeightField::sample(float x, float z) const {
  return sample(x, z, 0.f);
}

float HeightField::sample(float x, float z, float sprintScore) const {
  const float score = std::clamp(sprintScore, 0.f, 1.25f);
  const float ampMul = 1.f + score * 0.2f;
  const float detailMul = 1.f + score * 0.42f;

  // Crystal domain warp (double-ish)
  const float w1 = fbm(x * 0.009f + 3.1f, z * 0.009f + 1.7f, 4) - 0.5f;
  const float w2 = fbm(x * 0.009f + 19.4f, z * 0.009f + 7.2f, 4) - 0.5f;
  const float s = 16.f;
  float wx = x + w1 * s * 2.f;
  float wz = z + w2 * s * 2.f;
  const float w3 = fbm(wx * 0.014f + 41.f, wz * 0.014f + 9.f, 3) - 0.5f;
  const float w4 = fbm(wx * 0.014f + 8.f, wz * 0.014f + 33.f, 3) - 0.5f;
  wx = x + w1 * s * 2.f + w3 * s * 0.65f;
  wz = z + w2 * s * 2.f + w4 * s * 0.65f;

  const float cMacro = fbm(wx * 0.0072f, wz * 0.0072f, 5);
  const float cMid = fbm(wx * 0.024f + 17.f, wz * 0.024f, 4);
  const float cMicro = fbm(wx * 0.078f + 41.f, wz * 0.078f, 3);
  const float cRidge = ridge(wx * 0.012f, wz * 0.012f, 4);

  // Stronger base undulation so "organic flowing" hills read clearly under features
  float h = cMacro * 4.6f + cMid * 2.6f + cMicro * 0.75f * detailMul + cRidge * 1.65f;
  h += (hash2(x * 0.55f, z * 0.55f) - 0.5f) * (0.35f + score * 0.2f);
  h = h * 1.25f * ampMul;

  // TerrainFeatureGenerator: craters, ridges, rock mounds, veins, sprint look-ahead
  if (features_) h += features_->heightOffset(x, z, score);

  return h;
}

glm::vec3 HeightField::normal(float x, float z, float eps) const {
  // Use score=0 for finite differences when caller uses sample(x,z) overload —
  // prefer matching full sample via score 0 for consistency of static mesh normals.
  // Application rebuilds with sprint score; normals should use same.
  const float hL = sample(x - eps, z, 0.f);
  const float hR = sample(x + eps, z, 0.f);
  const float hD = sample(x, z - eps, 0.f);
  const float hU = sample(x, z + eps, 0.f);
  return glm::normalize(glm::vec3(hL - hR, 2.f * eps, hD - hU));
}

} // namespace bolt
