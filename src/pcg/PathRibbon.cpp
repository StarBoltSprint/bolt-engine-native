#include "bolt/pcg/PathRibbon.hpp"
#include <cmath>
#include <glm/geometric.hpp>

namespace bolt {
namespace {

void appendRibbon(PathRibbonCPU& mesh, const std::vector<PathPoint>& pts, float halfWidth,
                  const HeightField& height, float sprintScore, float elevate, float glowMat) {
  if (pts.size() < 2) return;
  const float hw = std::max(halfWidth, 0.35f);
  const int n = static_cast<int>(pts.size());
  const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
  mesh.vertices.reserve(mesh.vertices.size() + static_cast<size_t>(n) * 2);
  mesh.indices.reserve(mesh.indices.size() + static_cast<size_t>(n - 1) * 6);

  for (int i = 0; i < n; ++i) {
    glm::vec3 p = pts[static_cast<size_t>(i)].position;
    glm::vec3 t;
    if (i == 0)
      t = pts[1].position - pts[0].position;
    else if (i == n - 1)
      t = pts[static_cast<size_t>(n - 1)].position - pts[static_cast<size_t>(n - 2)].position;
    else
      t = pts[static_cast<size_t>(i + 1)].position - pts[static_cast<size_t>(i - 1)].position;
    t.y = 0.f;
    if (glm::length(t) < 1e-4f) t = glm::vec3(0, 0, 1);
    t = glm::normalize(t);
    glm::vec3 side = glm::normalize(glm::cross(glm::vec3(0, 1, 0), t));

    const float v = static_cast<float>(i) / static_cast<float>(n - 1);
    // Bridges: use path Y (already elevated); ground paths sample terrain
    float yL, yR;
    if (elevate > 0.05f) {
      yL = yR = p.y + 0.05f;
    } else {
      yL = height.sample(p.x - side.x * hw, p.z - side.z * hw, sprintScore) + 0.1f;
      yR = height.sample(p.x + side.x * hw, p.z + side.z * hw, sprintScore) + 0.1f;
    }

    VertexPC L{}, R{};
    L.pos = {p.x - side.x * hw, yL, p.z - side.z * hw};
    R.pos = {p.x + side.x * hw, yR, p.z + side.z * hw};
    L.normal = R.normal = {0.f, 1.f, 0.f};
    L.uv = {0.f, v * 4.f};
    R.uv = {1.f, v * 4.f};
    // Encode path ribbon for terrain.frag: matId > 50 means path surface (100+glow)
    L.matId = R.matId = 100.f + glowMat;
    mesh.vertices.push_back(L);
    mesh.vertices.push_back(R);
  }

  for (int i = 0; i < n - 1; ++i) {
    const uint32_t a = base + static_cast<uint32_t>(i * 2);
    const uint32_t b = a + 1;
    const uint32_t c = a + 2;
    const uint32_t d = a + 3;
    mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
  }
}

} // namespace

PathRibbonCPU buildPathRibbon(const std::vector<PathPoint>& pts, float halfWidth,
                              const HeightField& height, float sprintScore, float elevate,
                              float glowMat) {
  PathRibbonCPU mesh;
  appendRibbon(mesh, pts, halfWidth, height, sprintScore, elevate, glowMat);
  return mesh;
}

PathRibbonCPU buildPathNetworkMesh(const PathNetwork& net, float baseHalfWidth,
                                   const HeightField& height, float sprintScore) {
  PathRibbonCPU mesh;
  for (const auto& pl : net.paths) {
    if (pl.points.size() < 2) continue;
    const float hw = baseHalfWidth * pl.halfWidthMul;
    // matId: glow * kind encoding (0.5–2.0 typical)
    float glow = pl.glowMul;
    if (pl.kind == PathKind::Hidden) glow *= 0.55f;
    if (pl.kind == PathKind::Bridge) glow *= 1.25f;
    if (pl.kind == PathKind::Branch) glow *= 0.9f;
    appendRibbon(mesh, pl.points, hw, height, sprintScore, pl.elevate, glow);
  }
  return mesh;
}

} // namespace bolt
