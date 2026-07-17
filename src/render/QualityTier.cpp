#include "bolt/render/QualityTier.hpp"

namespace bolt {

QualitySettings qualitySettings(QualityTier tier) {
  QualitySettings q;
  switch (tier) {
    case QualityTier::Low:
      q.terrainSegs = 16;
      q.viewChunks = 1;
      q.motionBlur = false;
      q.gpuTerrainHeight = false;
      q.particleScale = 0.25f;
      q.lodBias = 0.35f;
      q.maxFoliageInstances = 500;
      break;
    case QualityTier::Medium:
      q.terrainSegs = 28;
      q.viewChunks = 2;
      q.particleScale = 0.5f;
      q.lodBias = 0.15f;
      q.maxFoliageInstances = 1500;
      break;
    case QualityTier::High:
      q.terrainSegs = 40;
      q.viewChunks = 2;
      q.motionBlur = true;
      q.particleScale = 0.85f;
      q.maxFoliageInstances = 4000;
      break;
    case QualityTier::Max:
      q.terrainSegs = 48;
      q.viewChunks = 3;
      q.motionBlur = true;
      q.gpuTerrainHeight = true;
      q.particleScale = 1.f;
      q.lodBias = -0.05f;
      q.maxFoliageInstances = 8000;
      break;
  }
  return q;
}

} // namespace bolt
