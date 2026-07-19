#pragma once
#include "bolt/pcg/PathGenerator.hpp"
#include "bolt/render/GpuMesh.hpp"
#include "bolt/world/HeightField.hpp"
#include <vector>

namespace bolt {

struct PathRibbonCPU {
  std::vector<VertexPC> vertices;
  std::vector<uint32_t> indices;
};

/**
 * Build a 3D ribbon from one centerline.
 * matId encodes glow strength; elevate lifts above terrain (bridges).
 */
PathRibbonCPU buildPathRibbon(const std::vector<PathPoint>& pts, float halfWidth,
                              const HeightField& height, float sprintScore,
                              float elevate = 0.f, float glowMat = 1.f);

/** Merge full PathNetwork into one GPU mesh (main + branches + hidden + bridges). */
PathRibbonCPU buildPathNetworkMesh(const PathNetwork& net, float baseHalfWidth,
                                   const HeightField& height, float sprintScore);

} // namespace bolt
