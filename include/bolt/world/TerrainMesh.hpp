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

/** Normal matching sample(..., sprintScore) including terrain features. */
glm::vec3 heightNormalAt(const HeightField& height, float x, float z, float sprintScore,
                         float eps = 0.75f);

} // namespace bolt
