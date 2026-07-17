#include "bolt/pcg/PathRibbon.hpp"
#include <cmath>
#include <glm/geometric.hpp>

namespace bolt {

PathRibbonCPU buildPathRibbon(const std::vector<PathPoint>& pts, float halfWidth,
                              const HeightField& height, float sprintScore) {
  PathRibbonCPU mesh;
  if (pts.size() < 2) return mesh;
  const float hw = std::max(halfWidth, 0.5f);
  const int n = static_cast<int>(pts.size());
  mesh.vertices.reserve(static_cast<size_t>(n) * 2);
  mesh.indices.reserve(static_cast<size_t>(n - 1) * 6);

  for (int i = 0; i < n; ++i) {
    glm::vec3 p = pts[static_cast<size_t>(i)].position;
    // Tangent from neighbors
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
    // Slight raise so ribbon sits above terrain without z-fight
    const float yL = height.sample(p.x - side.x * hw, p.z - side.z * hw, sprintScore) + 0.08f;
    const float yR = height.sample(p.x + side.x * hw, p.z + side.z * hw, sprintScore) + 0.08f;

    VertexPC L{}, R{};
    L.pos = {p.x - side.x * hw, yL, p.z - side.z * hw};
    R.pos = {p.x + side.x * hw, yR, p.z + side.z * hw};
    L.normal = R.normal = {0.f, 1.f, 0.f};
    L.uv = {0.f, v * 4.f};
    R.uv = {1.f, v * 4.f};
    L.matId = R.matId = 0.f;
    mesh.vertices.push_back(L);
    mesh.vertices.push_back(R);
  }

  for (int i = 0; i < n - 1; ++i) {
    const uint32_t a = static_cast<uint32_t>(i * 2);
    const uint32_t b = a + 1;
    const uint32_t c = a + 2;
    const uint32_t d = a + 3;
    mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
  }
  return mesh;
}

} // namespace bolt
