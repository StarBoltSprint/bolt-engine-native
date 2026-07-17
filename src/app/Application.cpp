#include "bolt/app/Application.hpp"
#include "bolt/ecs/Components.hpp"
#include "bolt/world/TerrainMesh.hpp"
#include "bolt/pcg/StalkMesh.hpp"
#include "bolt/core/Log.hpp"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

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

  glfwSetWindowUserPointer(window_, this);
  glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* w, int width, int height) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
    if (app) app->onResize(width, height);
  });
  glfwSetKeyCallback(window_, [](GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS) return;
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
    if (!app) return;
    if (key == GLFW_KEY_F11 || key == GLFW_KEY_F) app->toggleFullscreen();
  });

  materials_.setRoot("assets");
  materials_.scanAndLoad();

  if (!vulkan_.initialize(window_)) {
    logError("Vulkan init failed — install Vulkan SDK and GPU drivers");
    return false;
  }

  ctx_.quality = QualityTier::High;
  ctx_.budgets = SpawnBudgets::forQuality(static_cast<int>(ctx_.quality));
  ctx_.sprint.sprinting = false;
  ctx_.sprint.score = 0.5f;

  createCrystalScene();
  running_ = true;
  logInfo("Application ready — Crystal Nebula (WASD move, Shift sprint, F11 fullscreen, Esc quit)");
  return true;
}

void Application::onResize(int w, int h) {
  if (w > 0 && h > 0) {
    width_ = w;
    height_ = h;
    vulkan_.resize(w, h);
  }
}

void Application::toggleFullscreen() {
  if (!window_) return;
  GLFWmonitor* mon = glfwGetPrimaryMonitor();
  if (!mon) return;
  const GLFWvidmode* mode = glfwGetVideoMode(mon);
  if (!mode) return;

  if (!fullscreen_) {
    glfwGetWindowPos(window_, &windowedX_, &windowedY_);
    glfwGetWindowSize(window_, &windowedW_, &windowedH_);
    glfwSetWindowMonitor(window_, mon, 0, 0, mode->width, mode->height, mode->refreshRate);
    fullscreen_ = true;
    logInfo("Fullscreen " + std::to_string(mode->width) + "x" + std::to_string(mode->height));
  } else {
    glfwSetWindowMonitor(window_, nullptr, windowedX_, windowedY_, windowedW_, windowedH_, 0);
    fullscreen_ = false;
    logInfo("Windowed " + std::to_string(windowedW_) + "x" + std::to_string(windowedH_));
  }
  // Swapchain rebuild is driven by framebuffer size callback + framebufferResized_
}

void Application::rebuildTerrainAroundPlayer(bool force) {
  const float px = ctx_.sprint.position.x;
  const float pz = ctx_.sprint.position.z;
  const float half = terrainSize_ * 0.5f;
  // Rebuild before player walks off the patch (keep ~30% margin)
  const float margin = terrainSize_ * 0.30f;
  const float dx = std::abs(px - terrainOriginX_);
  const float dz = std::abs(pz - terrainOriginZ_);
  if (!force && dx < half - margin && dz < half - margin) return;

  terrainOriginX_ = px;
  terrainOriginZ_ = pz;
  auto cpu = buildTerrainMesh(ctx_.height, terrainSegs_, terrainSize_, terrainOriginX_,
                              terrainOriginZ_, ctx_.sprint.score);
  vulkan_.uploadTerrain(cpu.vertices, cpu.indices);
  logInfo("Terrain recentered at (" + std::to_string(static_cast<int>(px)) + ", " +
          std::to_string(static_cast<int>(pz)) + ") size=" + std::to_string(static_cast<int>(terrainSize_)));
}

void Application::createCrystalScene() {
  auto player = registry_.create();
  registry_.emplace<PlayerTag>(player);
  registry_.emplace<Transform>(player, Transform{glm::vec3(0.f, 2.f, 0.f)});
  registry_.emplace<Velocity>(player);
  registry_.emplace<BoltAura>(player);
  registry_.emplace<NameComponent>(player, "Bolt");

  // Large streaming terrain patch (follows Bolt — was 200m fixed at origin)
  const auto q = qualitySettings(ctx_.quality);
  terrainSegs_ = std::max(q.terrainSegs, 64);
  terrainSize_ = 640.f;
  terrainOriginX_ = 0.f;
  terrainOriginZ_ = 0.f;
  rebuildTerrainAroundPlayer(true);

  // Crystal biome multi-material pack (ground / rock / path / stalk)
  {
    const std::string dir = "assets/materials/crystal_nebula/";
    const bool ok = vulkan_.loadBiomeMaterials(
        dir + "crystal_ground", dir + "crystal_rock", dir + "crystal_path",
        dir + "crystal_stalk");
    if (ok) logInfo("Crystal biome multi-material active (ground+rock+path+stalk)");
    else logWarn("No crystal biome PBR maps — procedural. Run: scripts/run_grok_pipeline.ps1");
  }

  // Stalk mesh for instances
  std::vector<VertexPC> stalkV;
  std::vector<uint32_t> stalkI;
  buildStalkMesh(stalkV, stalkI);
  vulkan_.uploadStalkMesh(stalkV, stalkI);

  // Initial foliage batch around spawn
  ctx_.sprint.position = glm::vec3(0.f, 2.f, 0.f);
  ctx_.sprint.yaw = 0.f;
  ctx_.sprint.score = 0.55f;
  ctx_.sprint.sprinting = true;
  ctx_.budgets.applySprint(ctx_.sprint);

  auto initial = ctx_.vegetation.generate(ctx_.sprint, ctx_.rules, ctx_.height, ctx_.budgets, 48);
  // Seed more rings for first look
  for (int k = 0; k < 4; ++k) {
    ctx_.sprint.position.z += 20.f;
    auto more = ctx_.vegetation.generate(ctx_.sprint, ctx_.rules, ctx_.height, ctx_.budgets, 32);
    initial.insert(initial.end(), more.begin(), more.end());
  }
  ctx_.sprint.position = glm::vec3(0.f, 2.f, 0.f);

  foliageCpu_.clear();
  foliageCpu_.reserve(initial.size());
  for (auto& inst : initial) {
    FoliageInstanceGPU g;
    g.posScale = glm::vec4(inst.position, inst.scale);
    g.yawKind = glm::vec4(inst.yaw, static_cast<float>(inst.kind), 0.f, 0.f);
    foliageCpu_.push_back(g);
  }
  vulkan_.uploadFoliage(foliageCpu_);

  auto batch = registry_.create();
  FoliageBatch fb;
  fb.instanceCount = static_cast<uint32_t>(foliageCpu_.size());
  registry_.emplace<FoliageBatch>(batch, fb);
  registry_.emplace<NameComponent>(batch, "CrystalFoliageBatch");

  logInfo("Crystal scene: terrain segs=" + std::to_string(q.terrainSegs) +
          " foliage=" + std::to_string(foliageCpu_.size()));
}

void Application::fixedUpdate(float dt) {
  double mx, my;
  glfwGetCursorPos(window_, &mx, &my);
  // Simple mouse look when right button held
  static double lastX = mx, lastY = my;
  if (glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
    float dx = static_cast<float>(mx - lastX) * 0.004f;
    ctx_.sprint.yaw += dx;
  }
  lastX = mx;
  lastY = my;

  auto view = registry_.view<Velocity, Transform, PlayerTag>();
  for (auto e : view) {
    auto& vel = view.get<Velocity>(e);
    auto& tr = view.get<Transform>(e);
    glm::vec3 wish{0.f};
    if (glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS) wish.z += 1.f;
    if (glfwGetKey(window_, GLFW_KEY_S) == GLFW_PRESS) wish.z -= 1.f;
    if (glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS) wish.x -= 1.f;
    if (glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS) wish.x += 1.f;
    ctx_.sprint.sprinting = glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                            glfwGetKey(window_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    if (glm::length(wish) > 1e-4f) wish = glm::normalize(wish);
    const float yaw = ctx_.sprint.yaw;
    const float s = std::sin(yaw), c = std::cos(yaw);
    // wish.z forward, wish.x right in local — map to world XZ
    glm::vec3 worldWish{wish.x * c + wish.z * s, 0.f, -wish.x * s + wish.z * c};
    const float acc = ctx_.sprint.sprinting ? 48.f : 22.f;
    vel.linear.x += worldWish.x * acc * dt;
    vel.linear.z += worldWish.z * acc * dt;
    const float fr = ctx_.sprint.sprinting ? 1.2f : 8.f;
    vel.linear.x *= std::max(0.f, 1.f - fr * dt);
    vel.linear.z *= std::max(0.f, 1.f - fr * dt);
    const float maxSp = (ctx_.sprint.sprinting ? 32.f : 12.f) * (1.f + ctx_.sprint.momentum * 0.8f);
    const float sp = std::sqrt(vel.linear.x * vel.linear.x + vel.linear.z * vel.linear.z);
    if (sp > maxSp) {
      vel.linear.x *= maxSp / sp;
      vel.linear.z *= maxSp / sp;
    }
    tr.position.x += vel.linear.x * dt;
    tr.position.z += vel.linear.z * dt;
    tr.position.y = ctx_.height.sample(tr.position.x, tr.position.z, ctx_.sprint.score) + 1.1f;
    vel.linear.y = 0.f;

    ctx_.sprint.position = tr.position;
    ctx_.sprint.velocity = vel.linear;
  }

  runSimulationSystems(registry_, ctx_, dt);
}

void Application::frameUpdate(float dt) {
  materials_.pollHotReload();

  // Keep ground under Bolt / camera — foliage already streams, terrain must too
  rebuildTerrainAroundPlayer(false);

  // Stream more foliage ahead while sprinting
  if (ctx_.sprint.sprinting && foliageCpu_.size() < 4000) {
    auto fresh = ctx_.vegetation.generate(ctx_.sprint, ctx_.rules, ctx_.height, ctx_.budgets,
                                          ctx_.budgets.vegSpawnsPerTick * 4);
    for (auto& inst : fresh) {
      FoliageInstanceGPU g;
      g.posScale = glm::vec4(inst.position, inst.scale);
      g.yawKind = glm::vec4(inst.yaw, static_cast<float>(inst.kind), 0.f, 0.f);
      foliageCpu_.push_back(g);
    }
    if (!fresh.empty()) {
      // Cap total
      if (foliageCpu_.size() > 3500) {
        foliageCpu_.erase(foliageCpu_.begin(),
                          foliageCpu_.begin() + static_cast<long>(foliageCpu_.size() - 3500));
      }
      vulkan_.uploadFoliage(foliageCpu_);
    }
  }

  runSpawnSystems(registry_, ctx_, dt);
  ctx_.elapsed = time_.elapsed;
  (void)dt;
}

void Application::render() {
  // Camera behind Bolt
  const float yaw = ctx_.sprint.yaw;
  const glm::vec3 eye = ctx_.sprint.position +
                        glm::vec3(-std::sin(yaw) * 12.f, 6.f, -std::cos(yaw) * 12.f);
  const glm::vec3 target = ctx_.sprint.position + glm::vec3(0.f, 1.f, 0.f);
  const float aspect = height_ > 0 ? width_ / static_cast<float>(height_) : 16.f / 9.f;
  glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0, 1, 0));
  // Far plane past terrain half-size so horizon/fog owns the fade, not a hard clip
  glm::mat4 proj = glm::perspective(glm::radians(60.f), aspect, 0.2f, 520.f);
  proj[1][1] *= -1.f; // Vulkan Y flip

  const glm::mat4 viewProj = proj * view;
  FrameUBO ubo{};
  ubo.viewProj = viewProj;
  ubo.invViewProj = glm::inverse(viewProj);
  ubo.cameraPos_time = glm::vec4(eye, static_cast<float>(time_.elapsed));
  // y = material bitflags from loaded biome pack
  ubo.sprintScore_flags = glm::vec4(ctx_.sprint.score, vulkan_.materialFlags(), 0.f, 0.f);
  // x=tiling y=pathHalfWidth z=pathEdge w=meanderAmp
  ubo.tiling_pad = glm::vec4(0.032f, 5.5f, 3.2f, 4.0f);

  vulkan_.drawFrame(ubo, static_cast<uint32_t>(foliageCpu_.size()));
}

void Application::run() {
  double last = glfwGetTime();
  while (running_ && window_ && !glfwWindowShouldClose(window_)) {
    glfwPollEvents();
    if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;

    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(window_, &fbW, &fbH);
    if (fbW > 0 && fbH > 0) {
      width_ = fbW;
      height_ = fbH;
    }

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
  if (vulkan_.isValid()) {
    // wait handled inside shutdown
  }
  vulkan_.shutdown();
  if (window_) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
  }
  glfwTerminate();
  logInfo("Application shutdown");
}

} // namespace bolt
