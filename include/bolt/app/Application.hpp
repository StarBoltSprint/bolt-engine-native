#pragma once
#include <entt/entt.hpp>
#include <algorithm>
#include <vector>
#include "bolt/core/Time.hpp"
#include "bolt/ecs/Systems.hpp"
#include "bolt/assets/MaterialLibrary.hpp"
#include "bolt/render/VulkanContext.hpp"
#include "bolt/render/RenderGraph.hpp"
#include "bolt/render/GpuMesh.hpp"

struct GLFWwindow;

namespace bolt {

struct CpuParticle {
  glm::vec3 pos{0.f};
  glm::vec3 vel{0.f};
  float life = 0.f;
  float maxLife = 1.f;
  float size = 0.3f;
  glm::vec3 color{0.5f, 0.8f, 1.f};
};

class Application {
public:
  bool init();
  void run();
  void shutdown();
  void onResize(int w, int h);
  void toggleFullscreen();
  void adjustCamZoom(float delta) {
    camDist_ = std::clamp(camDist_ - delta * 0.85f, 3.5f, 22.f);
  }

private:
  void createCrystalScene();
  void rebuildTerrainAroundPlayer(bool force);
  void updateParticles(float dt);
  void fixedUpdate(float dt);
  void frameUpdate(float dt);
  void render();

  GLFWwindow* window_ = nullptr;
  entt::registry registry_;
  EngineContext ctx_;
  Time time_;
  MaterialLibrary materials_;
  VulkanContext vulkan_;
  RenderGraph graph_;
  std::vector<FoliageInstanceGPU> foliageCpu_;
  std::vector<CpuParticle> particles_;
  std::vector<ParticleGPU> particleGpu_;
  float emitAccum_ = 0.f;
  float trailDistAccum_ = 0.f;
  glm::vec3 lastTrailPos_{0.f};
  bool boltFullMesh_ = false;
  bool running_ = false;
  int width_ = 1280;
  int height_ = 720;

  // Free orbit camera around Bolt (RMB drag + scroll zoom)
  float camOrbitYaw_ = 0.55f;   // radians — start slightly to the side (¾)
  float camOrbitPitch_ = 0.32f; // look down a bit
  float camDist_ = 8.5f;
  double mouseLastX_ = 0.0;
  double mouseLastY_ = 0.0;
  bool mouseLookActive_ = false;

  // Streaming terrain patch (follows Bolt so ground never “ends”)
  float terrainOriginX_ = 0.f;
  float terrainOriginZ_ = 0.f;
  float terrainSize_ = 640.f;
  int terrainSegs_ = 72;
  bool fullscreen_ = false;
  int windowedX_ = 100;
  int windowedY_ = 100;
  int windowedW_ = 1280;
  int windowedH_ = 720;
};

} // namespace bolt
