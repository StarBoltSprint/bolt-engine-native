#include "bolt/app/Application.hpp"
#include "bolt/ecs/Components.hpp"
#include "bolt/world/TerrainMesh.hpp"
#include "bolt/pcg/StalkMesh.hpp"
#include "bolt/pcg/MeshPrimitives.hpp"
#include "bolt/pcg/BoltGsd.hpp"
#include "bolt/core/Log.hpp"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
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

  // Soft shadow blob + multi-part GSD (body/legs/tail/aura) + fur PBR
  {
    std::vector<VertexPC> bv;
    std::vector<uint32_t> bi;
    buildShadowBlobMesh(bv, bi);
    vulkan_.uploadBlobMesh(bv, bi);
  }
  {
    BoltCharacterMeshes ch;
    // Prefer free/imported low-poly mesh: bolt_gsd.glb / .obj (see ATTRIBUTION.txt)
    buildOrLoadBoltCharacter(ch, "assets/characters/bolt/bolt_gsd.glb");
    if (!ch.fullMesh) {
      saveBoltCharacterObj(ch, "assets/characters/bolt/bolt_gsd_generated.obj");
    }
    for (int i = 0; i < static_cast<int>(BoltPart::Count); ++i) {
      auto& p = ch.parts[static_cast<size_t>(i)];
      if (!p.vertices.empty()) vulkan_.uploadBoltPart(i, p.vertices, p.indices);
    }
    boltFullMesh_ = ch.fullMesh;
    if (ch.fullMesh)
      logInfo("Bolt imported low-poly mesh + aura (fullMesh mode)");
    else
      logInfo("Bolt multi-part procedural fallback");
  }
  {
    const bool ok = vulkan_.loadBoltFurPBR("assets/materials/bolt/bolt_fur");
    if (ok) logInfo("Bolt fur PBR on UVs (Imagine → Vulkan)");
    else logWarn("Bolt fur PBR missing under assets/materials/bolt/");
  }

  // Initial foliage batch around spawn
  ctx_.sprint.position = glm::vec3(0.f, 2.f, 0.f);
  lastTrailPos_ = ctx_.sprint.position;
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
    // Eye/body height ~1.1 above feet; mesh feet sit on ground in render()
    tr.position.y = ctx_.height.sample(tr.position.x, tr.position.z, ctx_.sprint.score) + 1.1f;
    vel.linear.y = 0.f;

    ctx_.sprint.position = tr.position;
    ctx_.sprint.velocity = vel.linear;
  }

  runSimulationSystems(registry_, ctx_, dt);
}

void Application::updateParticles(float dt) {
  // Age / move
  for (auto& p : particles_) {
    p.life -= dt;
    p.pos += p.vel * dt;
    p.vel.y += 0.8f * dt; // slight lift then settle
    p.vel *= (1.f - 1.8f * dt);
    p.size *= (1.f - 0.35f * dt);
  }
  particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                  [](const CpuParticle& p) { return p.life <= 0.f; }),
                   particles_.end());

  const float speed = ctx_.sprint.speed;
  const bool emit = ctx_.sprint.sprinting && speed > 4.f;
  const glm::vec3 pos = ctx_.sprint.position;
  const float yaw = ctx_.sprint.yaw;
  const glm::vec3 back{-std::sin(yaw), 0.f, -std::cos(yaw)};
  const glm::vec3 right{std::cos(yaw), 0.f, -std::sin(yaw)};

  auto spawnOne = [&](glm::vec3 at, float size, float life, glm::vec3 col, glm::vec3 kick) {
    if (particles_.size() >= 512) return;
    CpuParticle p;
    p.pos = at;
    p.vel = kick;
    p.life = life;
    p.maxLife = life;
    p.size = size;
    p.color = col;
    particles_.push_back(p);
  };

  // Sprint dust under feet
  if (emit) {
    emitAccum_ += dt * (8.f + speed * 0.55f);
    while (emitAccum_ >= 1.f && particles_.size() < 512) {
      emitAccum_ -= 1.f;
      const float rx = (static_cast<float>(std::rand() % 200) / 100.f - 1.f) * 0.35f;
      const float rz = (static_cast<float>(std::rand() % 200) / 100.f - 1.f) * 0.35f;
      const float groundY = ctx_.height.sample(pos.x, pos.z, ctx_.sprint.score) + 0.12f;
      spawnOne(glm::vec3(pos.x + rx, groundY, pos.z + rz), 0.25f + speed * 0.01f,
               0.35f + 0.25f * ctx_.sprint.momentum,
               glm::vec3(0.45f, 0.75f, 0.9f),
               back * (2.f + speed * 0.08f) + right * rx * 2.f + glm::vec3(0.f, 1.2f, 0.f));
    }
  } else {
    emitAccum_ = 0.f;
  }

  // Trail breadcrumbs while moving fast
  const float moved = glm::length(glm::vec2(pos.x - lastTrailPos_.x, pos.z - lastTrailPos_.z));
  trailDistAccum_ += moved;
  lastTrailPos_ = pos;
  if (speed > 6.f && trailDistAccum_ > 0.55f) {
    trailDistAccum_ = 0.f;
    const float groundY = ctx_.height.sample(pos.x, pos.z, ctx_.sprint.score) + 0.2f;
    const float side = (std::rand() % 2 == 0) ? 1.f : -1.f;
    spawnOne(pos + back * 0.4f + right * side * 0.15f + glm::vec3(0.f, groundY - pos.y + 0.15f, 0.f),
             0.18f + ctx_.sprint.momentum * 0.2f, 0.55f,
             glm::vec3(0.7f, 0.95f, 1.f), back * 1.5f + glm::vec3(0.f, 0.6f, 0.f));
  }

  // Upload GPU
  particleGpu_.clear();
  particleGpu_.reserve(particles_.size());
  for (const auto& p : particles_) {
    ParticleGPU g;
    const float t = p.maxLife > 1e-4f ? std::clamp(p.life / p.maxLife, 0.f, 1.f) : 0.f;
    g.posSize = glm::vec4(p.pos, p.size);
    g.colorLife = glm::vec4(p.color, t);
    particleGpu_.push_back(g);
  }
  vulkan_.uploadParticles(particleGpu_);
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

  updateParticles(dt);

  runSpawnSystems(registry_, ctx_, dt);
  ctx_.elapsed = time_.elapsed;
}

void Application::render() {
  // ¾ chase cam — side + back, lower height so full silhouette reads
  const float yaw = ctx_.sprint.yaw;
  const float sy = std::sin(yaw), cy = std::cos(yaw);
  // forward = (sy, 0, cy), right = (cy, 0, -sy)
  const float camBack = boltFullMesh_ ? 6.5f : 8.f;
  const float camSide = boltFullMesh_ ? 4.2f : 3.5f;
  const float camH = boltFullMesh_ ? 2.6f : 3.5f;
  const glm::vec3 forward(sy, 0.f, cy);
  const glm::vec3 right(cy, 0.f, -sy);
  const glm::vec3 eye = ctx_.sprint.position - forward * camBack + right * camSide +
                        glm::vec3(0.f, camH, 0.f);
  const glm::vec3 target = ctx_.sprint.position + forward * 1.8f + glm::vec3(0.f, 0.95f, 0.f);
  const float aspect = height_ > 0 ? width_ / static_cast<float>(height_) : 16.f / 9.f;
  glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0, 1, 0));
  glm::mat4 proj = glm::perspective(glm::radians(55.f), aspect, 0.15f, 520.f);
  proj[1][1] *= -1.f; // Vulkan Y flip

  const glm::mat4 viewProj = proj * view;
  FrameUBO ubo{};
  ubo.viewProj = viewProj;
  ubo.invViewProj = glm::inverse(viewProj);
  ubo.cameraPos_time = glm::vec4(eye, static_cast<float>(time_.elapsed));
  ubo.sprintScore_flags = glm::vec4(ctx_.sprint.score, vulkan_.materialFlags(), 0.f, 0.f);
  ubo.tiling_pad = glm::vec4(0.032f, 5.5f, 3.2f, 4.0f);

  // Root transform — imported mesh normalized ~2m; extra scale for screen presence
  const float groundY =
      ctx_.height.sample(ctx_.sprint.position.x, ctx_.sprint.position.z, ctx_.sprint.score);
  glm::mat4 root(1.f);
  root = glm::translate(root, glm::vec3(ctx_.sprint.position.x, groundY, ctx_.sprint.position.z));
  root = glm::rotate(root, yaw, glm::vec3(0.f, 1.f, 0.f));
  root = glm::scale(root, glm::vec3(boltFullMesh_ ? 1.85f : 1.65f));

  const float speedF = std::clamp(ctx_.sprint.speed / 28.f, 0.f, 1.4f);
  // Milder aura energy so mesh is the star
  const float energy = std::clamp(0.12f + ctx_.sprint.score * 0.4f + ctx_.sprint.momentum * 0.35f,
                                  0.f, 1.1f);
  const float phase = std::fmod(static_cast<float>(time_.elapsed) * (1.2f + speedF * 3.5f), 1.f);

  std::array<glm::mat4, static_cast<int>(BoltPart::Count)> local{};
  boltAnimTransforms(phase, speedF, energy, local);

  std::array<ObjectPush, VulkanContext::kBoltPartCount> boltDraw{};
  for (int i = 0; i < VulkanContext::kBoltPartCount; ++i) {
    glm::mat4 partLocal = local[static_cast<size_t>(i)];
    // Full imported mesh: only body + aura; shrink aura so it doesn't swamp
    if (boltFullMesh_ && i == static_cast<int>(BoltPart::Aura)) {
      partLocal = local[static_cast<int>(BoltPart::Body)];
      partLocal = glm::scale(partLocal, glm::vec3(1.12f));
    }
    boltDraw[static_cast<size_t>(i)].model = root * partLocal;
    float e = energy;
    if (i == static_cast<int>(BoltPart::Aura)) e *= 0.65f;
    boltDraw[static_cast<size_t>(i)].color = glm::vec4(1.f, 1.f, 1.f, e);
  }

  vulkan_.drawFrame(ubo, static_cast<uint32_t>(foliageCpu_.size()), boltDraw.data(),
                    VulkanContext::kBoltPartCount, static_cast<uint32_t>(particleGpu_.size()));
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
