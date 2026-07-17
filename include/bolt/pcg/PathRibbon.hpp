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
 * Build a real 3D ribbon mesh from PathGenerator centerline points.
 * halfWidth meters; UV.x across width, UV.y along path 0..1.
 */
PathRibbonCPU buildPathRibbon(const std::vector<PathPoint>& pts, float halfWidth,
                              const HeightField& height, float sprintScore);

} // namespace bolt
