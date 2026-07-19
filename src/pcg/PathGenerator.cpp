#include "bolt/pcg/PathGenerator.hpp"
#include <algorithm>
#include <cmath>

namespace bolt {
namespace {

float hash2(float x, float z) {
  const float s = std::sin(x * 127.1f + z * 311.7f) * 43758.5453f;
  return s - std::floor(s);
}

float smooth01(float e0, float e1, float x) {
  const float t = std::clamp((x - e0) / (e1 - e0 + 1e-6f), 0.f, 1.f);
  return t * t * (3.f - 2.f * t);
}

/** Build a meandering polyline from start along forward, with lateral meander + optional goal pull */
std::vector<PathPoint> buildPolyline(const glm::vec3& start, float yaw, float length, int segments,
                                     float meanderAmp, float seed, const HeightField& height,
                                     float score, float elevate, const glm::vec3* goal,
                                     float goalPull) {
  std::vector<PathPoint> pts;
  if (segments < 2 || length < 1.f) return pts;
  const float fx = std::sin(yaw), fz = std::cos(yaw);
  const float rx = std::cos(yaw), rz = -std::sin(yaw);
  pts.reserve(static_cast<size_t>(segments + 1));

  const glm::vec3 cursor = start;
  for (int i = 0; i <= segments; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(segments);
    const float dist = t * length;
    float side = std::sin(t * 6.28318f * 1.1f + seed) * meanderAmp +
                 std::sin(t * 6.28318f * 2.7f + seed * 2.1f) * (meanderAmp * 0.38f) +
                 std::sin(t * 6.28318f * 0.35f + seed * 0.5f) * (meanderAmp * 0.55f);

    glm::vec3 p =
        cursor + glm::vec3(fx, 0.f, fz) * dist + glm::vec3(rx, 0.f, rz) * side;

    // Steer toward goal (ruin / feature) — stronger mid-path
    if (goal) {
      const float pull = goalPull * smooth01(0.1f, 0.85f, t) * (1.f - t * 0.25f);
      const float gdx = goal->x - p.x;
      const float gdz = goal->z - p.z;
      p.x += gdx * pull;
      p.z += gdz * pull;
    }

    // Bridge arch elevation mid-segment
    float elev = elevate;
    if (elevate > 0.05f) {
      elev *= std::sin(t * 3.14159265f); // ramp up and down
    }
    p.y = height.sample(p.x, p.z, score) + 0.1f + elev;
    pts.push_back({p});
  }
  return pts;
}

void densifyExclusion(PathNetwork& net, const PathPolyline& pl, float hw) {
  if (pl.points.size() < 2) return;
  for (size_t i = 0; i < pl.points.size(); ++i) {
    net.exclusionSamples.push_back(pl.points[i].position);
    // mid samples for denser exclusion
    if (i + 1 < pl.points.size()) {
      glm::vec3 m = 0.5f * (pl.points[i].position + pl.points[i + 1].position);
      net.exclusionSamples.push_back(m);
    }
  }
  net.exclusionHalfWidth = std::max(net.exclusionHalfWidth, hw);
}

} // namespace

bool PathGenerator::nearPath(const glm::vec3& pos, const PathNetwork& net, float extraMargin) {
  if (net.exclusionSamples.empty()) return false;
  const float r = net.exclusionHalfWidth + extraMargin;
  const float r2 = r * r;
  for (const auto& s : net.exclusionSamples) {
    const float dx = pos.x - s.x;
    const float dz = pos.z - s.z;
    if (dx * dx + dz * dz < r2) return true;
  }
  return false;
}

std::vector<PathPoint> PathGenerator::generate(const SprintCore& sprint, const HeightField& height,
                                               float length, int segments) const {
  auto net = generateNetwork(sprint, height, length, segments, nullptr);
  if (net.paths.empty()) return {};
  return net.paths[0].points;
}

PathNetwork PathGenerator::generateNetwork(const SprintCore& sprint, const HeightField& height,
                                           float length, int segments,
                                           const glm::vec3* goalXZ) const {
  PathNetwork net;
  const float score = std::clamp(sprint.score, 0.f, 1.4f);
  const float fx = std::sin(sprint.yaw);
  const float fz = std::cos(sprint.yaw);

  const float startAhead = 7.f + score * 2.f;
  const glm::vec3 origin = sprint.position + glm::vec3(fx, 0.f, fz) * startAhead;
  const float seed = sprint.position.x * 0.017f + sprint.position.z * 0.013f + score * 0.3f;

  // Principal energy highway only (branches / hidden / bridges disabled for now)
  PathPolyline main;
  main.kind = PathKind::Main;
  main.halfWidthMul = 1.f;
  main.glowMul = 1.f + score * 0.65f;
  main.elevate = 0.f;
  const float meander = 3.2f + score * 1.8f;
  main.points = buildPolyline(origin, sprint.yaw, length, segments, meander, seed, height, score,
                              0.f, goalXZ, goalXZ ? (0.12f + score * 0.18f) : 0.f);
  densifyExclusion(net, main, ribbonHalfWidth(score) * main.halfWidthMul + 1.2f);
  net.paths.push_back(std::move(main));

  return net;
}

} // namespace bolt
