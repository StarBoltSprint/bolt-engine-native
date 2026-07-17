#pragma once
#include <entt/entt.hpp>
#include "bolt/core/Time.hpp"
#include "bolt/ecs/Systems.hpp"
#include "bolt/assets/MaterialLibrary.hpp"
#include "bolt/render/VulkanContext.hpp"
#include "bolt/render/RenderGraph.hpp"

struct GLFWwindow;

namespace bolt {

class Application {
public:
  bool init();
  void run();
  void shutdown();

private:
  void createCrystalScene();
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
  bool running_ = false;
};

} // namespace bolt
