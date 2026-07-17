#include "bolt/pcg/PropMeshes.hpp"
#include <cmath>

namespace bolt {
namespace {

constexpr float kPi = 3.14159265f;

void pushSphere(std::vector<VertexPC>& verts, std::vector<uint32_t>& indices, glm::vec3 c,
                float r, int slices, int stacks, glm::vec3 sc = {1, 1, 1}) {
  const uint32_t base = static_cast<uint32_t>(verts.size());
  for (int y = 0; y <= stacks; ++y) {
    float vv = float(y) / float(stacks);
    float phi = vv * kPi;
    float sp = std::sin(phi), cp = std::cos(phi);
    for (int x = 0; x <= slices; ++x) {
      float u = float(x) / float(slices);
      float th = u * kPi * 2.f;
      glm::vec3 n{std::sin(th) * sp, cp, std::cos(th) * sp};
      glm::vec3 p = c + glm::vec3(n.x * sc.x, n.y * sc.y, n.z * sc.z) * r;
      glm::vec3 sn = glm::normalize(glm::vec3(n.x * sc.x, n.y * sc.y, n.z * sc.z));
      verts.push_back({p, sn, {u, vv}, 0.f, 0.f});
    }
  }
  int stride = slices + 1;
  for (int y = 0; y < stacks; ++y)
    for (int x = 0; x < slices; ++x) {
      uint32_t i0 = base + uint32_t(y * stride + x);
      uint32_t i1 = i0 + 1, i2 = i0 + uint32_t(stride), i3 = i2 + 1;
      indices.insert(indices.end(), {i0, i2, i1, i1, i2, i3});
    }
}

void pushCyl(std::vector<VertexPC>& verts, std::vector<uint32_t>& indices, float y0, float y1,
             float r0, float r1, int sides) {
  const uint32_t base = static_cast<uint32_t>(verts.size());
  for (int ring = 0; ring < 2; ++ring) {
    float y = ring == 0 ? y0 : y1;
    float r = ring == 0 ? r0 : r1;
    float v = ring == 0 ? 0.f : 1.f;
    for (int i = 0; i <= sides; ++i) {
      float u = float(i) / float(sides);
      float a = u * kPi * 2.f;
      float ca = std::cos(a), sa = std::sin(a);
      verts.push_back({{ca * r, y, sa * r}, {ca, 0.2f, sa}, {u, v}, 0.f, 0.f});
    }
  }
  int stride = sides + 1;
  for (int i = 0; i < sides; ++i) {
    uint32_t a = base + uint32_t(i);
    uint32_t b = a + 1;
    uint32_t c = base + uint32_t(stride + i);
    uint32_t d = c + 1;
    indices.insert(indices.end(), {a, c, b, b, c, d});
  }
}

} // namespace

void buildBushMesh(std::vector<VertexPC>& v, std::vector<uint32_t>& i) {
  v.clear();
  i.clear();
  pushSphere(v, i, {0, 0.35f, 0}, 0.45f, 10, 6, {1.1f, 0.75f, 1.1f});
  pushSphere(v, i, {0.25f, 0.45f, 0.1f}, 0.28f, 8, 5);
  pushSphere(v, i, {-0.22f, 0.4f, -0.15f}, 0.26f, 8, 5);
  pushSphere(v, i, {0.05f, 0.65f, -0.05f}, 0.22f, 8, 5);
}

void buildDetailShardMesh(std::vector<VertexPC>& v, std::vector<uint32_t>& i) {
  v.clear();
  i.clear();
  // Small faceted crystal pebble
  pushCyl(v, i, 0.f, 0.18f, 0.12f, 0.04f, 6);
  pushSphere(v, i, {0, 0.2f, 0}, 0.06f, 6, 4);
}

void buildRuinPillarMesh(std::vector<VertexPC>& v, std::vector<uint32_t>& i) {
  v.clear();
  i.clear();
  pushCyl(v, i, 0.f, 1.6f, 0.28f, 0.22f, 8);
  pushCyl(v, i, 1.55f, 1.85f, 0.32f, 0.18f, 8); // broken top
  // Chunks
  pushSphere(v, i, {0.35f, 0.4f, 0.1f}, 0.2f, 6, 4, {1.2f, 0.6f, 0.9f});
}

void buildRuinArchMesh(std::vector<VertexPC>& v, std::vector<uint32_t>& i) {
  v.clear();
  i.clear();
  // Two pillars + lintel
  pushCyl(v, i, 0.f, 1.4f, 0.18f, 0.16f, 7);
  // offset second pillar by baking in positions
  const uint32_t base = static_cast<uint32_t>(v.size());
  pushCyl(v, i, 0.f, 1.4f, 0.18f, 0.16f, 7);
  for (uint32_t k = base; k < v.size(); ++k) v[k].pos.x += 1.2f;
  // lintel
  pushSphere(v, i, {0.6f, 1.5f, 0.f}, 0.35f, 8, 5, {1.8f, 0.35f, 0.4f});
}

void buildTallCrystalMesh(std::vector<VertexPC>& v, std::vector<uint32_t>& i) {
  v.clear();
  i.clear();
  pushCyl(v, i, 0.f, 2.8f, 0.18f, 0.02f, 7);
  pushSphere(v, i, {0, 2.9f, 0}, 0.08f, 6, 4);
}

} // namespace bolt
