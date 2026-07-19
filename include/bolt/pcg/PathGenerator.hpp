#pragma once
#include <algorithm>
#include <vector>
#include <glm/glm.hpp>
#include "bolt/sprint/SprintCore.hpp"
#include "bolt/world/HeightField.hpp"

namespace bolt {

struct PathPoint {
  glm::vec3 position{0.f};
};

enum class PathKind : int {
  Main = 0,    // primary energy highway
  Branch = 1,  // fork toward interest
  Hidden = 2,  // subtle trail (narrower, dimmer until near)
  Bridge = 3,  // elevated shortcut ramp
};

struct PathPolyline {
  std::vector<PathPoint> points;
  PathKind kind = PathKind::Main;
  float halfWidthMul = 1.f; // relative to base ribbon width
  float elevate = 0.f;      // meters above terrain (bridges)
  float glowMul = 1.f;      // shader emission scale via matId
};

/** Full path network ahead of Bolt */
struct PathNetwork {
  std::vector<PathPolyline> paths;
  /** Dense samples for vegetation exclusion (XZ + half-width) */
  std::vector<glm::vec3> exclusionSamples;
  float exclusionHalfWidth = 4.f;
};

class PathGenerator {
public:
  /**
   * Energy path ahead of Bolt.
   * Currently: principal (Main) highway only — meanders + optional ruin steering.
   * Branch / Hidden / Bridge kinds are reserved but not generated.
   */
  PathNetwork generateNetwork(const SprintCore& sprint, const HeightField& height,
                              float length = 48.f, int segments = 24,
                              const glm::vec3* goalXZ = nullptr) const;

  /** Legacy: main centerline only */
  std::vector<PathPoint> generate(const SprintCore& sprint, const HeightField& height,
                                  float length = 48.f, int segments = 24) const;

  float ribbonHalfWidth(float sprintScore) const {
    return 3.2f + std::clamp(sprintScore, 0.f, 1.2f) * 2.2f;
  }

  /** True if pos is within exclusion corridor of a path network */
  static bool nearPath(const glm::vec3& pos, const PathNetwork& net, float extraMargin = 0.f);
};

} // namespace bolt
