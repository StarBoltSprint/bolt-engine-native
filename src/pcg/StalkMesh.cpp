#include "bolt/pcg/StalkMesh.hpp"
#include <cmath>

namespace bolt {

void buildStalkMesh(std::vector<VertexPC>& outVerts, std::vector<uint32_t>& outIndices) {
  outVerts.clear();
  outIndices.clear();
  // Vertical crystal spear: octahedron-ish
  const float h = 2.2f;
  const float r = 0.18f;
  // base center, tip, mid ring
  auto push = [&](glm::vec3 p, glm::vec3 n, glm::vec2 uv) {
    outVerts.push_back({p, n, uv});
  };
  const int segs = 6;
  // tip
  push({0, h, 0}, {0, 1, 0}, {0.5f, 1});
  // ring
  for (int i = 0; i < segs; ++i) {
    float a = (float)i / segs * 6.28318f;
    push({std::cos(a) * r, h * 0.35f, std::sin(a) * r},
         glm::normalize(glm::vec3(std::cos(a), 0.3f, std::sin(a))),
         {i / (float)segs, 0.4f});
  }
  // base
  push({0, 0, 0}, {0, -1, 0}, {0.5f, 0});

  // tip fans
  for (int i = 0; i < segs; ++i) {
    uint32_t tip = 0;
    uint32_t a = 1 + i;
    uint32_t b = 1 + ((i + 1) % segs);
    outIndices.push_back(tip);
    outIndices.push_back(a);
    outIndices.push_back(b);
  }
  // base fans
  uint32_t base = 1 + segs;
  for (int i = 0; i < segs; ++i) {
    uint32_t a = 1 + i;
    uint32_t b = 1 + ((i + 1) % segs);
    outIndices.push_back(base);
    outIndices.push_back(b);
    outIndices.push_back(a);
  }
}

} // namespace bolt
