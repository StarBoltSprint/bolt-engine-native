#pragma once

namespace bolt {

enum class QualityTier {
  Low = 0,
  Medium = 1,
  High = 2,
  Max = 3
};

struct QualitySettings {
  int terrainSegs = 32;
  int viewChunks = 2;
  bool motionBlur = false;
  bool gpuTerrainHeight = false;
  float particleScale = 0.5f;
  float lodBias = 0.f;
  int maxFoliageInstances = 1500;
};

QualitySettings qualitySettings(QualityTier tier);

} // namespace bolt
