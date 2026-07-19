#include "bolt/pcg/BoltGsd.hpp"
#include "bolt/assets/ObjLoader.hpp"
#include "bolt/assets/GltfLoader.hpp"
#include "bolt/core/Log.hpp"
#include <array>
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
  // Tight shell around torso (import scale ~2m dog)
  pushCapsule(m, {0.f, 0.85f, -0.55f}, {0.f, 0.95f, 0.65f}, 0.38f, 12, 6, AURA);
  pushSphere(m, {0.f, 0.95f, 0.55f}, 0.42f, 12, 8, AURA, {1.0f, 1.05f, 1.1f});
  pushSphere(m, {0.f, 0.85f, -0.5f}, 0.38f, 12, 8, AURA);
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

/** Region ids for partitioning an imported full dog mesh into animatable parts. */
enum class MeshRegion : int { Body = 0, FL, FR, BL, BR, Tail, Count };

MeshRegion classifyVertex(const glm::vec3& p) {
  // Tail: behind hips, mid/high
  if (p.z < -0.48f && p.y > 0.55f && p.y < 1.55f && std::abs(p.x) < 0.42f) return MeshRegion::Tail;
  // Legs: below hip, offset from midline
  if (p.y < 0.95f && p.y > -0.02f && std::abs(p.x) > 0.055f) {
    if (p.z > 0.04f) return (p.x > 0.f) ? MeshRegion::FL : MeshRegion::FR;
    if (p.z < -0.04f) return (p.x > 0.f) ? MeshRegion::BL : MeshRegion::BR;
  }
  return MeshRegion::Body;
}

BoltPart regionToPart(MeshRegion r) {
  switch (r) {
  case MeshRegion::FL: return BoltPart::LegFL;
  case MeshRegion::FR: return BoltPart::LegFR;
  case MeshRegion::BL: return BoltPart::LegBL;
  case MeshRegion::BR: return BoltPart::LegBR;
  case MeshRegion::Tail: return BoltPart::Tail;
  default: return BoltPart::Body;
  }
}

glm::vec3 regionHip(MeshRegion r) {
  // Must match boltAnimTransforms leg/tail attach points
  switch (r) {
  case MeshRegion::FL: return {0.20f, 0.88f, 0.32f};
  case MeshRegion::FR: return {-0.20f, 0.88f, 0.32f};
  case MeshRegion::BL: return {0.22f, 0.88f, -0.42f};
  case MeshRegion::BR: return {-0.22f, 0.88f, -0.42f};
  case MeshRegion::Tail: return {0.f, 1.0f, -0.72f};
  default: return {0.f, 0.f, 0.f};
  }
}

/**
 * Split imported full mesh into Body + 4 legs + Tail so matrix animation works.
 * Limb verts are stored relative to hip/tail base (local space for boltAnimTransforms).
 */
bool partitionImportedMesh(BoltCharacterMeshes& out) {
  auto& src = out.parts[static_cast<int>(BoltPart::Body)];
  if (src.vertices.empty() || src.indices.size() < 3) return false;

  const auto srcV = std::move(src.vertices);
  const auto srcI = std::move(src.indices);

  // Clear body/legs/tail (keep aura if already built)
  for (int i = 0; i < static_cast<int>(BoltPart::Aura); ++i) {
    out.parts[static_cast<size_t>(i)].vertices.clear();
    out.parts[static_cast<size_t>(i)].indices.clear();
  }

  std::array<std::vector<int>, static_cast<int>(MeshRegion::Count)> remap;
  auto addVert = [&](MeshRegion reg, uint32_t srcIdx) -> uint32_t {
    const int ri = static_cast<int>(reg);
    if (remap[static_cast<size_t>(ri)].empty())
      remap[static_cast<size_t>(ri)].assign(srcV.size(), -1);
    int& slot = remap[static_cast<size_t>(ri)][srcIdx];
    if (slot >= 0) return static_cast<uint32_t>(slot);

    VertexPC v = srcV[srcIdx];
    if (reg != MeshRegion::Body) {
      const glm::vec3 hip = regionHip(reg);
      v.pos -= hip;
    }
    auto& part = out.parts[static_cast<int>(regionToPart(reg))];
    slot = static_cast<int>(part.vertices.size());
    part.vertices.push_back(v);
    return static_cast<uint32_t>(slot);
  };

  size_t legTris = 0, bodyTris = 0, tailTris = 0;
  for (size_t t = 0; t + 2 < srcI.size(); t += 3) {
    const uint32_t i0 = srcI[t], i1 = srcI[t + 1], i2 = srcI[t + 2];
    if (i0 >= srcV.size() || i1 >= srcV.size() || i2 >= srcV.size()) continue;

    const MeshRegion r0 = classifyVertex(srcV[i0].pos);
    const MeshRegion r1 = classifyVertex(srcV[i1].pos);
    const MeshRegion r2 = classifyVertex(srcV[i2].pos);

    // Majority vote; mixed → Body (avoids cracks eating torso)
    MeshRegion reg = MeshRegion::Body;
    if (r0 == r1 && r1 == r2)
      reg = r0;
    else if (r0 == r1 || r0 == r2)
      reg = r0;
    else if (r1 == r2)
      reg = r1;

    // Require pure agreement for limbs (cleaner joints)
    if (reg != MeshRegion::Body && !(r0 == r1 && r1 == r2)) reg = MeshRegion::Body;

    const uint32_t a = addVert(reg, i0);
    const uint32_t b = addVert(reg, i1);
    const uint32_t c = addVert(reg, i2);
    auto& part = out.parts[static_cast<int>(regionToPart(reg))];
    part.indices.insert(part.indices.end(), {a, b, c});

    if (reg == MeshRegion::Tail)
      ++tailTris;
    else if (reg == MeshRegion::Body)
      ++bodyTris;
    else
      ++legTris;
  }

  // Fallback: if partition failed to find legs, rebuild procedural legs only
  const bool hasFL = !out.parts[static_cast<int>(BoltPart::LegFL)].indices.empty();
  const bool hasFR = !out.parts[static_cast<int>(BoltPart::LegFR)].indices.empty();
  const bool hasBL = !out.parts[static_cast<int>(BoltPart::LegBL)].indices.empty();
  const bool hasBR = !out.parts[static_cast<int>(BoltPart::LegBR)].indices.empty();
  if (!(hasFL && hasFR && hasBL && hasBR)) {
    logWarn("Bolt mesh partition incomplete — using procedural legs under imported body");
    buildLeg(out.parts[static_cast<int>(BoltPart::LegFL)], 1.f);
    buildLeg(out.parts[static_cast<int>(BoltPart::LegFR)], -1.f);
    buildLeg(out.parts[static_cast<int>(BoltPart::LegBL)], 1.f);
    buildLeg(out.parts[static_cast<int>(BoltPart::LegBR)], -1.f);
  }
  if (out.parts[static_cast<int>(BoltPart::Tail)].indices.empty()) {
    pushCapsule(out.parts[static_cast<int>(BoltPart::Tail)], {0.f, 0.f, 0.f}, {0.04f, 0.28f, -0.38f},
                0.065f, 10, 5, FUR);
    pushSphere(out.parts[static_cast<int>(BoltPart::Tail)], {0.05f, 0.32f, -0.42f}, 0.08f, 10, 5, FUR);
  }

  logInfo("Bolt mesh partitioned: bodyTris=" + std::to_string(bodyTris) +
          " legTris=" + std::to_string(legTris) + " tailTris=" + std::to_string(tailTris));
  return bodyTris > 0 && legTris > 10;
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

    // Tag likely eye vertices (bright cyan materials lost on import — use position heuristic)
    // After normalize: height ~2m, nose +Z; eyes sit high on head
    for (auto& v : om.vertices) {
      // head region: high Y, forward Z
      if (v.pos.y > 1.35f && v.pos.z > 0.35f && std::abs(v.pos.x) > 0.06f &&
          std::abs(v.pos.x) < 0.35f && v.pos.y < 1.85f) {
        // small clusters on face sides → energy eyes
        if (v.pos.z > 0.55f) v.matId = 1.f;
      }
    }
    out.parts[static_cast<int>(BoltPart::Body)].vertices = std::move(om.vertices);
    out.parts[static_cast<int>(BoltPart::Body)].indices = std::move(om.indices);
    // Keep aura shell for lightning (procedural)
    buildAura(out.parts[static_cast<int>(BoltPart::Aura)]);

    // Split into animatable legs/tail — matrix gait (not frozen full mesh)
    if (partitionImportedMesh(out)) {
      out.fullMesh = false; // multi-part matrix animation
      logInfo("Bolt imported mesh + animated limbs: " + path);
    } else {
      out.fullMesh = true; // VS deform fallback
      logInfo("Bolt FULL mesh imported (VS limb deform): " + path + " (+ aura shell)");
    }
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

float boltJumpHeightOffset(float jumpT) {
  constexpr float pi = 3.14159265f;
  const float t = std::clamp(jumpT, 0.f, 1.f);
  // Crouch dip (0–0.18) → launch peak (~0.45) → land settle (1.0)
  if (t < 0.18f) {
    const float u = t / 0.18f;
    return -0.12f * std::sin(u * pi * 0.5f);
  }
  if (t < 0.55f) {
    const float u = (t - 0.18f) / 0.37f;
    return -0.12f + 1.55f * std::sin(u * pi); // air arc
  }
  const float u = (t - 0.55f) / 0.45f;
  // soft land squash then recover toward 0
  return 0.15f * (1.f - u) * std::sin((1.f - u) * pi);
}

float boltLateralSway(float phase, float speedFactor, float turnRate, BoltMotion motion) {
  const float sp = std::clamp(speedFactor, 0.f, 1.5f);
  const float hop = (motion == BoltMotion::Run && sp > 0.08f) ? std::clamp(0.75f + sp * 0.5f, 0.75f, 1.35f)
                    : (motion == BoltMotion::Jump)              ? 0.4f
                                                                : 0.15f;
  const float gait = phase * kPi * 2.f;
  // Gait weave (breaks laser-line path) + lean out of turn
  const float weave = std::sin(gait) * 0.07f * hop + std::sin(gait * 0.5f + 1.1f) * 0.04f * hop;
  const float turnShift = std::clamp(turnRate * 0.045f, -0.14f, 0.14f); // +right when turning left rate?
  // yaw increases CCW: positive turnRate → lean right (centrifugal feel uses -turn)
  return weave - turnShift;
}

void boltAnimTransforms(float phase, float speedFactor, float energy, BoltMotion motion,
                        float jumpT, float turnRate,
                        std::array<glm::mat4, static_cast<int>(BoltPart::Count)>& outLocal) {
  for (auto& m : outLocal) m = glm::mat4(1.f);

  const float sp = std::clamp(speedFactor, 0.f, 1.5f);
  const bool idle = motion == BoltMotion::Idle;
  const bool jumping = motion == BoltMotion::Jump;
  const float hop = jumping ? 0.45f : (idle ? 0.2f : (sp > 0.08f ? std::clamp(0.75f + sp * 0.55f, 0.75f, 1.4f) : 0.4f));

  // Turn bank: roll into the turn (not locked to straight-axis run)
  const float turn = std::clamp(turnRate, -4.5f, 4.5f);
  const float bank = std::clamp(-turn * 0.14f, -0.42f, 0.42f) * (idle ? 0.15f : hop);
  const float turnYaw = std::clamp(-turn * 0.06f, -0.22f, 0.22f); // nose leads into turn slightly

  const float gait = phase * kPi * 2.f;
  const float s1 = std::sin(gait);
  const float c1 = std::cos(gait);
  const float s2 = std::sin(gait * 2.f);

  float bob = 0.f;
  float lean = 0.f;
  float roll = 0.f;
  float yawRock = 0.f;
  float stretchY = 1.f;
  float stretchXZ = 1.f;
  float headNod = 0.f;
  float headYaw = 0.f;

  if (idle) {
    bob = s1 * 0.028f;
    stretchY = 1.f + s1 * 0.022f;
    stretchXZ = 1.f / stretchY;
    lean = 0.025f * std::sin(gait + 0.4f);
    headNod = s1 * 0.04f;
    headYaw = std::sin(gait * 0.5f) * 0.06f + turnYaw * 0.5f;
    roll = s1 * 0.015f + bank * 0.5f;
    yawRock = turnYaw * 0.4f;
  } else if (jumping) {
    const float t = std::clamp(jumpT, 0.f, 1.f);
    if (t < 0.18f) {
      const float u = t / 0.18f;
      stretchY = 1.f - 0.16f * u;
      stretchXZ = 1.f + 0.12f * u;
      bob = -0.08f * u;
      lean = 0.18f * u;
      headNod = 0.12f * u;
      roll = bank * 0.6f;
    } else if (t < 0.55f) {
      const float u = (t - 0.18f) / 0.37f;
      stretchY = 0.84f + 0.32f * std::sin(u * kPi);
      stretchXZ = 1.f / std::max(0.72f, stretchY);
      bob = 0.1f * std::sin(u * kPi);
      lean = -0.32f * std::sin(u * kPi);
      headNod = -0.15f * std::sin(u * kPi);
      roll = std::sin(u * kPi * 2.f) * 0.06f + bank;
      yawRock = turnYaw;
    } else {
      const float u = (t - 0.55f) / 0.45f;
      const float squash = (1.f - u) * 0.18f;
      stretchY = 1.f - squash;
      stretchXZ = 1.f + squash * 0.75f;
      bob = -0.05f * (1.f - u);
      lean = 0.1f * (1.f - u);
      headNod = 0.08f * (1.f - u);
      roll = bank * (1.f - u);
    }
  } else {
    // Gallop body + free turn axis (not stuck on pure forward plane)
    bob = s1 * 0.055f * hop + s2 * 0.035f * hop;
    lean = -0.16f * sp - 0.06f * hop + c1 * 0.14f * hop + s2 * 0.04f * hop;
    // Diagonal roll + stronger bank into turns
    roll = s1 * 0.12f * hop + bank;
    // Shoulder weave + turn lead (breaks "always same axis")
    yawRock = c1 * 0.06f * hop + turnYaw + std::sin(gait * 0.37f + 0.8f) * 0.04f * hop;
    stretchY = 1.f + sp * 0.06f - std::max(0.f, s2) * 0.04f * hop;
    stretchXZ = 1.f / std::max(0.85f, stretchY);
    headNod = -lean * 0.45f + s1 * 0.08f * hop;
    // Head looks into the turn, not fixed forward
    headYaw = s1 * 0.08f * hop + yawRock * 0.65f + turnYaw * 1.1f;
  }

  {
    const glm::vec3 pivot(0.f, 0.92f, -0.05f);
    glm::mat4 M(1.f);
    M = glm::translate(M, glm::vec3(0.f, bob, 0.f));
    M = glm::translate(M, pivot);
    M = glm::rotate(M, roll, glm::vec3(0, 0, 1));
    M = glm::rotate(M, lean, glm::vec3(1, 0, 0));
    M = glm::rotate(M, yawRock, glm::vec3(0, 1, 0));
    M = glm::scale(M, glm::vec3(stretchXZ, stretchY, stretchXZ));
    M = glm::translate(M, -pivot);
    M = glm::rotate(M, headNod * 0.35f, glm::vec3(1, 0, 0));
    M = glm::rotate(M, headYaw * 0.45f, glm::vec3(0, 1, 0));
    outLocal[static_cast<int>(BoltPart::Body)] = M;
  }

  // Outer legs take longer stride when turning (asymmetric gait)
  auto legMat = [&](float x, float z, float phaseOff, bool isLeft) {
    float ph = gait + phaseOff;
    float outer = 1.f;
    if (std::abs(turn) > 0.15f) {
      // turning left (turn>0): left legs inside → shorter; right outer → longer
      const bool outerLeg = (turn > 0.f) ? !isLeft : isLeft;
      outer = outerLeg ? (1.f + std::min(0.35f, std::abs(turn) * 0.08f))
                       : (1.f - std::min(0.22f, std::abs(turn) * 0.05f));
    }
    float swingAmp = jumping ? 0.38f : (idle ? 0.14f : 0.9f * hop * outer);
    float swing = std::sin(ph) * swingAmp;
    float lift = std::max(0.f, std::sin(ph)) * (idle ? 0.035f : 0.24f * hop * outer);
    float stride = std::cos(ph) * (idle ? 0.02f : 0.13f * hop * outer);
    if (jumping && jumpT > 0.2f && jumpT < 0.7f) {
      lift += 0.14f;
      swing *= 0.45f;
    }
    // Hip shifts outward on turn
    const float hipOut = bank * (isLeft ? -0.06f : 0.06f);
    glm::mat4 M(1.f);
    M = glm::translate(M, glm::vec3(x + roll * 0.05f + hipOut, 0.88f + lift + bob * 0.85f,
                                    z + stride));
    M = glm::rotate(M, swing * 0.88f + lean * 0.35f, glm::vec3(1, 0, 0));
    M = glm::rotate(M, -roll * 0.35f + bank * 0.25f, glm::vec3(0, 0, 1));
    M = glm::rotate(M, turnYaw * 0.3f, glm::vec3(0, 1, 0));
    return M;
  };
  outLocal[static_cast<int>(BoltPart::LegFL)] = legMat(0.20f, 0.32f, 0.f, true);
  outLocal[static_cast<int>(BoltPart::LegFR)] = legMat(-0.20f, 0.32f, kPi, false);
  outLocal[static_cast<int>(BoltPart::LegBL)] = legMat(0.22f, -0.42f, kPi, true);
  outLocal[static_cast<int>(BoltPart::LegBR)] = legMat(-0.22f, -0.42f, 0.f, false);

  {
    float wagRate = idle ? 2.2f : (jumping ? 3.5f : 5.f);
    float wag = std::sin(phase * kPi * wagRate) * 0.42f * (idle ? 0.3f : (0.35f + sp * 0.5f));
    wag += turnYaw * 0.4f; // tail follows turn
    float tailPitch = 0.32f + sp * 0.28f + (jumping ? 0.3f : 0.f) + s1 * 0.08f * hop;
    if (jumping) wag += 0.12f;
    glm::mat4 M =
        glm::translate(glm::mat4(1.f), glm::vec3(roll * 0.06f, 1.0f + bob * 0.9f, -0.72f));
    M = glm::rotate(M, wag, glm::vec3(0, 1, 0));
    M = glm::rotate(M, tailPitch, glm::vec3(1, 0, 0));
    M = glm::rotate(M, -roll * 0.35f, glm::vec3(0, 0, 1));
    outLocal[static_cast<int>(BoltPart::Tail)] = M;
  }

  {
    float pulseBase = 1.f + energy * 0.07f + std::sin(gait * 3.f) * 0.02f * energy;
    if (idle) pulseBase = 1.f + energy * 0.035f + s1 * 0.014f;
    if (jumping) pulseBase += 0.09f * std::sin(std::clamp(jumpT, 0.f, 1.f) * kPi);
    glm::mat4 M = outLocal[static_cast<int>(BoltPart::Body)];
    M = glm::scale(M, glm::vec3(pulseBase * 1.06f));
    outLocal[static_cast<int>(BoltPart::Aura)] = M;
  }

  (void)headNod;
  (void)headYaw;
  (void)energy;
}

} // namespace bolt
