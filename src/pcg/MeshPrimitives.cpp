#include "bolt/pcg/MeshPrimitives.hpp"

namespace bolt {

void buildShadowBlobMesh(std::vector<VertexPC>& outVerts, std::vector<uint32_t>& outIndices) {
  outVerts.clear();
  outIndices.clear();
  auto push = [&](float x, float z, float u, float v) {
    outVerts.push_back({{x, 0.f, z}, {0.f, 1.f, 0.f}, {u, v}, 0.f, 0.f});
  };
  push(-1.f, -1.f, 0.f, 0.f);
  push(1.f, -1.f, 1.f, 0.f);
  push(1.f, 1.f, 1.f, 1.f);
  push(-1.f, 1.f, 0.f, 1.f);
  outIndices = {0, 1, 2, 0, 2, 3};
}

} // namespace bolt
