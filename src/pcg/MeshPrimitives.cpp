#include "bolt/pcg/MeshPrimitives.hpp"
#include <cmath>

namespace bolt {
namespace {

constexpr float kPi = 3.14159265f;

// matId packed in UV.x for shader material regions
void pushSphere(std::vector<VertexPC>& verts, std::vector<uint32_t>& indices, glm::vec3 center,
                float radius, int slices, int stacks, float matId, glm::vec3 scale = {1, 1, 1}) {
  const uint32_t base = static_cast<uint32_t>(verts.size());
  for (int y = 0; y <= stacks; ++y) {
    const float v = static_cast<float>(y) / static_cast<float>(stacks);
    const float phi = v * kPi;
    const float sp = std::sin(phi), cp = std::cos(phi);
    for (int x = 0; x <= slices; ++x) {
      const float u = static_cast<float>(x) / static_cast<float>(slices);
      const float th = u * kPi * 2.f;
      const float st = std::sin(th), ct = std::cos(th);
      glm::vec3 n{st * sp, cp, ct * sp};
      glm::vec3 p = center + glm::vec3(n.x * scale.x, n.y * scale.y, n.z * scale.z) * radius;
      // re-normalize for scaled spheres
      glm::vec3 sn = glm::normalize(glm::vec3(n.x * scale.x, n.y * scale.y, n.z * scale.z));
      verts.push_back({p, sn, {matId, v}});
    }
  }
  const int stride = slices + 1;
  for (int y = 0; y < stacks; ++y) {
    for (int x = 0; x < slices; ++x) {
      const uint32_t i0 = base + static_cast<uint32_t>(y * stride + x);
      const uint32_t i1 = i0 + 1;
      const uint32_t i2 = i0 + static_cast<uint32_t>(stride);
      const uint32_t i3 = i2 + 1;
      indices.push_back(i0);
      indices.push_back(i2);
      indices.push_back(i1);
      indices.push_back(i1);
      indices.push_back(i2);
      indices.push_back(i3);
    }
  }
}

/** Capsule along local axis from a→b (including hemispheres). */
void pushCapsule(std::vector<VertexPC>& verts, std::vector<uint32_t>& indices, glm::vec3 a,
                 glm::vec3 b, float radius, int slices, int stacks, float matId) {
  glm::vec3 d = b - a;
  float len = glm::length(d);
  if (len < 1e-4f) {
    pushSphere(verts, indices, a, radius, slices, stacks, matId);
    return;
  }
  glm::vec3 axis = d / len;
  // Build orthonormal basis
  glm::vec3 up = std::abs(axis.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
  glm::vec3 t = glm::normalize(glm::cross(axis, up));
  glm::vec3 bi = glm::cross(axis, t);

  const uint32_t base = static_cast<uint32_t>(verts.size());
  const int rings = stacks + 2;
  for (int y = 0; y <= rings; ++y) {
    float v = static_cast<float>(y) / static_cast<float>(rings);
    float along = v * len;
    glm::vec3 c = a + axis * along;
    // hemisphere ends
    float rr = radius;
    glm::vec3 nOff{0};
    if (v < 0.15f) {
      float t0 = 1.f - v / 0.15f;
      float ang = t0 * (kPi * 0.5f);
      rr = radius * std::cos(ang);
      nOff = -axis * (radius * std::sin(ang));
      c = a + nOff;
    } else if (v > 0.85f) {
      float t0 = (v - 0.85f) / 0.15f;
      float ang = t0 * (kPi * 0.5f);
      rr = radius * std::cos(ang);
      nOff = axis * (radius * std::sin(ang));
      c = b + nOff;
    }
    for (int x = 0; x <= slices; ++x) {
      float u = static_cast<float>(x) / static_cast<float>(slices);
      float th = u * kPi * 2.f;
      float ct = std::cos(th), st = std::sin(th);
      glm::vec3 n = t * ct + bi * st;
      if (v < 0.15f || v > 0.85f) {
        glm::vec3 radial = n * rr + nOff;
        n = glm::normalize(radial);
      }
      verts.push_back({c + n * rr, n, {matId, v}});
    }
  }
  const int stride = slices + 1;
  for (int y = 0; y < rings; ++y) {
    for (int x = 0; x < slices; ++x) {
      const uint32_t i0 = base + static_cast<uint32_t>(y * stride + x);
      const uint32_t i1 = i0 + 1;
      const uint32_t i2 = i0 + static_cast<uint32_t>(stride);
      const uint32_t i3 = i2 + 1;
      indices.push_back(i0);
      indices.push_back(i2);
      indices.push_back(i1);
      indices.push_back(i1);
      indices.push_back(i2);
      indices.push_back(i3);
    }
  }
}

void pushEar(std::vector<VertexPC>& verts, std::vector<uint32_t>& indices, glm::vec3 base,
             float side, float matOuter, float matInner) {
  // GSD upright triangle ear: outer fur + pink inner
  const glm::vec3 tip = base + glm::vec3(side * 0.06f, 0.38f, -0.04f);
  const glm::vec3 b0 = base + glm::vec3(side * 0.02f, 0.f, 0.06f);
  const glm::vec3 b1 = base + glm::vec3(side * 0.14f, 0.f, -0.08f);
  const glm::vec3 b2 = base + glm::vec3(side * -0.02f, 0.f, -0.1f);
  // outer
  auto tri = [&](glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, float mat) {
    glm::vec3 n = glm::normalize(glm::cross(p1 - p0, p2 - p0));
    uint32_t i0 = static_cast<uint32_t>(verts.size());
    verts.push_back({p0, n, {mat, 0.f}});
    verts.push_back({p1, n, {mat, 0.5f}});
    verts.push_back({p2, n, {mat, 1.f}});
    indices.push_back(i0);
    indices.push_back(i0 + 1);
    indices.push_back(i0 + 2);
  };
  tri(b0, b1, tip, matOuter);
  tri(b1, b2, tip, matOuter);
  tri(b2, b0, tip, matOuter);
  // inner (slightly inset, reverse wind)
  const glm::vec3 inset = glm::vec3(-side * 0.02f, 0.02f, 0.01f);
  tri(b0 + inset, tip + inset * 0.5f, b1 + inset, matInner);
}

} // namespace

void buildShadowBlobMesh(std::vector<VertexPC>& outVerts, std::vector<uint32_t>& outIndices) {
  outVerts.clear();
  outIndices.clear();
  auto push = [&](float x, float z, float u, float v) {
    outVerts.push_back({{x, 0.f, z}, {0.f, 1.f, 0.f}, {u, v}});
  };
  push(-1.f, -1.f, 0.f, 0.f);
  push(1.f, -1.f, 1.f, 0.f);
  push(1.f, 1.f, 1.f, 1.f);
  push(-1.f, 1.f, 0.f, 1.f);
  outIndices = {0, 1, 2, 0, 2, 3};
}

void buildBoltMesh(std::vector<VertexPC>& outVerts, std::vector<uint32_t>& outIndices) {
  outVerts.clear();
  outIndices.clear();
  // Material ids: 0 fur, 1 eye, 2 nose, 3 ear-inner, 4 pad
  constexpr float FUR = 0.f, EYE = 1.f, NOSE = 2.f, EAR_IN = 3.f, PAD = 4.f;
  const int S = 12;
  const int St = 8;

  // ---- Body (lean athletic GSD) — facing +Z ----
  // Torso capsule hip → chest (along Z, elevated)
  pushCapsule(outVerts, outIndices, {0.f, 0.95f, -0.45f}, {0.f, 1.0f, 0.55f}, 0.32f, S, St, FUR);
  // Chest volume
  pushSphere(outVerts, outIndices, {0.f, 1.0f, 0.55f}, 0.42f, S, St, FUR, {1.05f, 1.1f, 1.15f});
  // Hip
  pushSphere(outVerts, outIndices, {0.f, 0.95f, -0.5f}, 0.36f, S, St, FUR, {1.05f, 0.95f, 1.1f});
  // Belly
  pushSphere(outVerts, outIndices, {0.f, 0.72f, 0.05f}, 0.26f, 10, 6, FUR, {0.85f, 0.6f, 1.4f});
  // Back ruff / saddle white volume
  pushSphere(outVerts, outIndices, {0.f, 1.18f, 0.05f}, 0.28f, 10, 6, FUR, {0.85f, 0.55f, 1.3f});
  // Neck
  pushCapsule(outVerts, outIndices, {0.f, 1.15f, 0.7f}, {0.f, 1.35f, 0.95f}, 0.22f, 10, 6, FUR);
  pushSphere(outVerts, outIndices, {0.f, 1.28f, 0.85f}, 0.26f, 10, 6, FUR, {1.1f, 0.9f, 1.0f});

  // ---- Head ----
  const glm::vec3 headC{0.f, 1.45f, 1.15f};
  pushSphere(outVerts, outIndices, headC, 0.30f, S, St, FUR, {0.95f, 0.9f, 1.2f});
  // Snout
  pushCapsule(outVerts, outIndices, headC + glm::vec3(0, -0.02f, 0.15f),
             headC + glm::vec3(0, -0.04f, 0.52f), 0.10f, 10, 6, FUR);
  // Nose
  pushSphere(outVerts, outIndices, headC + glm::vec3(0, -0.02f, 0.58f), 0.07f, 8, 6, NOSE,
             {1.2f, 0.85f, 0.9f});
  // Cheeks
  pushSphere(outVerts, outIndices, headC + glm::vec3(0.16f, -0.04f, 0.08f), 0.12f, 8, 6, FUR);
  pushSphere(outVerts, outIndices, headC + glm::vec3(-0.16f, -0.04f, 0.08f), 0.12f, 8, 6, FUR);
  // Brow
  pushSphere(outVerts, outIndices, headC + glm::vec3(0, 0.12f, 0.18f), 0.1f, 8, 4, FUR,
             {1.8f, 0.45f, 0.9f});

  // Eyes (cyan energy)
  pushSphere(outVerts, outIndices, headC + glm::vec3(0.11f, 0.04f, 0.28f), 0.055f, 8, 6, EYE);
  pushSphere(outVerts, outIndices, headC + glm::vec3(-0.11f, 0.04f, 0.28f), 0.055f, 8, 6, EYE);
  // Pupils (darker via nose mat smaller spheres inset)
  pushSphere(outVerts, outIndices, headC + glm::vec3(0.11f, 0.04f, 0.32f), 0.025f, 6, 4, NOSE);
  pushSphere(outVerts, outIndices, headC + glm::vec3(-0.11f, 0.04f, 0.32f), 0.025f, 6, 4, NOSE);

  // Ears
  pushEar(outVerts, outIndices, headC + glm::vec3(0.12f, 0.18f, -0.02f), 1.f, FUR, EAR_IN);
  pushEar(outVerts, outIndices, headC + glm::vec3(-0.12f, 0.18f, -0.02f), -1.f, FUR, EAR_IN);

  // ---- Legs ----
  auto leg = [&](float x, float z, float hipY) {
    // upper
    pushCapsule(outVerts, outIndices, {x, hipY, z}, {x * 0.95f, 0.45f, z + 0.02f}, 0.09f, 8, 5,
                FUR);
    // lower
    pushCapsule(outVerts, outIndices, {x * 0.95f, 0.45f, z + 0.02f}, {x * 0.9f, 0.12f, z + 0.04f},
                0.07f, 8, 5, FUR);
    // paw
    pushSphere(outVerts, outIndices, {x * 0.9f, 0.07f, z + 0.08f}, 0.09f, 8, 5, PAD,
               {1.1f, 0.55f, 1.3f});
  };
  leg(0.22f, 0.35f, 0.85f);   // front R
  leg(-0.22f, 0.35f, 0.85f);  // front L
  leg(0.24f, -0.4f, 0.88f);   // hind R
  leg(-0.24f, -0.4f, 0.88f);  // hind L

  // Tail (GSD curve up)
  pushCapsule(outVerts, outIndices, {0.f, 1.0f, -0.7f}, {0.05f, 1.25f, -1.05f}, 0.07f, 8, 5, FUR);
  pushSphere(outVerts, outIndices, {0.06f, 1.28f, -1.1f}, 0.09f, 8, 5, FUR);

  // Shoulder energy blazes (subtle geometry — lit as eye material when energy high)
  pushSphere(outVerts, outIndices, {0.28f, 1.15f, 0.3f}, 0.08f, 6, 4, EYE, {1.1f, 0.7f, 1.2f});
  pushSphere(outVerts, outIndices, {-0.28f, 1.15f, 0.3f}, 0.08f, 6, 4, EYE, {1.1f, 0.7f, 1.2f});
}

} // namespace bolt
