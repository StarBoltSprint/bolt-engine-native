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
      glm::vec3 sn = glm::normalize(glm::vec3(n.x / sc.x, n.y / sc.y, n.z / sc.z));
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
             float r0, float r1, int sides, float xOff = 0.f, float zOff = 0.f) {
  const uint32_t base = static_cast<uint32_t>(verts.size());
  for (int ring = 0; ring < 2; ++ring) {
    float y = ring == 0 ? y0 : y1;
    float r = ring == 0 ? r0 : r1;
    float v = ring == 0 ? 0.f : 1.f;
    for (int i = 0; i <= sides; ++i) {
      float u = float(i) / float(sides);
      float a = u * kPi * 2.f;
      float ca = std::cos(a), sa = std::sin(a);
      verts.push_back(
          {{xOff + ca * r, y, zOff + sa * r}, {ca, 0.25f, sa}, {u, v}, 0.f, 0.f});
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

void pushCrystalSpear(std::vector<VertexPC>& v, std::vector<uint32_t>& i, float y0, float y1,
                      float rBase, float leanX = 0.f) {
  // Faceted tapered crystal with slight lean
  const int sides = 6;
  const uint32_t base = static_cast<uint32_t>(v.size());
  const float midY = y0 + (y1 - y0) * 0.55f;
  for (int ring = 0; ring < 3; ++ring) {
    float t = ring / 2.f;
    float y = y0 + (y1 - y0) * t;
    float r = rBase * (1.f - t * 0.92f);
    float lean = leanX * t;
    for (int s = 0; s <= sides; ++s) {
      float u = float(s) / float(sides);
      float a = u * kPi * 2.f;
      float ca = std::cos(a), sa = std::sin(a);
      float rr = r * (1.f + 0.12f * std::sin(a * 2.f + y));
      v.push_back({{ca * rr + lean, y, sa * rr}, {ca, 0.4f, sa}, {u, t}, 0.f, 0.f});
    }
  }
  // tip
  v.push_back({{leanX, y1 + (y1 - y0) * 0.08f, 0.f}, {0, 1, 0}, {0.5f, 1.f}, 0.f, 0.f});
  const uint32_t tip = static_cast<uint32_t>(v.size() - 1);
  int stride = sides + 1;
  for (int r = 0; r < 2; ++r)
    for (int s = 0; s < sides; ++s) {
      uint32_t a = base + uint32_t(r * stride + s);
      uint32_t b = a + 1;
      uint32_t c = base + uint32_t((r + 1) * stride + s);
      uint32_t d = c + 1;
      i.insert(i.end(), {a, c, b, b, c, d});
    }
  for (int s = 0; s < sides; ++s) {
    uint32_t a = base + uint32_t(2 * stride + s);
    uint32_t b = base + uint32_t(2 * stride + s + 1);
    i.insert(i.end(), {a, tip, b});
  }
  (void)midY;
}

} // namespace

void buildBushMesh(std::vector<VertexPC>& v, std::vector<uint32_t>& i) {
  v.clear();
  i.clear();
  // Crystal fern / glowing bush — layered plates + central bud
  pushSphere(v, i, {0, 0.28f, 0}, 0.38f, 10, 6, {1.25f, 0.55f, 1.15f});
  pushSphere(v, i, {0.22f, 0.42f, 0.12f}, 0.22f, 8, 5, {1.1f, 0.7f, 0.9f});
  pushSphere(v, i, {-0.2f, 0.4f, -0.1f}, 0.2f, 8, 5, {1.0f, 0.65f, 1.1f});
  pushSphere(v, i, {0.05f, 0.55f, -0.08f}, 0.16f, 7, 4, {0.9f, 0.8f, 1.0f});
  // Floating crystal buds
  pushCrystalSpear(v, i, 0.5f, 0.95f, 0.06f, 0.08f);
  pushCrystalSpear(v, i, 0.48f, 0.88f, 0.05f, -0.1f);
}

void buildDetailShardMesh(std::vector<VertexPC>& v, std::vector<uint32_t>& i) {
  v.clear();
  i.clear();
  // Multi-use detail prop: vent lip + cluster spears + buds (styled by DetailKind in shader)
  pushSphere(v, i, {0.f, 0.06f, 0.f}, 0.22f, 10, 6, {1.35f, 0.35f, 1.25f});
  pushCyl(v, i, 0.f, 0.08f, 0.28f, 0.22f, 10);
  pushCrystalSpear(v, i, 0.05f, 0.55f, 0.09f, 0.f);
  pushCrystalSpear(v, i, 0.08f, 0.42f, 0.06f, 0.18f);
  pushCrystalSpear(v, i, 0.06f, 0.38f, 0.05f, -0.16f);
  pushSphere(v, i, {0.14f, 0.12f, 0.08f}, 0.07f, 6, 4, {1.1f, 0.7f, 1.0f});
  pushSphere(v, i, {-0.12f, 0.1f, -0.06f}, 0.06f, 6, 4, {1.0f, 0.65f, 1.05f});
  pushSphere(v, i, {0.02f, 0.48f, 0.f}, 0.08f, 7, 5, {0.9f, 1.1f, 0.95f});
  pushSphere(v, i, {0.f, 0.04f, 0.18f}, 0.1f, 6, 3, {1.4f, 0.2f, 0.9f});
}

void buildRuinPillarMesh(std::vector<VertexPC>& v, std::vector<uint32_t>& i) {
  v.clear();
  i.clear();
  // Resonance Monolith — massive ancient pedestal + thick shaft + crystal crown
  // Wide stone base (architecture, not a spear tip)
  pushCyl(v, i, 0.f, 0.55f, 0.85f, 0.72f, 10);
  pushCyl(v, i, 0.5f, 0.85f, 0.72f, 0.48f, 9);
  // Heavy shaft
  pushCyl(v, i, 0.8f, 3.4f, 0.45f, 0.38f, 8);
  pushCyl(v, i, 3.3f, 4.2f, 0.4f, 0.28f, 8);
  // Broken capital
  pushCyl(v, i, 4.1f, 4.55f, 0.52f, 0.22f, 8);
  // Crystal crown (secondary, not the whole read)
  pushCrystalSpear(v, i, 4.3f, 5.6f, 0.16f, 0.04f);
  pushCrystalSpear(v, i, 4.0f, 5.1f, 0.1f, 0.22f);
  // Rubble / grown crystal at base
  pushSphere(v, i, {0.55f, 0.35f, 0.2f}, 0.32f, 7, 5, {1.3f, 0.55f, 1.0f});
  pushSphere(v, i, {-0.5f, 0.28f, -0.15f}, 0.28f, 7, 4, {1.2f, 0.5f, 0.95f});
}

void buildRuinArchMesh(std::vector<VertexPC>& v, std::vector<uint32_t>& i) {
  v.clear();
  i.clear();
  // Floating Archway — thick legs, clear span, monumental lintel
  pushCyl(v, i, 0.f, 0.45f, 0.55f, 0.48f, 8, -1.15f, 0.f); // left foot
  pushCyl(v, i, 0.4f, 2.8f, 0.38f, 0.32f, 8, -1.15f, 0.f);
  pushCyl(v, i, 0.f, 0.45f, 0.55f, 0.48f, 8, 1.15f, 0.f); // right foot
  pushCyl(v, i, 0.4f, 2.65f, 0.38f, 0.3f, 8, 1.15f, 0.f);
  // Massive lintel (broken)
  pushSphere(v, i, {0.f, 3.05f, 0.f}, 0.7f, 10, 6, {2.4f, 0.45f, 0.55f});
  pushSphere(v, i, {-0.55f, 2.95f, 0.08f}, 0.38f, 8, 5, {1.6f, 0.4f, 0.45f});
  pushSphere(v, i, {0.6f, 3.15f, -0.06f}, 0.35f, 8, 5, {1.5f, 0.42f, 0.4f});
  // Crystal growth on stone
  pushCrystalSpear(v, i, 2.2f, 3.6f, 0.1f, 0.15f);
  pushCrystalSpear(v, i, 2.0f, 3.4f, 0.09f, -0.12f);
  pushSphere(v, i, {0.15f, 3.7f, 0.f}, 0.18f, 6, 4);
}

void buildRuinObservatoryMesh(std::vector<VertexPC>& v, std::vector<uint32_t>& i) {
  v.clear();
  i.clear();
  // Crystal Observatory — wide plaza ring + thick walls + dome + lenses
  pushCyl(v, i, 0.f, 0.4f, 1.6f, 1.55f, 14); // plaza
  pushCyl(v, i, 0.35f, 0.85f, 1.4f, 1.25f, 12);
  pushCyl(v, i, 0.8f, 1.5f, 1.15f, 1.0f, 12); // wall
  // Broken dome
  pushSphere(v, i, {0.f, 2.0f, 0.f}, 1.05f, 14, 8, {1.5f, 0.75f, 1.4f});
  pushSphere(v, i, {0.65f, 1.7f, 0.3f}, 0.48f, 9, 5, {1.2f, 0.65f, 1.1f});
  pushSphere(v, i, {-0.55f, 1.75f, -0.25f}, 0.45f, 9, 5, {1.15f, 0.6f, 1.15f});
  // Crystal lenses (hero glow points)
  pushSphere(v, i, {0.85f, 2.35f, 0.15f}, 0.32f, 9, 6, {0.9f, 1.0f, 1.1f});
  pushSphere(v, i, {-0.75f, 2.25f, 0.2f}, 0.28f, 8, 5, {0.95f, 0.95f, 1.1f});
  pushCrystalSpear(v, i, 1.4f, 3.0f, 0.14f, 0.06f);
  pushCyl(v, i, 0.85f, 1.6f, 0.28f, 0.2f, 8); // central pedestal
}

void buildRuinTempleMesh(std::vector<VertexPC>& v, std::vector<uint32_t>& i) {
  v.clear();
  i.clear();
  // Buried Temple — heavy architectural mass, not a blob
  pushCyl(v, i, 0.f, 0.7f, 1.7f, 1.55f, 12); // sunk podium
  pushSphere(v, i, {0.f, 0.9f, 0.f}, 1.35f, 12, 7, {2.0f, 0.55f, 1.5f}); // body
  // Corner pillars (ruined)
  pushCyl(v, i, 0.5f, 2.4f, 0.22f, 0.16f, 7, -1.0f, 0.85f);
  pushCyl(v, i, 0.45f, 2.2f, 0.22f, 0.15f, 7, 1.0f, 0.8f);
  pushCyl(v, i, 0.4f, 2.0f, 0.2f, 0.14f, 7, 0.85f, -0.95f);
  pushCyl(v, i, 0.4f, 1.85f, 0.2f, 0.13f, 7, -0.9f, -0.9f);
  // Central altar crystal (still active)
  pushCrystalSpear(v, i, 1.0f, 3.2f, 0.22f, 0.f);
  pushSphere(v, i, {0.f, 1.35f, 0.f}, 0.5f, 9, 5, {1.1f, 0.75f, 1.1f});
  // Crystal growth over stone
  pushSphere(v, i, {0.75f, 0.55f, -0.5f}, 0.4f, 8, 5, {1.3f, 0.45f, 1.0f});
  pushSphere(v, i, {-0.8f, 0.5f, 0.4f}, 0.42f, 8, 5, {1.25f, 0.5f, 1.05f});
  pushCrystalSpear(v, i, 0.6f, 2.0f, 0.12f, 0.25f);
}

void buildCrystalTree(int typeIndex, std::vector<VertexPC>& v, std::vector<uint32_t>& i) {
  v.clear();
  i.clear();
  const int t = ((typeIndex % kCrystalTreeTypes) + kCrystalTreeTypes) % kCrystalTreeTypes;

  switch (t) {
  case 0: // Resonance Spear — tall thin antenna of the plains
    pushCyl(v, i, 0.f, 2.0f, 0.14f, 0.08f, 8);
    pushCyl(v, i, 1.9f, 3.0f, 0.08f, 0.04f, 7);
    pushCrystalSpear(v, i, 2.6f, 4.6f, 0.13f, 0.f);
    pushCrystalSpear(v, i, 2.4f, 3.7f, 0.07f, 0.28f);
    pushCrystalSpear(v, i, 2.45f, 3.8f, 0.06f, -0.3f);
    pushSphere(v, i, {0.f, 3.2f, 0.f}, 0.42f, 10, 6, {1.25f, 0.75f, 1.2f});
    pushSphere(v, i, {0.12f, 0.1f, 0.08f}, 0.12f, 6, 4, {1.15f, 0.5f, 1.0f});
    break;

  case 1: // Twisted Conduit — dual braided trunks of living crystal
    pushCyl(v, i, 0.f, 1.6f, 0.11f, 0.07f, 7, -0.08f, 0.05f);
    pushCyl(v, i, 1.5f, 2.9f, 0.07f, 0.035f, 6, -0.05f, 0.08f);
    pushCyl(v, i, 0.f, 1.8f, 0.1f, 0.06f, 7, 0.14f, -0.1f);
    pushCyl(v, i, 1.7f, 2.7f, 0.06f, 0.03f, 6, 0.18f, -0.06f);
    pushCrystalSpear(v, i, 2.5f, 4.1f, 0.11f, 0.22f);
    pushCrystalSpear(v, i, 2.3f, 3.9f, 0.1f, -0.35f);
    pushCrystalSpear(v, i, 2.6f, 3.5f, 0.08f, 0.05f);
    pushSphere(v, i, {0.05f, 3.05f, 0.f}, 0.5f, 10, 6, {1.35f, 0.65f, 1.25f});
    pushSphere(v, i, {-0.28f, 2.7f, 0.18f}, 0.26f, 7, 4, {1.1f, 0.55f, 1.15f});
    pushSphere(v, i, {0.32f, 2.55f, -0.12f}, 0.24f, 7, 4, {1.15f, 0.5f, 1.1f});
    break;

  case 2: // Nebula Cap — wide soft canopy, short bole (mushroom of light)
    pushCyl(v, i, 0.f, 1.0f, 0.24f, 0.16f, 9);
    pushCyl(v, i, 0.95f, 1.45f, 0.16f, 0.1f, 8);
    pushSphere(v, i, {0.f, 1.95f, 0.f}, 0.95f, 14, 8, {1.55f, 0.5f, 1.45f});
    pushSphere(v, i, {0.45f, 1.8f, 0.25f}, 0.42f, 9, 5, {1.25f, 0.48f, 1.2f});
    pushSphere(v, i, {-0.4f, 1.85f, -0.22f}, 0.4f, 9, 5, {1.2f, 0.45f, 1.25f});
    pushSphere(v, i, {0.1f, 2.25f, -0.15f}, 0.32f, 8, 5, {1.1f, 0.55f, 1.15f});
    pushCrystalSpear(v, i, 1.55f, 2.45f, 0.09f, 0.12f);
    pushCrystalSpear(v, i, 1.5f, 2.35f, 0.08f, -0.2f);
    pushSphere(v, i, {0.18f, 0.08f, 0.12f}, 0.14f, 6, 4, {1.1f, 0.45f, 1.0f});
    break;

  case 3: // Amethyst Spire — stacked angular crystal towers
    pushCyl(v, i, 0.f, 0.5f, 0.32f, 0.22f, 6);
    pushCrystalSpear(v, i, 0.4f, 2.2f, 0.2f, 0.f);
    pushCrystalSpear(v, i, 1.6f, 3.5f, 0.14f, 0.12f);
    pushCrystalSpear(v, i, 2.6f, 4.4f, 0.09f, -0.08f);
    pushCrystalSpear(v, i, 1.2f, 2.8f, 0.1f, 0.38f);
    pushCrystalSpear(v, i, 1.3f, 2.9f, 0.09f, -0.42f);
    pushCrystalSpear(v, i, 2.0f, 3.4f, 0.07f, 0.28f);
    pushSphere(v, i, {0.f, 0.2f, 0.f}, 0.28f, 8, 5, {1.3f, 0.4f, 1.2f});
    break;

  case 4: // Weeping Crystal — trunk with cascading hanging shards
    pushCyl(v, i, 0.f, 2.4f, 0.15f, 0.09f, 8);
    pushSphere(v, i, {0.f, 2.55f, 0.f}, 0.48f, 10, 6, {1.3f, 0.7f, 1.2f});
    // Drooping spears (downward-ish via negative height span using lean + low tips)
    pushCrystalSpear(v, i, 1.8f, 2.7f, 0.07f, 0.55f);
    pushCrystalSpear(v, i, 1.6f, 2.5f, 0.06f, -0.6f);
    pushCrystalSpear(v, i, 1.5f, 2.4f, 0.055f, 0.35f);
    pushCrystalSpear(v, i, 1.4f, 2.35f, 0.05f, -0.4f);
    pushSphere(v, i, {0.55f, 1.7f, 0.1f}, 0.12f, 6, 4, {0.9f, 0.8f, 1.1f});
    pushSphere(v, i, {-0.5f, 1.55f, -0.08f}, 0.11f, 6, 4, {0.95f, 0.75f, 1.15f});
    pushSphere(v, i, {0.35f, 1.2f, -0.2f}, 0.1f, 6, 4, {1.0f, 0.7f, 1.1f});
    pushSphere(v, i, {-0.3f, 1.05f, 0.25f}, 0.09f, 6, 4, {0.9f, 0.75f, 1.2f});
    pushCrystalSpear(v, i, 2.3f, 3.5f, 0.08f, 0.1f);
    break;

  case 5: // Floating Crown — bole + levitating resonance cluster
    pushCyl(v, i, 0.f, 1.8f, 0.13f, 0.07f, 8);
    pushCyl(v, i, 1.7f, 2.2f, 0.07f, 0.03f, 6);
    // Gap then floating crown
    pushSphere(v, i, {0.f, 3.2f, 0.f}, 0.55f, 11, 7, {1.4f, 0.85f, 1.35f});
    pushSphere(v, i, {0.28f, 3.4f, 0.15f}, 0.28f, 8, 5, {1.15f, 0.7f, 1.2f});
    pushSphere(v, i, {-0.25f, 3.35f, -0.12f}, 0.26f, 8, 5, {1.2f, 0.65f, 1.15f});
    pushCrystalSpear(v, i, 3.0f, 4.0f, 0.08f, 0.15f);
    pushCrystalSpear(v, i, 2.95f, 3.85f, 0.07f, -0.2f);
    pushSphere(v, i, {0.08f, 2.55f, 0.f}, 0.1f, 6, 4); // small bridge crystal
    pushSphere(v, i, {0.f, 0.12f, 0.f}, 0.16f, 7, 4, {1.15f, 0.5f, 1.05f});
    break;

  case 6: // Rooted Prism — wide angular roots, geometric body
    pushSphere(v, i, {0.35f, 0.15f, 0.2f}, 0.28f, 7, 4, {1.4f, 0.35f, 1.0f});
    pushSphere(v, i, {-0.32f, 0.12f, -0.18f}, 0.26f, 7, 4, {1.35f, 0.32f, 1.05f});
    pushSphere(v, i, {0.05f, 0.1f, 0.38f}, 0.22f, 6, 4, {1.3f, 0.3f, 1.1f});
    pushSphere(v, i, {-0.1f, 0.1f, -0.35f}, 0.2f, 6, 4, {1.25f, 0.28f, 1.0f});
    pushCyl(v, i, 0.2f, 0.7f, 0.35f, 0.2f, 6);
    pushCrystalSpear(v, i, 0.55f, 2.8f, 0.18f, 0.f);
    pushCrystalSpear(v, i, 1.8f, 3.6f, 0.1f, 0.2f);
    pushCrystalSpear(v, i, 1.9f, 3.5f, 0.09f, -0.25f);
    pushSphere(v, i, {0.f, 2.4f, 0.f}, 0.35f, 8, 5, {1.2f, 0.6f, 1.15f});
    break;

  case 7: // Lumen Fern-Tree — stacked luminous frond plates
    pushCyl(v, i, 0.f, 1.4f, 0.1f, 0.06f, 7);
    // Layered horizontal fronds
    pushSphere(v, i, {0.f, 1.0f, 0.f}, 0.55f, 10, 4, {1.8f, 0.22f, 1.6f});
    pushSphere(v, i, {0.f, 1.45f, 0.f}, 0.7f, 12, 4, {2.0f, 0.2f, 1.8f});
    pushSphere(v, i, {0.f, 1.9f, 0.f}, 0.85f, 12, 4, {2.1f, 0.18f, 1.9f});
    pushSphere(v, i, {0.f, 2.35f, 0.f}, 0.65f, 10, 4, {1.7f, 0.2f, 1.5f});
    pushSphere(v, i, {0.f, 2.75f, 0.f}, 0.4f, 9, 4, {1.3f, 0.25f, 1.2f});
    pushCrystalSpear(v, i, 2.5f, 3.4f, 0.07f, 0.05f);
    pushSphere(v, i, {0.25f, 1.6f, 0.2f}, 0.18f, 6, 4, {1.0f, 0.5f, 1.1f});
    break;

  case 8: // Broken Sentinel — shattered ancient growth, still alive
    pushCyl(v, i, 0.f, 1.2f, 0.2f, 0.16f, 8);
    pushCyl(v, i, 1.15f, 1.9f, 0.16f, 0.12f, 7);
    // Jagged break
    pushSphere(v, i, {0.12f, 2.0f, -0.05f}, 0.22f, 7, 5, {1.2f, 0.5f, 0.9f});
    pushSphere(v, i, {-0.15f, 1.95f, 0.1f}, 0.18f, 6, 4, {1.1f, 0.45f, 0.95f});
    // New crystal regrowth from wound
    pushCrystalSpear(v, i, 1.85f, 3.3f, 0.1f, 0.18f);
    pushCrystalSpear(v, i, 1.9f, 3.1f, 0.08f, -0.22f);
    pushCrystalSpear(v, i, 2.0f, 2.9f, 0.06f, 0.05f);
    // Fallen shard near base
    pushSphere(v, i, {0.45f, 0.12f, 0.15f}, 0.16f, 6, 4, {1.3f, 0.35f, 0.9f});
    pushCrystalSpear(v, i, 0.05f, 0.55f, 0.08f, 0.4f);
    break;

  case 9: // Halo Bloom — stem with orbital ring of crystals
  default:
    pushCyl(v, i, 0.f, 2.2f, 0.12f, 0.07f, 8);
    pushSphere(v, i, {0.f, 2.4f, 0.f}, 0.28f, 9, 5, {1.15f, 0.8f, 1.15f});
    // Ring of crystals around mid-upper height
    for (int k = 0; k < 6; ++k) {
      float a = float(k) / 6.f * 6.2831853f;
      float cx = std::cos(a) * 0.55f;
      float cz = std::sin(a) * 0.55f;
      const size_t before = v.size();
      pushCrystalSpear(v, i, 1.7f, 2.6f, 0.055f, 0.f);
      for (size_t vi = before; vi < v.size(); ++vi) {
        v[vi].pos.x += cx;
        v[vi].pos.z += cz;
      }
    }
    pushCrystalSpear(v, i, 2.3f, 3.6f, 0.09f, 0.f);
    pushSphere(v, i, {0.5f, 2.15f, 0.f}, 0.12f, 6, 4, {1.0f, 0.7f, 1.2f});
    pushSphere(v, i, {-0.45f, 2.1f, 0.2f}, 0.11f, 6, 4, {0.95f, 0.65f, 1.15f});
    pushSphere(v, i, {0.f, 0.1f, 0.f}, 0.14f, 6, 4, {1.1f, 0.45f, 1.0f});
    break;
  }
}

void buildTallCrystalMesh(std::vector<VertexPC>& v, std::vector<uint32_t>& i) {
  buildCrystalTree(0, v, i);
}
void buildTallCrystalMeshB(std::vector<VertexPC>& v, std::vector<uint32_t>& i) {
  buildCrystalTree(1, v, i);
}
void buildTallCrystalMeshC(std::vector<VertexPC>& v, std::vector<uint32_t>& i) {
  buildCrystalTree(2, v, i);
}

} // namespace bolt
