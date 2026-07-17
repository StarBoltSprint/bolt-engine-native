#include "bolt/app/Application.hpp"
#include "bolt/ecs/Components.hpp"
#include "bolt/world/TerrainMesh.hpp"
#include "bolt/pcg/StalkMesh.hpp"
#include "bolt/pcg/MeshPrimitives.hpp"
#include "bolt/pcg/BoltGsd.hpp"
#include "bolt/pcg/PropMeshes.hpp"
#include "bolt/pcg/PathRibbon.hpp"
#include "bolt/pcg/StalkMesh.hpp"
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
  glfwSetScrollCallback(window_, [](GLFWwindow* w, double /*xoff*/, double yoff) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
    if (!app) return;
    app->adjustCamZoom(static_cast<float>(yoff));
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
  // Default ¾ orbit so Bolt reads well on first frame
  camOrbitYaw_ = 0.65f;
  camOrbitPitch_ = 0.28f;
  camDist_ = boltFullMesh_ ? 7.5f : 9.f;
  running_ = true;
  logInfo("Application ready — WASD move, Shift sprint, RMB orbit camera, scroll zoom, F11 fullscreen");
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
  float ox = 0.f, oz = 0.f;
  streamer_.terrainOrigin(ctx_.sprint.position.x, ctx_.sprint.position.z, ox, oz);
  const float half = terrainSize_ * 0.5f;
  const float margin = terrainSize_ * 0.28f;
  const float dx = std::abs(ctx_.sprint.position.x - terrainOriginX_);
  const float dz = std::abs(ctx_.sprint.position.z - terrainOriginZ_);
  // Snap to chunk grid; rebuild when player leaves margin
  if (!force && dx < half - margin && dz < half - margin &&
      std::abs(ox - terrainOriginX_) < 1.f && std::abs(oz - terrainOriginZ_) < 1.f)
    return;

  terrainOriginX_ = ox;
  terrainOriginZ_ = oz;
  auto cpu = buildTerrainMesh(ctx_.height, terrainSegs_, terrainSize_, terrainOriginX_,
                              terrainOriginZ_, ctx_.sprint.score);
  vulkan_.uploadTerrain(cpu.vertices, cpu.indices);
  logInfo("Terrain chunk-origin (" + std::to_string(static_cast<int>(ox)) + ", " +
          std::to_string(static_cast<int>(oz)) + ") chunks=" +
          std::to_string(streamer_.loadedChunkCount()));
}

void Application::rebuildPathRibbon() {
  const float len = 52.f + ctx_.sprint.score * 36.f;
  auto pts = ctx_.paths.generate(ctx_.sprint, ctx_.height, len, 28);
  const float hw = ctx_.paths.ribbonHalfWidth(ctx_.sprint.score);
  auto ribbon = buildPathRibbon(pts, hw, ctx_.height, ctx_.sprint.score);
  vulkan_.uploadPathMesh(ribbon.vertices, ribbon.indices);
}

void Application::packAndUploadInstances() {
  std::vector<FoliageInstance> foliage, details;
  std::vector<RuinInstance> ruins;
  streamer_.gatherFoliage(foliage);
  streamer_.gatherDetails(details);
  streamer_.gatherRuins(ruins);

  std::vector<FoliageInstanceGPU> stalk, bush, tall, det, rui;
  auto push = [](std::vector<FoliageInstanceGPU>& dst, const glm::vec3& p, float sc, float yaw,
                 float kind) {
    FoliageInstanceGPU g;
    g.posScale = glm::vec4(p, sc);
    g.yawKind = glm::vec4(yaw, kind, 0.f, 0.f);
    dst.push_back(g);
  };

  for (const auto& f : foliage) {
    if (f.kind == 3u)
      push(bush, f.position, f.scale, f.yaw, static_cast<float>(f.kind));
    else if (f.kind == 4u)
      push(tall, f.position, f.scale * 1.15f, f.yaw, static_cast<float>(f.kind));
    else
      push(stalk, f.position, f.scale, f.yaw, static_cast<float>(f.kind));
  }
  for (const auto& d : details)
    push(det, d.position, d.scale, d.yaw, static_cast<float>(d.kind));
  for (const auto& r : ruins)
    push(rui, r.position, r.scale, r.yaw, static_cast<float>(r.kind));

  // Soft caps for GPU
  auto cap = [](std::vector<FoliageInstanceGPU>& v, size_t n) {
    if (v.size() > n) v.resize(n);
  };
  cap(stalk, 2800);
  cap(bush, 800);
  cap(tall, 600);
  cap(det, 1500);
  cap(rui, 120);

  packedInstances_.clear();
  instCounts_ = {};
  instCounts_.stalkFirst = 0;
  instCounts_.stalkCount = static_cast<uint32_t>(stalk.size());
  packedInstances_.insert(packedInstances_.end(), stalk.begin(), stalk.end());

  instCounts_.bushFirst = static_cast<uint32_t>(packedInstances_.size());
  instCounts_.bushCount = static_cast<uint32_t>(bush.size());
  packedInstances_.insert(packedInstances_.end(), bush.begin(), bush.end());

  instCounts_.tallFirst = static_cast<uint32_t>(packedInstances_.size());
  instCounts_.tallCount = static_cast<uint32_t>(tall.size());
  packedInstances_.insert(packedInstances_.end(), tall.begin(), tall.end());

  instCounts_.detailFirst = static_cast<uint32_t>(packedInstances_.size());
  instCounts_.detailCount = static_cast<uint32_t>(det.size());
  packedInstances_.insert(packedInstances_.end(), det.begin(), det.end());

  instCounts_.ruinFirst = static_cast<uint32_t>(packedInstances_.size());
  instCounts_.ruinCount = static_cast<uint32_t>(rui.size());
  packedInstances_.insert(packedInstances_.end(), rui.begin(), rui.end());

  vulkan_.uploadFoliage(packedInstances_);
}

void Application::refreshWorldStreaming() {
  streamer_.chunkSize = 72.f;
  streamer_.loadRadius = 2;
  streamer_.update(ctx_.sprint, ctx_.rules, ctx_.height, ctx_.budgets, ctx_.vegetation,
                   detailSpawner_, ruinGenerator_);
  packAndUploadInstances();
}

void Application::createCrystalScene() {
  auto player = registry_.create();
  registry_.emplace<PlayerTag>(player);
  registry_.emplace<Transform>(player, Transform{glm::vec3(0.f, 2.f, 0.f)});
  registry_.emplace<Velocity>(player);
  registry_.emplace<BoltAura>(player);
  registry_.emplace<NameComponent>(player, "Bolt");

  // Chunk-streamed terrain (6×72m ≈ covers loadRadius 2 neighborhood)
  const auto q = qualitySettings(ctx_.quality);
  terrainSegs_ = std::max(q.terrainSegs, 64);
  terrainSize_ = streamer_.chunkSize * static_cast<float>(streamer_.loadRadius * 2 + 2);
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

  // PCG meshes: stalks, bushes, tall crystals, detail shards, ruin pillars
  {
    std::vector<VertexPC> v;
    std::vector<uint32_t> i;
    buildStalkMesh(v, i);
    vulkan_.uploadStalkMesh(v, i);
    buildBushMesh(v, i);
    vulkan_.uploadBushMesh(v, i);
    buildTallCrystalMesh(v, i);
    vulkan_.uploadTallMesh(v, i);
    buildDetailShardMesh(v, i);
    vulkan_.uploadDetailMesh(v, i);
    buildRuinPillarMesh(v, i);
    vulkan_.uploadRuinMesh(v, i);
  }

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

  // Seed world streamer + path ribbon + instances
  ctx_.sprint.position = glm::vec3(0.f, 2.f, 0.f);
  lastTrailPos_ = ctx_.sprint.position;
  ctx_.sprint.yaw = 0.f;
  ctx_.sprint.score = 0.55f;
  ctx_.sprint.sprinting = true;
  ctx_.budgets.applySprint(ctx_.sprint);

  refreshWorldStreaming();
  rebuildPathRibbon();

  auto batch = registry_.create();
  FoliageBatch fb;
  fb.instanceCount = instCounts_.stalkCount + instCounts_.bushCount + instCounts_.tallCount;
  registry_.emplace<FoliageBatch>(batch, fb);
  registry_.emplace<NameComponent>(batch, "CrystalFoliageBatch");

  logInfo("Crystal scene: chunks=" + std::to_string(streamer_.loadedChunkCount()) +
          " stalk=" + std::to_string(instCounts_.stalkCount) +
          " bush=" + std::to_string(instCounts_.bushCount) +
          " tall=" + std::to_string(instCounts_.tallCount) +
          " detail=" + std::to_string(instCounts_.detailCount) +
          " ruins=" + std::to_string(instCounts_.ruinCount));
}

void Application::fixedUpdate(float dt) {
  double mx, my;
  glfwGetCursorPos(window_, &mx, &my);

  // RMB free orbit (yaw + pitch) — was locked ¾ chase; now fully steerable
  const bool rmb = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
  if (rmb) {
    if (!mouseLookActive_) {
      mouseLookActive_ = true;
      mouseLastX_ = mx;
      mouseLastY_ = my;
    }
    const float dx = static_cast<float>(mx - mouseLastX_) * 0.0055f;
    const float dy = static_cast<float>(my - mouseLastY_) * 0.0055f;
    camOrbitYaw_ += dx;
    camOrbitPitch_ += dy;
    camOrbitPitch_ = std::clamp(camOrbitPitch_, -0.15f, 1.25f); // avoid flip under ground
    mouseLastX_ = mx;
    mouseLastY_ = my;
  } else {
    mouseLookActive_ = false;
    mouseLastX_ = mx;
    mouseLastY_ = my;
  }

  // Scroll zoom
  // (polled via glfwSetScrollCallback would be cleaner; use keys as reliable backup)
  if (glfwGetKey(window_, GLFW_KEY_EQUAL) == GLFW_PRESS ||
      glfwGetKey(window_, GLFW_KEY_KP_ADD) == GLFW_PRESS)
    camDist_ = std::max(3.5f, camDist_ - 8.f * dt);
  if (glfwGetKey(window_, GLFW_KEY_MINUS) == GLFW_PRESS ||
      glfwGetKey(window_, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS)
    camDist_ = std::min(22.f, camDist_ + 8.f * dt);

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
    // Move relative to camera flat yaw (W = into look direction on ground)
    const float fs = std::sin(camOrbitYaw_), fc = std::cos(camOrbitYaw_);
    glm::vec3 worldWish{wish.x * fc + wish.z * (-fs), 0.f, -wish.x * fs + wish.z * (-fc)};
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

    // Face movement direction (dog turns to run, camera orbits freely)
    const float spH = std::sqrt(vel.linear.x * vel.linear.x + vel.linear.z * vel.linear.z);
    if (spH > 0.8f) {
      const float faceYaw = std::atan2(vel.linear.x, vel.linear.z);
      float dy = faceYaw - ctx_.sprint.yaw;
      while (dy > 3.14159f) dy -= 6.28318f;
      while (dy < -3.14159f) dy += 6.28318f;
      ctx_.sprint.yaw += dy * std::min(1.f, 10.f * dt);
    }

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

  // Chunk streaming + terrain patch + path ribbon
  rebuildTerrainAroundPlayer(false);
  refreshWorldStreaming();

  pathRebuildAccum_ += dt;
  if (pathRebuildAccum_ > 0.35f || ctx_.sprint.sprinting) {
    if (pathRebuildAccum_ > 0.2f) {
      rebuildPathRibbon();
      pathRebuildAccum_ = 0.f;
    }
  }

  updateParticles(dt);

  runSpawnSystems(registry_, ctx_, dt);
  ctx_.elapsed = time_.elapsed;
}

void Application::render() {
  // Free orbit camera around Bolt (RMB steers camOrbitYaw_/Pitch_, scroll via +/-)
  const float groundY =
      ctx_.height.sample(ctx_.sprint.position.x, ctx_.sprint.position.z, ctx_.sprint.score);
  const glm::vec3 focus = glm::vec3(ctx_.sprint.position.x, groundY + 0.95f, ctx_.sprint.position.z);

  const float cp = std::cos(camOrbitPitch_);
  const float sp = std::sin(camOrbitPitch_);
  const float sy = std::sin(camOrbitYaw_);
  const float cy = std::cos(camOrbitYaw_);
  // Spherical offset: yaw around Y, pitch from horizontal
  const glm::vec3 offset(sy * cp * camDist_, sp * camDist_, cy * cp * camDist_);
  const glm::vec3 eye = focus + offset;
  const glm::vec3 target = focus;

  const float aspect = height_ > 0 ? width_ / static_cast<float>(height_) : 16.f / 9.f;
  glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0, 1, 0));
  glm::mat4 proj = glm::perspective(glm::radians(55.f), aspect, 0.12f, 520.f);
  proj[1][1] *= -1.f; // Vulkan Y flip

  const glm::mat4 viewProj = proj * view;
  FrameUBO ubo{};
  ubo.viewProj = viewProj;
  ubo.invViewProj = glm::inverse(viewProj);
  ubo.cameraPos_time = glm::vec4(eye, static_cast<float>(time_.elapsed));
  ubo.sprintScore_flags = glm::vec4(ctx_.sprint.score, vulkan_.materialFlags(), 0.f, 0.f);
  const float pathHw = ctx_.paths.ribbonHalfWidth(ctx_.sprint.score);
  ubo.tiling_pad = glm::vec4(0.032f, pathHw, 3.2f, 4.0f);

  // Dog faces sprint.yaw (movement); camera orbits independently
  const float yaw = ctx_.sprint.yaw;
  glm::mat4 root(1.f);
  root = glm::translate(root, glm::vec3(ctx_.sprint.position.x, groundY, ctx_.sprint.position.z));
  root = glm::rotate(root, yaw, glm::vec3(0.f, 1.f, 0.f));
  // Blender GSD is normalized ~2m; 1.55 keeps readable size without giant dog
  root = glm::scale(root, glm::vec3(boltFullMesh_ ? 1.55f : 1.65f));

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

  vulkan_.drawFrame(ubo, instCounts_, boltDraw.data(), VulkanContext::kBoltPartCount,
                    static_cast<uint32_t>(particleGpu_.size()));
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
