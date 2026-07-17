#pragma once
#include <entt/entt.hpp>
#include <vector>
#include "bolt/core/Time.hpp"
#include "bolt/ecs/Systems.hpp"
#include "bolt/assets/MaterialLibrary.hpp"
#include "bolt/render/VulkanContext.hpp"
#include "bolt/render/RenderGraph.hpp"
#include "bolt/render/GpuMesh.hpp"

struct GLFWwindow;

namespace bolt {

class Application {
public:
  bool init();
  void run();
  void shutdown();
  void onResize(int w, int h);
  void toggleFullscreen();

private:
  void createCrystalScene();
  void rebuildTerrainAroundPlayer(bool force);
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
  bool running_ = false;
  int width_ = 1280;
  int height_ = 720;

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
