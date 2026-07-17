#include "bolt/app/Application.hpp"
#include "bolt/ecs/Components.hpp"
#include "bolt/core/Log.hpp"
#include <GLFW/glfw3.h>
#include <cmath>

namespace bolt {

bool Application::init() {
  if (!glfwInit()) {
    logError("GLFW init failed");
    return false;
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  window_ = glfwCreateWindow(1280, 720, "Bolt Engine — Crystal Nebula Plains", nullptr, nullptr);
  if (!window_) {
    logError("GLFW window failed");
    glfwTerminate();
    return false;
  }

  materials_.setRoot("assets");
  materials_.scanAndLoad();

  vulkan_.initialize(window_); // may be stub

  ctx_.quality = QualityTier::High;
  ctx_.budgets = SpawnBudgets::forQuality(static_cast<int>(ctx_.quality));

  createCrystalScene();
  running_ = true;
  logInfo("Application ready — Crystal Nebula vertical slice");
  return true;
}

void Application::createCrystalScene() {
  // Player / Bolt
  auto player = registry_.create();
  registry_.emplace<PlayerTag>(player);
  registry_.emplace<Transform>(player, Transform{glm::vec3(0.f, 1.f, 0.f)});
  registry_.emplace<Velocity>(player);
  registry_.emplace<BoltAura>(player);
  registry_.emplace<NameComponent>(player, "Bolt");
  registry_.emplace<MeshRenderer>(player, MeshRenderer{1, materials_.findByName("crystal_bolt"), false});

  // Terrain root chunk
  auto terrain = registry_.create();
  TerrainChunk tc;
  tc.cx = 0;
  tc.cz = 0;
  tc.material = materials_.findByName("crystal_ground");
  tc.scoreBake = 0.f;
  tc.gpuHeight = qualitySettings(ctx_.quality).gpuTerrainHeight;
  registry_.emplace<TerrainChunk>(terrain, tc);
  registry_.emplace<NameComponent>(terrain, "Terrain_0_0");

  ctx_.sprint.position = glm::vec3(0.f, 1.f, 0.f);
  ctx_.sprint.yaw = 0.f;
}

void Application::fixedUpdate(float dt) {
  // Sample input → velocity (minimal)
  auto view = registry_.view<Velocity, PlayerTag>();
  for (auto e : view) {
    auto& vel = view.get<Velocity>(e);
    glm::vec3 wish{0.f};
    if (glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS) wish.z += 1.f;
    if (glfwGetKey(window_, GLFW_KEY_S) == GLFW_PRESS) wish.z -= 1.f;
    if (glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS) wish.x -= 1.f;
    if (glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS) wish.x += 1.f;
    ctx_.sprint.sprinting = glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                            glfwGetKey(window_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    if (glm::length(wish) > 0.f) wish = glm::normalize(wish);
    // Rotate wish by yaw
    const float yaw = ctx_.sprint.yaw;
    const glm::vec3 worldWish{
        wish.x * std::cos(yaw) + wish.z * std::sin(yaw),
        0.f,
        -wish.x * std::sin(yaw) + wish.z * std::cos(yaw)};
    const float acc = ctx_.sprint.sprinting ? 42.f : 20.f;
    vel.linear += worldWish * acc * dt;
    // Friction
    const float fr = ctx_.sprint.sprinting ? 1.4f : 8.5f;
    vel.linear *= std::max(0.f, 1.f - fr * dt);
    const float maxSp = (ctx_.sprint.sprinting ? 30.f : 12.f) * (1.f + ctx_.sprint.momentum * 0.85f);
    const float sp = glm::length(glm::vec2(vel.linear.x, vel.linear.z));
    if (sp > maxSp) vel.linear *= maxSp / sp;
  }

  runSimulationSystems(registry_, ctx_, dt);
}

void Application::frameUpdate(float dt) {
  // Mouse look stub — hold right mouse later
  (void)dt;
  materials_.pollHotReload();
  runSpawnSystems(registry_, ctx_, dt);
  ctx_.elapsed = time_.elapsed;
}

void Application::render() {
  vulkan_.beginFrame();
  graph_.buildCrystalFrame(ctx_.sprint, qualitySettings(ctx_.quality));
  graph_.execute();
  vulkan_.endFrame();
}

void Application::run() {
  double last = glfwGetTime();
  while (running_ && !glfwWindowShouldClose(window_)) {
    glfwPollEvents();
    if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;

    const double now = glfwGetTime();
    time_.beginFrame(static_cast<float>(now - last));
    last = now;

    const int steps = time_.consumeFixedSteps();
    for (int i = 0; i < steps; ++i) fixedUpdate(Time::kFixedDt);
    if (steps == 0) fixedUpdate(Time::kFixedDt);

    frameUpdate(time_.realDt);
    render();
  }
}

void Application::shutdown() {
  vulkan_.shutdown();
  if (window_) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
  }
  glfwTerminate();
  logInfo("Application shutdown");
}

} // namespace bolt
