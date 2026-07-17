#include "bolt/pcg/StalkMesh.hpp"
#include <cmath>

namespace bolt {

void buildStalkMesh(std::vector<VertexPC>& outVerts, std::vector<uint32_t>& outIndices) {
  outVerts.clear();
  outIndices.clear();

  // Multi-tier crystal spear (base flare → mid waist → sharp tip)
  const int sides = 10;
  auto push = [&](glm::vec3 p, glm::vec3 n, glm::vec2 uv) {
    outVerts.push_back({p, glm::normalize(n), uv});
  };

  // Rings: y, radius
  const float rings[][2] = {
      {0.00f, 0.28f},
      {0.35f, 0.22f},
      {1.10f, 0.16f},
      {1.85f, 0.10f},
      {2.55f, 0.00f}, // tip
  };
  const int ringCount = 5;

  for (int r = 0; r < ringCount; ++r) {
    float y = rings[r][0];
    float rad = rings[r][1];
    float v = y / 2.55f;
    if (rad < 0.001f) {
      push({0.f, y, 0.f}, {0.f, 1.f, 0.f}, {0.5f, v});
      continue;
    }
    for (int i = 0; i < sides; ++i) {
      float a = (float)i / (float)sides * 6.2831853f;
      float ca = std::cos(a), sa = std::sin(a);
      // Slight irregular crystal facets
      float rr = rad * (1.0f + 0.08f * std::sin(a * 3.f + y * 2.f));
      glm::vec3 p{ca * rr, y, sa * rr};
      glm::vec3 n{ca, 0.35f, sa};
      push(p, n, {float(i) / float(sides), v});
    }
  }

  // Tip index
  uint32_t tip = static_cast<uint32_t>((ringCount - 1) * sides); // after 4 full rings? 
  // Layout: rings 0..3 are sides verts each; ring 4 is tip single
  // verts: 0..sides-1, sides..2s-1, 2s..3s-1, 3s..4s-1, tip
  tip = static_cast<uint32_t>(4 * sides);

  auto ringVert = [&](int ring, int i) -> uint32_t {
    return static_cast<uint32_t>(ring * sides + (i % sides));
  };

  for (int r = 0; r < 3; ++r) {
    for (int i = 0; i < sides; ++i) {
      uint32_t a = ringVert(r, i);
      uint32_t b = ringVert(r, i + 1);
      uint32_t c = ringVert(r + 1, i);
      uint32_t d = ringVert(r + 1, i + 1);
      outIndices.push_back(a);
      outIndices.push_back(c);
      outIndices.push_back(b);
      outIndices.push_back(b);
      outIndices.push_back(c);
      outIndices.push_back(d);
    }
  }
  // last ring to tip
  for (int i = 0; i < sides; ++i) {
    uint32_t a = ringVert(3, i);
    uint32_t b = ringVert(3, i + 1);
    outIndices.push_back(a);
    outIndices.push_back(tip);
    outIndices.push_back(b);
  }
  // base cap
  uint32_t baseCenter = tip + 1;
  push({0.f, 0.f, 0.f}, {0.f, -1.f, 0.f}, {0.5f, 0.f});
  for (int i = 0; i < sides; ++i) {
    uint32_t a = ringVert(0, i);
    uint32_t b = ringVert(0, i + 1);
    outIndices.push_back(baseCenter);
    outIndices.push_back(b);
    outIndices.push_back(a);
  }
}

} // namespace bolt
