#include "bolt/pcg/BoltGsd.hpp"
#include "bolt/assets/ObjLoader.hpp"
#include "bolt/assets/GltfLoader.hpp"
#include "bolt/core/Log.hpp"
#include <cmath>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>

namespace bolt {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float FUR = 0.f, EYE = 1.f, NOSE = 2.f, EAR = 3.f, PAD = 4.f, AURA = 5.f;

void pushSphere(BoltPartMesh& m, glm::vec3 c, float r, int slices, int stacks, float mat,
                glm::vec3 sc = {1, 1, 1}) {
  const uint32_t base = static_cast<uint32_t>(m.vertices.size());
  for (int y = 0; y <= stacks; ++y) {
    float v = float(y) / float(stacks);
    float phi = v * kPi;
    float sp = std::sin(phi), cp = std::cos(phi);
    for (int x = 0; x <= slices; ++x) {
      float u = float(x) / float(slices);
      float th = u * kPi * 2.f;
      float st = std::sin(th), ct = std::cos(th);
      glm::vec3 n{st * sp, cp, ct * sp};
      glm::vec3 p = c + glm::vec3(n.x * sc.x, n.y * sc.y, n.z * sc.z) * r;
      glm::vec3 sn = glm::normalize(glm::vec3(n.x * sc.x, n.y * sc.y, n.z * sc.z));
      // cylindrical-ish fur UVs from world-ish local coords
      float tu = u;
      float tv = v;
      m.vertices.push_back({p, sn, {tu, tv}, mat, 0.f});
    }
  }
  int stride = slices + 1;
  for (int y = 0; y < stacks; ++y) {
    for (int x = 0; x < slices; ++x) {
      uint32_t i0 = base + uint32_t(y * stride + x);
      uint32_t i1 = i0 + 1;
      uint32_t i2 = i0 + uint32_t(stride);
      uint32_t i3 = i2 + 1;
      m.indices.insert(m.indices.end(), {i0, i2, i1, i1, i2, i3});
    }
  }
}

void pushCapsule(BoltPartMesh& m, glm::vec3 a, glm::vec3 b, float radius, int slices, int stacks,
                 float mat) {
  glm::vec3 d = b - a;
  float len = glm::length(d);
  if (len < 1e-4f) {
    pushSphere(m, a, radius, slices, stacks, mat);
    return;
  }
  glm::vec3 axis = d / len;
  glm::vec3 up = std::abs(axis.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
  glm::vec3 t = glm::normalize(glm::cross(axis, up));
  glm::vec3 bi = glm::cross(axis, t);
  const uint32_t base = static_cast<uint32_t>(m.vertices.size());
  const int rings = stacks + 4;
  for (int y = 0; y <= rings; ++y) {
    float v = float(y) / float(rings);
    glm::vec3 c = a + axis * (v * len);
    float rr = radius;
    glm::vec3 nOff{0};
    if (v < 0.12f) {
      float tt = 1.f - v / 0.12f;
      float ang = tt * (kPi * 0.5f);
      rr = radius * std::cos(ang);
      nOff = -axis * (radius * std::sin(ang));
      c = a + nOff;
    } else if (v > 0.88f) {
      float tt = (v - 0.88f) / 0.12f;
      float ang = tt * (kPi * 0.5f);
      rr = radius * std::cos(ang);
      nOff = axis * (radius * std::sin(ang));
      c = b + nOff;
    }
    for (int x = 0; x <= slices; ++x) {
      float u = float(x) / float(slices);
      float th = u * kPi * 2.f;
      glm::vec3 n = t * std::cos(th) + bi * std::sin(th);
      if (v < 0.12f || v > 0.88f) n = glm::normalize(n * rr + nOff);
      m.vertices.push_back({c + n * rr, n, {u, v}, mat, 0.f});
    }
  }
  int stride = slices + 1;
  for (int y = 0; y < rings; ++y) {
    for (int x = 0; x < slices; ++x) {
      uint32_t i0 = base + uint32_t(y * stride + x);
      uint32_t i1 = i0 + 1;
      uint32_t i2 = i0 + uint32_t(stride);
      uint32_t i3 = i2 + 1;
      m.indices.insert(m.indices.end(), {i0, i2, i1, i1, i2, i3});
    }
  }
}

void pushEar(BoltPartMesh& m, glm::vec3 base, float side) {
  glm::vec3 tip = base + glm::vec3(side * 0.05f, 0.42f, -0.06f);
  glm::vec3 b0 = base + glm::vec3(side * 0.0f, 0.f, 0.08f);
  glm::vec3 b1 = base + glm::vec3(side * 0.16f, 0.f, -0.06f);
  glm::vec3 b2 = base + glm::vec3(side * -0.04f, 0.f, -0.1f);
  auto tri = [&](glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, float mat) {
    glm::vec3 n = glm::normalize(glm::cross(p1 - p0, p2 - p0));
    // ensure outward (approx)
    if (glm::dot(n, glm::vec3(side, 0.3f, 0.2f)) < 0.f) n = -n;
    uint32_t i = static_cast<uint32_t>(m.vertices.size());
    m.vertices.push_back({p0, n, {0.f, 0.f}, mat, 0.f});
    m.vertices.push_back({p1, n, {1.f, 0.f}, mat, 0.f});
    m.vertices.push_back({p2, n, {0.5f, 1.f}, mat, 0.f});
    m.indices.insert(m.indices.end(), {i, i + 1, i + 2});
  };
  tri(b0, b1, tip, FUR);
  tri(b1, b2, tip, FUR);
  tri(b2, b0, tip, FUR);
  // inner pink
  glm::vec3 inset(-side * 0.015f, 0.02f, 0.02f);
  tri(b0 + inset, tip + inset * 0.4f, b1 + inset, EAR);
}

void buildLeg(BoltPartMesh& m, float /*sideHint*/) {
  // Local space: hip at origin, foot at -Y, slight +Z paw
  // Upper leg
  pushCapsule(m, {0.f, 0.f, 0.f}, {0.02f, -0.38f, 0.02f}, 0.085f, 10, 6, FUR);
  // Lower leg
  pushCapsule(m, {0.02f, -0.38f, 0.02f}, {0.03f, -0.72f, 0.04f}, 0.065f, 10, 6, FUR);
  // Paw
  pushSphere(m, {0.03f, -0.78f, 0.1f}, 0.09f, 10, 6, PAD, {1.15f, 0.55f, 1.35f});
}

void buildBody(BoltPartMesh& m) {
  // Athletic GSD facing +Z, feet plane y=0 for full character
  // Torso along Z (elongated) — hip back, chest forward
  pushCapsule(m, {0.f, 0.92f, -0.55f}, {0.f, 0.98f, 0.5f}, 0.30f, 14, 8, FUR);
  // Chest
  pushSphere(m, {0.f, 0.95f, 0.55f}, 0.40f, 14, 10, FUR, {1.0f, 1.05f, 1.12f});
  // Hip
  pushSphere(m, {0.f, 0.90f, -0.55f}, 0.34f, 14, 10, FUR, {1.05f, 0.95f, 1.05f});
  // Belly
  pushSphere(m, {0.f, 0.68f, 0.05f}, 0.24f, 12, 8, FUR, {0.8f, 0.55f, 1.35f});
  // Back volume
  pushSphere(m, {0.f, 1.12f, 0.0f}, 0.26f, 12, 8, FUR, {0.8f, 0.5f, 1.25f});
  // Neck
  pushCapsule(m, {0.f, 1.05f, 0.65f}, {0.f, 1.28f, 0.95f}, 0.20f, 12, 6, FUR);
  // Head
  glm::vec3 hc{0.f, 1.38f, 1.15f};
  pushSphere(m, hc, 0.28f, 14, 10, FUR, {0.95f, 0.9f, 1.18f});
  // Snout
  pushCapsule(m, hc + glm::vec3(0, -0.02f, 0.12f), hc + glm::vec3(0, -0.04f, 0.48f), 0.095f, 10, 6,
              FUR);
  // Nose
  pushSphere(m, hc + glm::vec3(0, -0.02f, 0.55f), 0.065f, 10, 6, NOSE, {1.25f, 0.85f, 0.9f});
  // Cheeks
  pushSphere(m, hc + glm::vec3(0.15f, -0.04f, 0.08f), 0.11f, 10, 6, FUR);
  pushSphere(m, hc + glm::vec3(-0.15f, -0.04f, 0.08f), 0.11f, 10, 6, FUR);
  // Eyes
  pushSphere(m, hc + glm::vec3(0.10f, 0.04f, 0.28f), 0.05f, 10, 6, EYE);
  pushSphere(m, hc + glm::vec3(-0.10f, 0.04f, 0.28f), 0.05f, 10, 6, EYE);
  pushSphere(m, hc + glm::vec3(0.10f, 0.04f, 0.32f), 0.022f, 8, 4, NOSE);
  pushSphere(m, hc + glm::vec3(-0.10f, 0.04f, 0.32f), 0.022f, 8, 4, NOSE);
  // Ears
  pushEar(m, hc + glm::vec3(0.12f, 0.16f, -0.04f), 1.f);
  pushEar(m, hc + glm::vec3(-0.12f, 0.16f, -0.04f), -1.f);
  // Shoulder energy nodes
  pushSphere(m, {0.26f, 1.08f, 0.28f}, 0.07f, 8, 4, EYE, {1.1f, 0.7f, 1.2f});
  pushSphere(m, {-0.26f, 1.08f, 0.28f}, 0.07f, 8, 4, EYE, {1.1f, 0.7f, 1.2f});
}

void buildAura(BoltPartMesh& m) {
  // Soft shell around torso for lightning aura
  pushCapsule(m, {0.f, 0.92f, -0.6f}, {0.f, 0.98f, 0.55f}, 0.42f, 12, 6, AURA);
  pushSphere(m, {0.f, 0.95f, 0.55f}, 0.50f, 12, 8, AURA, {1.05f, 1.1f, 1.15f});
  pushSphere(m, {0.f, 0.90f, -0.55f}, 0.44f, 12, 8, AURA);
}

void buildProcedural(BoltCharacterMeshes& out) {
  for (auto& p : out.parts) {
    p.vertices.clear();
    p.indices.clear();
  }
  buildBody(out.parts[static_cast<int>(BoltPart::Body)]);
  buildLeg(out.parts[static_cast<int>(BoltPart::LegFL)], 1.f);
  buildLeg(out.parts[static_cast<int>(BoltPart::LegFR)], -1.f);
  buildLeg(out.parts[static_cast<int>(BoltPart::LegBL)], 1.f);
  buildLeg(out.parts[static_cast<int>(BoltPart::LegBR)], -1.f);
  // Tail in local space, base at origin, extends -Z / up
  pushCapsule(out.parts[static_cast<int>(BoltPart::Tail)], {0.f, 0.f, 0.f}, {0.04f, 0.28f, -0.38f},
              0.065f, 10, 5, FUR);
  pushSphere(out.parts[static_cast<int>(BoltPart::Tail)], {0.05f, 0.32f, -0.42f}, 0.08f, 10, 5, FUR);
  buildAura(out.parts[static_cast<int>(BoltPart::Aura)]);
}

} // namespace

bool buildOrLoadBoltCharacter(BoltCharacterMeshes& out, const std::string& preferPath) {
  for (auto& p : out.parts) {
    p.vertices.clear();
    p.indices.clear();
  }
  out.fullMesh = false;

  auto tryLoadFull = [&](const std::string& path) -> bool {
    if (path.empty() || !std::filesystem::exists(path)) return false;
    ObjMesh om;
    const bool isGltf = path.size() > 4 && (path.ends_with(".glb") || path.ends_with(".GLB") ||
                                            path.ends_with(".gltf") || path.ends_with(".GLTF"));
    const bool isObj = path.size() > 4 && (path.ends_with(".obj") || path.ends_with(".OBJ"));
    bool ok = false;
    if (isGltf) ok = loadGltfMesh(path, om);
    else if (isObj) ok = loadObj(path, om, 0.f);
    if (!ok || om.vertices.empty()) return false;

    // Full imported mesh as Body only
    out.parts[static_cast<int>(BoltPart::Body)].vertices = std::move(om.vertices);
    out.parts[static_cast<int>(BoltPart::Body)].indices = std::move(om.indices);
    // Keep aura shell for lightning (procedural)
    buildAura(out.parts[static_cast<int>(BoltPart::Aura)]);
    out.fullMesh = true;
    logInfo("Bolt FULL mesh imported: " + path + " (+ aura shell)");
    return true;
  };

  // Prefer explicit path, then standard asset locations
  if (tryLoadFull(preferPath)) return true;
  if (tryLoadFull("assets/characters/bolt/bolt_gsd.glb")) return true;
  if (tryLoadFull("assets/characters/bolt/bolt_gsd.gltf")) return true;
  if (tryLoadFull("assets/characters/bolt/bolt_gsd.obj")) return true;

  buildProcedural(out);
  out.fullMesh = false;
  logInfo("Bolt multi-part procedural fallback (no bolt_gsd.glb/obj found)");
  return true;
}

bool saveBoltCharacterObj(const BoltCharacterMeshes& meshes, const std::string& objPath) {
  // Merge body only for artist reference
  const auto& body = meshes.parts[static_cast<int>(BoltPart::Body)];
  return saveObj(objPath, body.vertices, body.indices);
}

void boltAnimTransforms(float phase, float speedFactor, float energy,
                        std::array<glm::mat4, static_cast<int>(BoltPart::Count)>& outLocal) {
  // Default identity for unused parts
  for (auto& m : outLocal) m = glm::mat4(1.f);

  const float sp = std::clamp(speedFactor, 0.f, 1.5f);
  const float hop = sp > 0.08f ? 1.f : 0.2f;
  float bob = std::sin(phase * kPi * 2.f) * 0.04f * hop;
  float lean = -0.1f * sp;
  // Whole-body run bounce (works for full imported mesh + multi-part)
  {
    glm::mat4 M = glm::translate(glm::mat4(1.f), glm::vec3(0.f, bob, 0.f));
    M = glm::rotate(M, lean, glm::vec3(1, 0, 0));
    // subtle stretch when sprinting
    float stretch = 1.f + sp * 0.03f;
    M = glm::scale(M, glm::vec3(1.f / stretch, stretch, stretch));
    outLocal[static_cast<int>(BoltPart::Body)] = M;
  }

  auto legMat = [&](float x, float z, float phaseOff) {
    float ph = phase * kPi * 2.f + phaseOff;
    float swing = std::sin(ph) * 0.55f * hop;
    float lift = std::max(0.f, std::sin(ph)) * 0.14f * hop;
    float stride = std::cos(ph) * 0.08f * hop;
    glm::mat4 M(1.f);
    M = glm::translate(M, glm::vec3(x, 0.88f + lift + bob, z + stride));
    M = glm::rotate(M, swing * 0.65f + lean, glm::vec3(1, 0, 0));
    return M;
  };
  outLocal[static_cast<int>(BoltPart::LegFL)] = legMat(0.20f, 0.32f, 0.f);
  outLocal[static_cast<int>(BoltPart::LegFR)] = legMat(-0.20f, 0.32f, kPi);
  outLocal[static_cast<int>(BoltPart::LegBL)] = legMat(0.22f, -0.42f, kPi);
  outLocal[static_cast<int>(BoltPart::LegBR)] = legMat(-0.22f, -0.42f, 0.f);

  {
    float wag = std::sin(phase * kPi * 4.f) * 0.35f * (0.3f + sp);
    glm::mat4 M = glm::translate(glm::mat4(1.f), glm::vec3(0.f, 1.0f + bob, -0.72f));
    M = glm::rotate(M, wag, glm::vec3(0, 1, 0));
    M = glm::rotate(M, 0.35f + sp * 0.2f, glm::vec3(1, 0, 0));
    outLocal[static_cast<int>(BoltPart::Tail)] = M;
  }

  {
    float pulse = 1.f + energy * 0.1f + std::sin(phase * kPi * 6.f) * 0.025f * energy;
    glm::mat4 M = outLocal[static_cast<int>(BoltPart::Body)];
    M = glm::scale(M, glm::vec3(pulse * 1.08f));
    outLocal[static_cast<int>(BoltPart::Aura)] = M;
  }
}

} // namespace bolt
