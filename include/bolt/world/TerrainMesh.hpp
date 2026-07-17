#pragma once
#include "bolt/world/HeightField.hpp"
#include "bolt/render/GpuMesh.hpp"
#include <vector>

namespace bolt {

struct TerrainMeshCPU {
  std::vector<VertexPC> vertices;
  std::vector<uint32_t> indices;
  int segs = 0;
  float size = 80.f;
};

/** Build a Crystal heightfield grid centered at origin (or given origin). */
TerrainMeshCPU buildTerrainMesh(const HeightField& height, int segs, float sizeMeters,
                                float originX = 0.f, float originZ = 0.f,
                                float sprintScore = 0.f);

} // namespace bolt
