#include "bolt/pcg/MeshPrimitives.hpp"
#include <cmath>

namespace bolt {
namespace {

constexpr float kPi = 3.14159265f;

void pushSphere(std::vector<VertexPC>& verts, std::vector<uint32_t>& indices,
                glm::vec3 center, float radius, int slices, int stacks) {
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
      verts.push_back({center + n * radius, n, {u, v}});
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

void pushCapsuleBody(std::vector<VertexPC>& verts, std::vector<uint32_t>& indices,
                     float y0, float y1, float radius, int slices, int stacks) {
  const uint32_t base = static_cast<uint32_t>(verts.size());
  for (int y = 0; y <= stacks; ++y) {
    const float v = static_cast<float>(y) / static_cast<float>(stacks);
    const float yy = y0 + (y1 - y0) * v;
    for (int x = 0; x <= slices; ++x) {
      const float u = static_cast<float>(x) / static_cast<float>(slices);
      const float th = u * kPi * 2.f;
      const float ct = std::cos(th), st = std::sin(th);
      glm::vec3 n{ct, 0.f, st};
      verts.push_back({{ct * radius, yy, st * radius}, n, {u, v}});
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

} // namespace

void buildShadowBlobMesh(std::vector<VertexPC>& outVerts, std::vector<uint32_t>& outIndices) {
  outVerts.clear();
  outIndices.clear();
  // Unit disc as a quad on XZ; radial soft edge in shader via UV
  // Corners at ±1 so UV maps cleanly to circle
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
  // Feet at y=0, head ~1.7 — pure white silhouette, readable at sprint cam distance
  const int slices = 12;
  pushSphere(outVerts, outIndices, {0.f, 0.18f, 0.f}, 0.22f, slices, 6);       // feet/base
  pushCapsuleBody(outVerts, outIndices, 0.25f, 1.05f, 0.26f, slices, 6);        // torso
  pushSphere(outVerts, outIndices, {0.f, 1.35f, 0.f}, 0.30f, slices, 8);       // head
  // Simple “bolt” shoulder flares
  pushSphere(outVerts, outIndices, {0.32f, 1.0f, 0.f}, 0.12f, 8, 4);
  pushSphere(outVerts, outIndices, {-0.32f, 1.0f, 0.f}, 0.12f, 8, 4);
}

void buildBoltBillboardMesh(std::vector<VertexPC>& outVerts, std::vector<uint32_t>& outIndices,
                            float width, float height) {
  outVerts.clear();
  outIndices.clear();
  const float hx = width * 0.5f;
  // Facing +Z; UV: bottom-left origin so feet stay at bottom of sprite
  auto push = [&](float x, float y, float u, float v) {
    outVerts.push_back({{x, y, 0.f}, {0.f, 0.f, 1.f}, {u, v}});
  };
  push(-hx, 0.f, 0.f, 1.f);
  push(hx, 0.f, 1.f, 1.f);
  push(hx, height, 1.f, 0.f);
  push(-hx, height, 0.f, 0.f);
  outIndices = {0, 1, 2, 0, 2, 3};
}

} // namespace bolt
