#include "bolt/render/RenderGraph.hpp"
#include "bolt/core/Log.hpp"

namespace bolt {

void RenderGraph::clear() { passes_.clear(); }

void RenderGraph::addPass(std::string name, std::function<void()> exec) {
  passes_.push_back({std::move(name), std::move(exec)});
}

void RenderGraph::buildCrystalFrame(const SprintCore& sprint, const QualitySettings& quality) {
  clear();

  const float lod = quality.lodBias + sprint.speed * 0.015f;
  const float particles = quality.particleScale * (0.4f + sprint.score * 0.7f);

  addPass("Shadow/DepthPrepass", [] {
    // TODO: depth-only for solid terrain + bolt
  });

  addPass("Terrain", [gpu = quality.gpuTerrainHeight, score = sprint.score] {
    (void)gpu;
    (void)score;
    // TODO: draw heightfield; if gpu, displace in VS with Crystal noise
  });

  addPass("FoliageInstanced", [lod] {
    (void)lod;
    // TODO: indirect draw GPU instances; drop LOD with sprint speed
  });

  addPass("Paths", [] {
    // TODO: emissive path ribbons
  });

  addPass("Bolt", [i = sprint.score] {
    (void)i;
    // TODO: character + lightning aura
  });

  addPass("Particles", [particles] {
    (void)particles;
    // TODO: crystal motes density from sprint
  });

  if (quality.motionBlur && sprint.speed > 12.f) {
    addPass("PostMotionBlur", [s = sprint.speed] {
      (void)s;
      // TODO: velocity buffer blur scaled by speed
    });
  }

  addPass("UIDebug", [] {
    // TODO: ImGui score / FPS
  });
}

void RenderGraph::execute() {
  for (auto& p : passes_) {
    if (p.execute) p.execute();
  }
}

} // namespace bolt
