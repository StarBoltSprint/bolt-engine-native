#include "bolt/world/TerrainMesh.hpp"
#include "bolt/world/TerrainFeatureGenerator.hpp"
#include <cmath>

namespace bolt {

TerrainMeshCPU buildTerrainMesh(const HeightField& height, int segs, float sizeMeters,
                                float originX, float originZ, float sprintScore) {
  TerrainMeshCPU mesh;
  mesh.segs = segs;
  mesh.size = sizeMeters;
  const int n = segs + 1;
  mesh.vertices.resize(static_cast<size_t>(n * n));
  const float half = sizeMeters * 0.5f;

  for (int iz = 0; iz < n; ++iz) {
    for (int ix = 0; ix < n; ++ix) {
      const float u = static_cast<float>(ix) / static_cast<float>(segs);
      const float v = static_cast<float>(iz) / static_cast<float>(segs);
      const float x = originX + (u - 0.5f) * sizeMeters;
      const float z = originZ + (v - 0.5f) * sizeMeters;
      const float y = height.sample(x, z, sprintScore);
      VertexPC& vert = mesh.vertices[static_cast<size_t>(iz * n + ix)];
      vert.pos = {x, y, z};
      vert.uv = {u * 8.f, v * 8.f};
      vert.normal = heightNormalAt(height, x, z, sprintScore);
      // Feature offset for shader landform coloring (approx; 0 if no features)
      const float fo =
          height.features() ? height.features()->heightOffset(x, z, sprintScore) : 0.f;
      vert.matId = fo; // crater neg, ridge/rock pos
      vert.pad = 0.f;
    }
  }

  mesh.indices.reserve(static_cast<size_t>(segs * segs * 6));
  for (int iz = 0; iz < segs; ++iz) {
    for (int ix = 0; ix < segs; ++ix) {
      const uint32_t i0 = static_cast<uint32_t>(iz * n + ix);
      const uint32_t i1 = i0 + 1;
      const uint32_t i2 = i0 + static_cast<uint32_t>(n);
      const uint32_t i3 = i2 + 1;
      mesh.indices.push_back(i0);
      mesh.indices.push_back(i2);
      mesh.indices.push_back(i1);
      mesh.indices.push_back(i1);
      mesh.indices.push_back(i2);
      mesh.indices.push_back(i3);
    }
  }
  (void)half;
  return mesh;
}

glm::vec3 heightNormalAt(const HeightField& height, float x, float z, float sprintScore,
                         float eps) {
  const float hL = height.sample(x - eps, z, sprintScore);
  const float hR = height.sample(x + eps, z, sprintScore);
  const float hD = height.sample(x, z - eps, sprintScore);
  const float hU = height.sample(x, z + eps, sprintScore);
  return glm::normalize(glm::vec3(hL - hR, 2.f * eps, hD - hU));
}

} // namespace bolt
