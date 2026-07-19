#include "bolt/app/Application.hpp"
#include "bolt/ecs/Components.hpp"
#include "bolt/world/TerrainMesh.hpp"
#include "bolt/pcg/StalkMesh.hpp"
#include "bolt/pcg/MeshPrimitives.hpp"
#include "bolt/pcg/BoltGsd.hpp"
#include "bolt/pcg/PropMeshes.hpp"
#include <array>
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

bool Application::init(bool startFullscreen) {
  if (!glfwInit()) {
    logError("GLFW init failed");
    return false;
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
  glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
  // Prefer sRGB-capable / high-DPI framebuffer sizes where supported
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

  window_ = glfwCreateWindow(windowedW_, windowedH_, "Bolt Engine — Crystal Nebula Plains", nullptr, nullptr);
  if (!window_) {
    logError("GLFW window failed");
    glfwTerminate();
    return false;
  }

  // Center windowed mode on primary monitor
  if (GLFWmonitor* prim = glfwGetPrimaryMonitor()) {
    if (const GLFWvidmode* vm = glfwGetVideoMode(prim)) {
      int mx = 0, my = 0;
      glfwGetMonitorPos(prim, &mx, &my);
      windowedX_ = mx + (vm->width - windowedW_) / 2;
      windowedY_ = my + (vm->height - windowedH_) / 2;
      glfwSetWindowPos(window_, windowedX_, windowedY_);
    }
  }

  glfwSetWindowUserPointer(window_, this);
  glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* w, int width, int height) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
    if (app) app->onResize(width, height);
  });
  glfwSetKeyCallback(window_, [](GLFWwindow* w, int key, int /*scancode*/, int action, int mods) {
    if (action != GLFW_PRESS) return;
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
    if (!app) return;
    // F11 or Alt+Enter — game-standard fullscreen toggle
    if (key == GLFW_KEY_F11) {
      app->toggleFullscreen();
      return;
    }
    if (key == GLFW_KEY_ENTER && (mods & GLFW_MOD_ALT)) {
      app->toggleFullscreen();
      return;
    }
    // Escape: leave fullscreen first; second press quits
    if (key == GLFW_KEY_ESCAPE) {
      if (app->isFullscreen()) {
        app->setFullscreen(false);
      } else {
        glfwSetWindowShouldClose(w, GLFW_TRUE);
      }
    }
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
  // TerrainFeatureGenerator stamps + look-ahead drama on height sampling
  ctx_.height.setFeatures(&ctx_.terrainFeatures);
  ctx_.terrainFeatures.updateLookAhead(ctx_.sprint);

  createCrystalScene();
  // Default ¾ orbit so Bolt reads well on first frame
  camOrbitYaw_ = 0.65f;
  camOrbitPitch_ = 0.28f;
  camDist_ = boltFullMesh_ ? 7.5f : 9.f;
  running_ = true;

  if (startFullscreen) {
    setFullscreen(true);
  }

  logInfo("Application ready — WASD move, Shift sprint, Space jump, RMB orbit, scroll zoom");
  logInfo("Fullscreen: F11 or Alt+Enter  |  Esc: leave fullscreen / quit");
  return true;
}

void Application::onResize(int w, int h) {
  if (w > 0 && h > 0) {
    width_ = w;
    height_ = h;
    vulkan_.resize(w, h);
  }
}

GLFWmonitor* Application::monitorForWindow() const {
  if (!window_) return glfwGetPrimaryMonitor();

  // Prefer monitor already owning an exclusive fullscreen window
  if (GLFWmonitor* cur = glfwGetWindowMonitor(window_)) return cur;

  int wx = 0, wy = 0, ww = 0, wh = 0;
  glfwGetWindowPos(window_, &wx, &wy);
  glfwGetWindowSize(window_, &ww, &wh);
  const int cx = wx + ww / 2;
  const int cy = wy + wh / 2;

  int count = 0;
  GLFWmonitor** mons = glfwGetMonitors(&count);
  for (int i = 0; i < count; ++i) {
    int mx = 0, my = 0;
    glfwGetMonitorPos(mons[i], &mx, &my);
    const GLFWvidmode* vm = glfwGetVideoMode(mons[i]);
    if (!vm) continue;
    if (cx >= mx && cy >= my && cx < mx + vm->width && cy < my + vm->height) {
      return mons[i];
    }
  }
  return glfwGetPrimaryMonitor();
}

void Application::toggleFullscreen() { setFullscreen(!fullscreen_); }

void Application::setFullscreen(bool enable) {
  if (!window_ || enable == fullscreen_) return;

  GLFWmonitor* mon = monitorForWindow();
  if (!mon) mon = glfwGetPrimaryMonitor();
  if (!mon) {
    logError("No monitor available for fullscreen");
    return;
  }
  const GLFWvidmode* mode = glfwGetVideoMode(mon);
  if (!mode) {
    logError("No video mode for fullscreen");
    return;
  }

  if (enable) {
    // Remember windowed placement before exclusive mode
    if (!fullscreen_) {
      glfwGetWindowPos(window_, &windowedX_, &windowedY_);
      glfwGetWindowSize(window_, &windowedW_, &windowedH_);
      if (windowedW_ < 320) windowedW_ = 1280;
      if (windowedH_ < 240) windowedH_ = 720;
    }
    // Match desktop mode first so the OS compositor does less mode-switch thrash
    glfwSetWindowMonitor(window_, mon, 0, 0, mode->width, mode->height, mode->refreshRate);
    fullscreen_ = true;
    logInfo("Fullscreen " + std::to_string(mode->width) + "x" + std::to_string(mode->height) +
            " @" + std::to_string(mode->refreshRate) + "Hz");
  } else {
    // Restore previous windowed rect (clamped to a sensible minimum)
    const int w = std::max(windowedW_, 640);
    const int h = std::max(windowedH_, 360);
    glfwSetWindowMonitor(window_, nullptr, windowedX_, windowedY_, w, h, 0);
    fullscreen_ = false;
    logInfo("Windowed " + std::to_string(w) + "x" + std::to_string(h));
  }
  // Swapchain rebuild: framebuffer size callback + Vulkan OUT_OF_DATE path
}

void Application::rebuildTerrainAroundPlayer(bool force) {
  float ox = 0.f, oz = 0.f;
  streamer_.terrainOrigin(ctx_.sprint.position.x, ctx_.sprint.position.z, ox, oz);
  const float half = terrainSize_ * 0.5f;
  const float margin = terrainSize_ * 0.28f;
  const float dx = std::abs(ctx_.sprint.position.x - terrainOriginX_);
  const float dz = std::abs(ctx_.sprint.position.z - terrainOriginZ_);
  const float pdx = ctx_.sprint.position.x - lastTerrainPlayerPos_.x;
  const float pdz = ctx_.sprint.position.z - lastTerrainPlayerPos_.z;
  const float move2 = pdx * pdx + pdz * pdz;
  const float scoreDelta = std::abs(ctx_.sprint.score - lastTerrainScoreBake_);
  // Rebuild when: leave terrain patch, moved ~22m (look-ahead drama), or sprint score jumps
  constexpr float kLookRefresh = 22.f;
  const bool originOk = dx < half - margin && dz < half - margin &&
                        std::abs(ox - terrainOriginX_) < 1.f && std::abs(oz - terrainOriginZ_) < 1.f;
  if (!force && originOk && move2 < kLookRefresh * kLookRefresh && scoreDelta < 0.18f) return;

  terrainOriginX_ = ox;
  terrainOriginZ_ = oz;
  lastTerrainPlayerPos_ = ctx_.sprint.position;
  lastTerrainScoreBake_ = ctx_.sprint.score;
  ctx_.terrainFeatures.updateLookAhead(ctx_.sprint);
  auto cpu = buildTerrainMesh(ctx_.height, terrainSegs_, terrainSize_, terrainOriginX_,
                              terrainOriginZ_, ctx_.sprint.score);
  vulkan_.uploadTerrain(cpu.vertices, cpu.indices);
  logInfo("Terrain features+origin (" + std::to_string(static_cast<int>(ox)) + ", " +
          std::to_string(static_cast<int>(oz)) + ") score=" +
          std::to_string(ctx_.sprint.score).substr(0, 4) +
          " chunks=" + std::to_string(streamer_.loadedChunkCount()));
}

void Application::rebuildPathRibbon() {
  const float len = 55.f + ctx_.sprint.score * 48.f;
  const int segs = 30 + static_cast<int>(ctx_.sprint.score * 12.f);

  // Steer main path toward nearest ruin (guidance fantasy)
  glm::vec3 goal{};
  const glm::vec3* goalPtr = nullptr;
  {
    std::vector<RuinInstance> ruins;
    streamer_.gatherRuins(ruins);
    float best = 1e12f;
    for (const auto& r : ruins) {
      const float dx = r.position.x - ctx_.sprint.position.x;
      const float dz = r.position.z - ctx_.sprint.position.z;
      const float d2 = dx * dx + dz * dz;
      // Prefer ruins ahead in cone
      const float fx = std::sin(ctx_.sprint.yaw), fz = std::cos(ctx_.sprint.yaw);
      const float along = dx * fx + dz * fz;
      if (along < 15.f || d2 < 25.f * 25.f) continue;
      if (d2 < best && d2 < 160.f * 160.f) {
        best = d2;
        goal = r.position;
        goalPtr = &goal;
      }
    }
  }

  lastPathNetwork_ = ctx_.paths.generateNetwork(ctx_.sprint, ctx_.height, len, segs, goalPtr);
  ctx_.rules.setPathNetwork(lastPathNetwork_);

  const float hw = ctx_.paths.ribbonHalfWidth(ctx_.sprint.score);
  auto ribbon = buildPathNetworkMesh(lastPathNetwork_, hw, ctx_.height, ctx_.sprint.score);
  vulkan_.uploadPathMesh(ribbon.vertices, ribbon.indices);

  // Path fireflies along principal path only
  const float score = ctx_.sprint.score;
  if (score > 0.25f && particles_.size() < 480 && !lastPathNetwork_.paths.empty()) {
    const auto& pl = lastPathNetwork_.paths[0];
    if (pl.points.size() >= 3) {
      const int n = 2 + static_cast<int>(score * 6.f);
      for (int k = 0; k < n && particles_.size() < 512; ++k) {
        const float t = (static_cast<float>(k) + 0.5f) / static_cast<float>(n);
        const size_t i = static_cast<size_t>(
            std::clamp(static_cast<int>(t * static_cast<float>(pl.points.size() - 1)), 0,
                       static_cast<int>(pl.points.size()) - 1));
        const glm::vec3& p = pl.points[i].position;
        CpuParticle c;
        c.pos = p + glm::vec3(0.f, 0.35f, 0.f);
        auto h01 = [](float v) {
          float n = std::sin(v * 12.9898f) * 43758.5453f;
          return n - std::floor(n);
        };
        c.vel = glm::vec3((h01(p.x) - 0.5f) * 0.8f, 0.6f + score * 0.5f,
                          (h01(p.z + 1.7f) - 0.5f) * 0.8f);
        c.life = 0.8f + score * 0.6f;
        c.maxLife = c.life;
        c.size = 0.12f + score * 0.08f;
        c.color = glm::vec3(0.45f, 0.95f, 1.f);
        c.kind = 2; // crystal spark
        particles_.push_back(c);
      }
    }
  }
}

void Application::packAndUploadInstances() {
  std::vector<FoliageInstance> foliage, details, flyingRaw;
  std::vector<RuinInstance> ruins;
  streamer_.gatherFoliage(foliage);
  streamer_.gatherDetails(details);
  streamer_.gatherFlying(flyingRaw);
  streamer_.gatherRuins(ruins);

  // FlyingGenerator: animate bob/spin + LOD keep nearest
  std::vector<FoliageInstance> flying;
  FlyingGenerator::animateForRender(flyingRaw, flying, static_cast<float>(ctx_.elapsed),
                                    ctx_.sprint.score);
  FlyingGenerator::lodCull(flying, ctx_.sprint.position,
                           static_cast<size_t>(900 + ctx_.sprint.score * 600.f));

  // Moving clear bubble: dense forest on flanks, open ground under/around Bolt.
  // Filter at pack time with *current* position (chunk bake only cleared at load).
  glm::vec3 eye = ctx_.sprint.position;
  for (auto e : registry_.view<Transform, PlayerTag>()) {
    eye = registry_.get<Transform>(e).position;
    break;
  }
  lastPackPlayerPos_ = eye;
  const auto& clr = ctx_.rules.clear();
  const float rSmall2 = clr.packClearSmall * clr.packClearSmall;
  const float rBush2 = clr.packClearBush * clr.packClearBush;
  const float rTree2 = clr.packClearTree * clr.packClearTree;
  // Soft elliptical bubble: slightly wider on sides so flanks fill earlier than forward? No —
  // circular bubble keeps flanks dense beyond radius.
  auto inClearBubble = [&](const glm::vec3& p, float r2) {
    const float dx = p.x - eye.x;
    const float dz = p.z - eye.z;
    return dx * dx + dz * dz < r2;
  };
  auto onPath = [&](const glm::vec3& p) {
    return ctx_.rules.nearEnergyPath(p, 2.0f);
  };

  std::vector<FoliageInstanceGPU> stalk, bush, det, rui;
  std::array<std::vector<FoliageInstanceGPU>, VulkanContext::kTreeTypes> trees{};
  auto push = [](std::vector<FoliageInstanceGPU>& dst, const glm::vec3& p, float sc, float yaw,
                 float kind) {
    FoliageInstanceGPU g;
    g.posScale = glm::vec4(p, sc);
    // Deterministic per-instance color seed + morph (breaks clone spam)
    auto fracHash = [](float x, float z, float a, float b) {
      float n = std::sin(x * a + z * b) * 43758.5453f;
      return n - std::floor(n);
    };
    const float h = fracHash(p.x, p.z, 12.9898f, 78.233f);
    const float h2 = fracHash(p.x, p.z, 39.346f, 11.135f);
    g.yawKind = glm::vec4(yaw, kind, h, h2);
    if (kind > 3.5f && kind < 4.5f) g.posScale.w *= 0.82f + h2 * 0.4f;
    dst.push_back(g);
  };

  for (const auto& f : foliage) {
    // Kind-scaled keep-out: tall trees need more room so they don't loom on Bolt
    float r2 = rSmall2;
    if (f.kind == 4u)
      r2 = rTree2;
    else if (f.kind == 3u)
      r2 = rBush2;
    if (inClearBubble(f.position, r2)) continue;
    if (onPath(f.position)) continue; // PathGenerator exclusion zone

    if (f.kind == 3u)
      push(bush, f.position, f.scale, f.yaw, static_cast<float>(f.kind));
    else if (f.kind == 4u) {
      const int tv =
          static_cast<int>(std::min(f.treeVariant, static_cast<std::uint32_t>(VulkanContext::kTreeTypes - 1)));
      push(trees[static_cast<size_t>(tv)], f.position, f.scale * 1.2f, f.yaw,
           static_cast<float>(f.kind));
    } else if (f.kind == 5u)
      push(stalk, f.position, f.scale * 0.85f, f.yaw, static_cast<float>(f.kind));
    else if (f.kind == 2u)
      push(stalk, f.position, f.scale * 1.05f, f.yaw, static_cast<float>(f.kind));
    else
      push(stalk, f.position, f.scale, f.yaw, static_cast<float>(f.kind));
  }
  for (const auto& d : details) {
    // Floaters can sit closer (airborne); ground details respect bubble + path
    const bool isFloat = d.kind == DetailKind::Float;
    const bool isVent = d.kind == DetailKind::Vent;
    if (!isFloat && inClearBubble(d.position, rSmall2)) continue;
    if (!isFloat && onPath(d.position)) continue;
    float sc = d.scale;
    if (isVent) sc *= 0.85f;
    if (d.kind == DetailKind::Cluster) sc *= 1.15f;
    push(det, d.position, sc, d.yaw, static_cast<float>(d.kind));
  }
  // FlyingGenerator air layer (shards / debris / sky motes) — no path clear
  for (const auto& f : flying) {
    float sc = f.scale;
    if (f.kind == FlyingKind::Debris) sc *= 1.2f;
    if (f.kind == FlyingKind::SkyMote) sc *= 1.35f;
    push(det, f.position, sc, f.yaw, static_cast<float>(f.kind));
  }

  // Crystal Nebula ruins: 0 monolith 1 arch 2 observatory 3 temple (GPU kinds 10–13)
  // Extra scale mul so monuments dominate the horizon (not vegetation-sized)
  std::vector<FoliageInstanceGPU> ruiArch, ruiObs, ruiTemple;
  for (const auto& r : ruins) {
    const float gk = 10.f + static_cast<float>(r.kind);
    const float hero = r.scale * 1.15f; // pack boost on already-hero generators
    if (r.kind == 1u)
      push(ruiArch, r.position, hero, r.yaw, gk);
    else if (r.kind == 2u)
      push(ruiObs, r.position, hero, r.yaw, gk);
    else if (r.kind == 3u)
      push(ruiTemple, r.position, hero, r.yaw, gk);
    else
      push(rui, r.position, hero, r.yaw, gk);
  }

  // Soft caps for GPU — trees share a global budget across 10 species
  auto cap = [](std::vector<FoliageInstanceGPU>& v, size_t n) {
    if (v.size() > n) v.resize(n);
  };
  cap(stalk, 4200);
  cap(bush, 2800);
  cap(det, 3600); // details + flying share batch
  {
    size_t treeTotal = 0;
    for (auto& tr : trees) treeTotal += tr.size();
    const size_t treeBudget = 2800;
    if (treeTotal > treeBudget) {
      // Proportional thin so all species remain
      const float keep = static_cast<float>(treeBudget) / static_cast<float>(treeTotal);
      for (auto& tr : trees) {
        const size_t n = std::max<size_t>(1, static_cast<size_t>(tr.size() * keep));
        if (tr.size() > n) tr.resize(n);
      }
    }
  }
  cap(rui, 24);
  cap(ruiArch, 16);
  cap(ruiObs, 16);
  cap(ruiTemple, 16);

  packedInstances_.clear();
  instCounts_ = {};
  instCounts_.stalkFirst = 0;
  instCounts_.stalkCount = static_cast<uint32_t>(stalk.size());
  packedInstances_.insert(packedInstances_.end(), stalk.begin(), stalk.end());

  instCounts_.bushFirst = static_cast<uint32_t>(packedInstances_.size());
  instCounts_.bushCount = static_cast<uint32_t>(bush.size());
  packedInstances_.insert(packedInstances_.end(), bush.begin(), bush.end());

  instCounts_.tallFirst = static_cast<uint32_t>(packedInstances_.size());
  instCounts_.tallCount = 0;
  for (int t = 0; t < VulkanContext::kTreeTypes; ++t) {
    instCounts_.treeFirst[t] = static_cast<uint32_t>(packedInstances_.size());
    instCounts_.treeCount[t] = static_cast<uint32_t>(trees[static_cast<size_t>(t)].size());
    packedInstances_.insert(packedInstances_.end(), trees[static_cast<size_t>(t)].begin(),
                            trees[static_cast<size_t>(t)].end());
    instCounts_.tallCount += instCounts_.treeCount[t];
  }

  instCounts_.detailFirst = static_cast<uint32_t>(packedInstances_.size());
  instCounts_.detailCount = static_cast<uint32_t>(det.size());
  packedInstances_.insert(packedInstances_.end(), det.begin(), det.end());

  instCounts_.ruinFirst = static_cast<uint32_t>(packedInstances_.size());
  instCounts_.ruinCount = static_cast<uint32_t>(rui.size());
  packedInstances_.insert(packedInstances_.end(), rui.begin(), rui.end());

  instCounts_.ruinArchFirst = static_cast<uint32_t>(packedInstances_.size());
  instCounts_.ruinArchCount = static_cast<uint32_t>(ruiArch.size());
  packedInstances_.insert(packedInstances_.end(), ruiArch.begin(), ruiArch.end());

  instCounts_.ruinObsFirst = static_cast<uint32_t>(packedInstances_.size());
  instCounts_.ruinObsCount = static_cast<uint32_t>(ruiObs.size());
  packedInstances_.insert(packedInstances_.end(), ruiObs.begin(), ruiObs.end());

  instCounts_.ruinTempleFirst = static_cast<uint32_t>(packedInstances_.size());
  instCounts_.ruinTempleCount = static_cast<uint32_t>(ruiTemple.size());
  packedInstances_.insert(packedInstances_.end(), ruiTemple.begin(), ruiTemple.end());

  vulkan_.uploadFoliage(packedInstances_);

  // —— Crystal multi-lights (nearest N to player) — path toward deferred ——
  struct Cand {
    CrystalLightGPU L;
    float dist2 = 0.f;
  };
  std::vector<Cand> cands;
  cands.reserve(512);
  auto pushLight = [&](const glm::vec3& p, float range, const glm::vec3& col, float intensity) {
    Cand c;
    c.L.posRange = glm::vec4(p, range);
    c.L.colorIntensity = glm::vec4(col, intensity);
    const glm::vec3 d = p - eye;
    c.dist2 = glm::dot(d, d);
    cands.push_back(c);
  };

  // Crystal trees — lights tinted slightly per species range (all still kind 4)
  for (int ti = 0; ti < VulkanContext::kTreeTypes; ++ti) {
    static const glm::vec3 kTreeLight[10] = {
        {0.55f, 0.35f, 0.95f}, {0.45f, 0.4f, 1.0f},  {0.7f, 0.45f, 0.95f}, {0.65f, 0.3f, 1.0f},
        {0.5f, 0.55f, 1.0f},  {0.75f, 0.5f, 0.9f},  {0.4f, 0.35f, 0.9f},  {0.6f, 0.7f, 1.0f},
        {0.85f, 0.55f, 0.75f},{0.55f, 0.45f, 1.05f}};
    for (const auto& t : trees[static_cast<size_t>(ti)]) {
      const glm::vec3 p = glm::vec3(t.posScale) + glm::vec3(0.f, t.posScale.w * 3.0f, 0.f);
      pushLight(p, 13.f + t.posScale.w * 3.2f, kTreeLight[ti], 1.25f);
    }
  }
  // Bright crystal clusters / floating shards in stalk pack
  for (const auto& s : stalk) {
    const float kind = s.yawKind.y;
    if (kind > 1.5f && kind < 2.5f) {
      const glm::vec3 p = glm::vec3(s.posScale) + glm::vec3(0.f, s.posScale.w * 1.4f, 0.f);
      pushLight(p, 8.f + s.posScale.w * 2.f, glm::vec3(0.35f, 0.85f, 1.0f), 0.9f);
    } else if (kind > 4.5f) {
      const glm::vec3 p = glm::vec3(s.posScale);
      pushLight(p, 10.f + s.posScale.w * 2.5f, glm::vec3(0.65f, 0.4f, 1.0f), 1.1f);
    }
  }
  // Detail + Flying lights
  size_t detStep = std::max<size_t>(1, det.size() / 110);
  for (size_t i = 0; i < det.size(); i += detStep) {
    const auto& d = det[i];
    const float k = d.yawKind.y;
    glm::vec3 p = glm::vec3(d.posScale);
    glm::vec3 col(0.4f, 0.85f, 1.0f);
    float inten = 0.35f;
    float range = 4.5f + d.posScale.w * 1.5f;
    if (k > 27.5f) { // sky mote
      col = glm::vec3(0.55f, 0.4f, 0.95f);
      inten = 0.55f + ctx_.sprint.score * 0.35f;
      range = 12.f;
    } else if (k > 26.5f) { // flying debris
      col = glm::vec3(0.7f, 0.55f, 0.95f);
      inten = 0.5f;
      range = 7.f;
    } else if (k > 24.5f) { // flying shards 25–26
      col = glm::vec3(0.45f, 0.85f, 1.1f);
      inten = 0.45f + ctx_.sprint.score * 0.25f;
      range = 6.f;
    } else if (k > 21.5f && k < 22.5f) { // vent
      p.y += 0.35f;
      col = glm::vec3(0.3f, 0.95f, 1.1f);
      inten = 0.75f + ctx_.sprint.score * 0.55f;
      range = 7.f + d.posScale.w * 2.f;
    } else if (k > 20.5f && k < 21.5f) { // cluster
      p.y += d.posScale.w * 0.5f;
      col = glm::vec3(0.4f, 0.9f, 1.15f);
      inten = 0.65f + ctx_.sprint.score * 0.4f;
      range = 6.5f;
    } else if (k > 22.5f && k < 23.5f) { // detail float
      col = glm::vec3(0.55f, 0.75f, 1.1f);
      inten = 0.5f;
      range = 5.5f;
    } else if (k > 23.5f && k < 24.5f) { // rune
      col = glm::vec3(0.9f, 0.7f, 0.35f);
      inten = 0.4f + ctx_.sprint.score * 0.3f;
      range = 5.f;
    } else {
      p.y += 0.4f;
    }
    pushLight(p, range, col, inten);
  }
  // Ruins — strong hero lights (gold / cyan / magenta)
  auto ruinLight = [&](const std::vector<FoliageInstanceGPU>& rr, const glm::vec3& col, float hMul,
                       float intensity) {
    for (const auto& r : rr) {
      const glm::vec3 p = glm::vec3(r.posScale) + glm::vec3(0.f, r.posScale.w * hMul, 0.f);
      pushLight(p, 28.f + r.posScale.w * 2.5f, col, intensity);
    }
  };
  ruinLight(rui, glm::vec3(1.0f, 0.78f, 0.32f), 4.5f, 2.4f);       // monolith gold
  ruinLight(ruiArch, glm::vec3(1.0f, 0.55f, 0.35f), 5.5f, 2.2f);   // arch warm
  ruinLight(ruiObs, glm::vec3(0.45f, 0.9f, 1.0f), 6.0f, 2.6f);     // observatory cyan
  ruinLight(ruiTemple, glm::vec3(1.0f, 0.72f, 0.28f), 3.5f, 2.8f); // temple gold

  // Bolt aura point light — soft local fill, not a cyan flood that ghosts the coat
  pushLight(eye + glm::vec3(0.f, 0.85f, 0.f), 7.f, glm::vec3(0.3f, 0.75f, 0.95f), 0.55f);

  std::sort(cands.begin(), cands.end(),
            [](const Cand& a, const Cand& b) { return a.dist2 < b.dist2; });
  const size_t nKeep = std::min(cands.size(), static_cast<size_t>(64));
  std::vector<CrystalLightGPU> lights;
  lights.reserve(nKeep);
  for (size_t i = 0; i < nKeep; ++i) lights.push_back(cands[i].L);
  vulkan_.uploadCrystalLights(lights);
  logInfo("Crystal lights: " + std::to_string(lights.size()) + " nearest of " +
          std::to_string(cands.size()) + " candidates (SSBO binding 18)");
}

void Application::refreshWorldStreaming() {
  streamer_.chunkSize = 72.f;
  streamer_.loadRadius = 2;
  const bool dirty =
      streamer_.update(ctx_.sprint, ctx_.rules, ctx_.height, ctx_.budgets, ctx_.vegetation,
                       detailSpawner_, flyingGenerator_, ruinGenerator_);
  // Re-pack when chunks change OR Bolt moved enough that the clear bubble must shift
  // (keeps flanks dense; open ground follows the player without full-world regen)
  const glm::vec3 p = ctx_.sprint.position;
  const float dx = p.x - lastPackPlayerPos_.x;
  const float dz = p.z - lastPackPlayerPos_.z;
  const float move2 = dx * dx + dz * dz;
  constexpr float kRepackMove = 5.f; // meters — bubble slides with Bolt
  if (dirty || packedInstances_.empty() || move2 > kRepackMove * kRepackMove)
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
  // Higher segs so craters/ridges/rock shelves stay sharp
  terrainSegs_ = std::max(q.terrainSegs, 96);
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
    // Pick tall mesh variant by hash of time — re-upload one blended shape set.
    // Runtime variety is morph/color; mesh B/C used as primary for silhouette refresh.
    for (int t = 0; t < kCrystalTreeTypes; ++t) {
      buildCrystalTree(t, v, i);
      vulkan_.uploadTreeMesh(t, v, i);
    }
    logInfo("Crystal Nebula: uploaded 10 tree species meshes");
    buildDetailShardMesh(v, i);
    vulkan_.uploadDetailMesh(v, i);
    buildRuinPillarMesh(v, i);
    vulkan_.uploadRuinMesh(v, i);
    buildRuinArchMesh(v, i);
    vulkan_.uploadRuinArchMesh(v, i);
    buildRuinObservatoryMesh(v, i);
    vulkan_.uploadRuinObsMesh(v, i);
    buildRuinTempleMesh(v, i);
    vulkan_.uploadRuinTempleMesh(v, i);
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
    // HQ pack materials first, then legacy engine path
    const char* furCandidates[] = {
        "assets/materials/bolt/bolt_fur",
        "assets/characters/bolt/pack_hq/materials/bolt_fur",
        "assets/characters/bolt/pack/materials/bolt_fur",
    };
    bool ok = false;
    for (const char* path : furCandidates) {
      if (vulkan_.loadBoltFurPBR(path)) {
        logInfo(std::string("Bolt HQ fur PBR loaded: ") + path);
        ok = true;
        break;
      }
    }
    if (!ok) logWarn("Bolt fur PBR missing — procedural white coat only");
  }

  // Seed world streamer + path ribbon + instances
  ctx_.sprint.position = glm::vec3(0.f, 2.f, 0.f);
  lastTrailPos_ = ctx_.sprint.position;
  ctx_.sprint.yaw = 0.f;
  ctx_.sprint.score = 0.55f;
  ctx_.terrainFeatures.updateLookAhead(ctx_.sprint);
  ctx_.height.setFeatures(&ctx_.terrainFeatures);
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
          " ruins=" +
          std::to_string(instCounts_.ruinCount + instCounts_.ruinArchCount +
                         instCounts_.ruinObsCount + instCounts_.ruinTempleCount) +
          " (M" + std::to_string(instCounts_.ruinCount) + " A" +
          std::to_string(instCounts_.ruinArchCount) + " O" +
          std::to_string(instCounts_.ruinObsCount) + " T" +
          std::to_string(instCounts_.ruinTempleCount) + ")");
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

    // Snappy target-velocity — sprint ~300 m/s base (hold Shift)
    const float maxSp =
        (ctx_.sprint.sprinting ? 300.f : 42.f) * (1.f + ctx_.sprint.momentum * 0.45f);
    const float approach = ctx_.sprint.sprinting ? 18.f : 16.f; // 1/s toward target
    const glm::vec3 target = worldWish * maxSp;
    const float k = std::min(1.f, approach * dt);
    vel.linear.x += (target.x - vel.linear.x) * k;
    vel.linear.z += (target.z - vel.linear.z) * k;
    // Coast decay only when no input (keeps sprint momentum readable)
    if (glm::length(wish) < 1e-4f) {
      const float fr = ctx_.sprint.sprinting ? 1.2f : 9.f;
      const float damp = std::max(0.f, 1.f - fr * dt);
      vel.linear.x *= damp;
      vel.linear.z *= damp;
    }
    tr.position.x += vel.linear.x * dt;
    tr.position.z += vel.linear.z * dt;
    // Eye/body height ~1.1 above feet; mesh feet sit on ground in render()
    tr.position.y = ctx_.height.sample(tr.position.x, tr.position.z, ctx_.sprint.score) + 1.1f;
    vel.linear.y = 0.f;

    // Face movement — slower at sprint so body trails the turn (not a tank turret)
    const float spH = std::sqrt(vel.linear.x * vel.linear.x + vel.linear.z * vel.linear.z);
    if (spH > 0.8f) {
      const float faceYaw = std::atan2(vel.linear.x, vel.linear.z);
      float dy = faceYaw - ctx_.sprint.yaw;
      while (dy > 3.14159f) dy -= 6.28318f;
      while (dy < -3.14159f) dy += 6.28318f;
      // High speed: gradual arc; walk/strafe: snappier
      const float turnFollow = ctx_.sprint.sprinting ? (3.2f + spH * 0.01f) : 11.f;
      ctx_.sprint.yaw += dy * std::min(1.f, turnFollow * dt);
    }
    // Smoothed yaw rate for bank / lean animation
    {
      float dyaw = ctx_.sprint.yaw - prevSprintYaw_;
      while (dyaw > 3.14159f) dyaw -= 6.28318f;
      while (dyaw < -3.14159f) dyaw += 6.28318f;
      const float inst = (dt > 1e-5f) ? (dyaw / dt) : 0.f;
      turnRate_ += (inst - turnRate_) * std::min(1.f, 8.f * dt);
      prevSprintYaw_ = ctx_.sprint.yaw;
    }

    // Path 2: Space jump (edge-triggered)
    const bool jumpDown = glfwGetKey(window_, GLFW_KEY_SPACE) == GLFW_PRESS;
    if (jumpDown && !jumpKeyWasDown_ && jumpT_ < 0.f) {
      jumpT_ = 0.f;
      // small upward boost on launch phase (horizontal impulse from facing)
      const float jYaw = ctx_.sprint.yaw;
      vel.linear.x += std::sin(jYaw) * 8.f;
      vel.linear.z += std::cos(jYaw) * 8.f;
    }
    jumpKeyWasDown_ = jumpDown;
    if (jumpT_ >= 0.f) {
      const float prev = jumpT_;
      jumpT_ += dt / std::max(0.2f, jumpDuration_);
      if (prev < 0.55f && jumpT_ >= 0.55f) landBurstPending_ = true;
      if (jumpT_ >= 1.f) jumpT_ = -1.f;
    }

    ctx_.sprint.position = tr.position;
    ctx_.sprint.velocity = vel.linear;
  }

  runSimulationSystems(registry_, ctx_, dt);
}

void Application::updateParticles(float dt) {
  // Age / move — pawprints hug ground; sparks drift
  for (auto& p : particles_) {
    p.life -= dt;
    p.pos += p.vel * dt;
    if (p.kind == 1) {
      p.vel *= (1.f - 3.5f * dt); // footprints almost stick
      p.size *= (1.f - 0.15f * dt);
    } else if (p.kind == 2) {
      p.vel.y += 1.6f * dt;
      p.vel *= (1.f - 1.2f * dt);
      p.size *= (1.f - 0.55f * dt);
    } else if (p.kind == 3) {
      p.vel *= (1.f - 1.0f * dt);
      p.size *= (1.f - 0.4f * dt);
    } else {
      p.vel.y += 0.8f * dt;
      p.vel *= (1.f - 1.8f * dt);
      p.size *= (1.f - 0.35f * dt);
    }
  }
  particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                  [](const CpuParticle& p) { return p.life <= 0.f; }),
                   particles_.end());

  const float speed = ctx_.sprint.speed;
  const bool emit = (ctx_.sprint.sprinting && speed > 8.f) || jumpT_ >= 0.f;
  const glm::vec3 pos = ctx_.sprint.position;
  const float yaw = ctx_.sprint.yaw;
  const glm::vec3 back{-std::sin(yaw), 0.f, -std::cos(yaw)};
  const glm::vec3 right{std::cos(yaw), 0.f, -std::sin(yaw)};
  const float groundY = ctx_.height.sample(pos.x, pos.z, ctx_.sprint.score);

  auto spawnOne = [&](glm::vec3 at, float size, float life, glm::vec3 col, glm::vec3 kick, int kind) {
    if (particles_.size() >= 512) return;
    CpuParticle p;
    p.pos = at;
    p.vel = kick;
    p.life = life;
    p.maxLife = life;
    p.size = size;
    p.color = col;
    p.kind = kind;
    particles_.push_back(p);
  };

  // Path 5: crystal dust under feet (pack crystal_dust aesthetic)
  if (emit && speed > 6.f) {
    emitAccum_ += dt * (10.f + speed * 0.65f);
    while (emitAccum_ >= 1.f && particles_.size() < 512) {
      emitAccum_ -= 1.f;
      const float rx = (static_cast<float>(std::rand() % 200) / 100.f - 1.f) * 0.4f;
      const float rz = (static_cast<float>(std::rand() % 200) / 100.f - 1.f) * 0.4f;
      const int kind = (std::rand() % 3 == 0) ? 2 : 0; // mix soft dust + crystal sparks
      spawnOne(glm::vec3(pos.x + rx, groundY + 0.1f, pos.z + rz),
               kind == 2 ? (0.18f + speed * 0.004f) : (0.28f + speed * 0.01f),
               kind == 2 ? 0.45f : (0.35f + 0.25f * ctx_.sprint.momentum),
               kind == 2 ? glm::vec3(0.75f, 0.55f, 1.f) : glm::vec3(0.45f, 0.8f, 0.95f),
               back * (2.f + speed * 0.08f) + right * rx * 2.f + glm::vec3(0.f, 1.4f, 0.f), kind);
    }
  } else {
    emitAccum_ = 0.f;
  }

  // Path 5: glowing Star Moss pawprint trails while moving fast
  const float moved = glm::length(glm::vec2(pos.x - lastTrailPos_.x, pos.z - lastTrailPos_.z));
  trailDistAccum_ += moved;
  lastTrailPos_ = pos;
  if (speed > 14.f && trailDistAccum_ > 0.7f) {
    trailDistAccum_ = 0.f;
    const float side = (std::rand() % 2 == 0) ? 1.f : -1.f;
    spawnOne(glm::vec3(pos.x, groundY + 0.06f, pos.z) + back * 0.35f + right * side * 0.18f,
             0.55f + ctx_.sprint.momentum * 0.25f, 0.85f,
             glm::vec3(0.25f, 0.95f, 1.f), glm::vec3(0.f, 0.02f, 0.f), 1);
  }

  // Path 5: aura sparks when sprinting or mid-jump (pack aura ring energy)
  const float energyGate = ctx_.sprint.score + (jumpT_ >= 0.f ? 0.5f : 0.f);
  if (energyGate > 0.35f && (ctx_.sprint.sprinting || jumpT_ >= 0.f)) {
    auraEmitAccum_ += dt * (6.f + energyGate * 10.f + speed * 0.05f);
    while (auraEmitAccum_ >= 1.f && particles_.size() < 512) {
      auraEmitAccum_ -= 1.f;
      const float ang = static_cast<float>(std::rand() % 628) / 100.f;
      const float rad = 0.35f + static_cast<float>(std::rand() % 100) / 100.f * 0.55f;
      const float h = 0.4f + static_cast<float>(std::rand() % 100) / 100.f * 1.1f;
      const glm::vec3 offset(std::cos(ang) * rad, h, std::sin(ang) * rad);
      spawnOne(pos + offset, 0.22f + energyGate * 0.12f, 0.28f + energyGate * 0.15f,
               glm::vec3(0.7f, 0.45f, 1.f),
               glm::vec3(offset.x, 0.4f, offset.z) * 0.8f + back * 0.5f, 3);
    }
  } else {
    auraEmitAccum_ = 0.f;
  }

  // Landing crystal burst (once when jump enters land phase)
  if (landBurstPending_) {
    landBurstPending_ = false;
    for (int i = 0; i < 12 && static_cast<int>(particles_.size()) < 512; ++i) {
      const float ang = static_cast<float>(i) / 12.f * 6.28318f;
      spawnOne(glm::vec3(pos.x, groundY + 0.15f, pos.z), 0.22f, 0.55f,
               glm::vec3(0.6f, 0.9f, 1.f),
               glm::vec3(std::cos(ang) * 3.5f, 2.2f, std::sin(ang) * 3.5f), 2);
    }
  }

  // —— DetailGenerator: vents / clusters / runes (ground layer FX) ——
  {
    const float score = ctx_.sprint.score;
    detailFxAccum_ += dt * (4.f + score * 10.f + (ctx_.sprint.sprinting ? speed * 0.08f : 0.f));
    std::vector<FoliageInstance> dets;
    streamer_.gatherDetails(dets);

    while (detailFxAccum_ >= 1.f && particles_.size() < 500) {
      detailFxAccum_ -= 1.f;
      if (dets.empty()) break;
      for (int attempt = 0; attempt < 6 && particles_.size() < 500; ++attempt) {
        const size_t idx = static_cast<size_t>(std::rand()) % dets.size();
        const auto& d = dets[idx];
        const float dx = d.position.x - pos.x;
        const float dz = d.position.z - pos.z;
        const float d2 = dx * dx + dz * dz;
        if (d2 > 38.f * 38.f) continue;

        if (d.kind == DetailKind::Vent) {
          const float h = 0.2f + score * 0.35f;
          const float up = 1.4f + score * 2.2f + (ctx_.sprint.sprinting ? 1.2f : 0.f);
          spawnOne(d.position + glm::vec3(0.f, 0.15f, 0.f), 0.1f + score * 0.08f,
                   0.5f + score * 0.35f, glm::vec3(0.3f, 0.95f, 1.05f),
                   glm::vec3((static_cast<float>(std::rand() % 100) / 100.f - 0.5f) * 0.4f, up,
                             (static_cast<float>(std::rand() % 100) / 100.f - 0.5f) * 0.4f),
                   2);
          if (score > 0.55f && particles_.size() < 500)
            spawnOne(d.position + glm::vec3(0.f, h, 0.f), 0.08f, 0.4f,
                     glm::vec3(0.45f, 0.85f, 1.f), glm::vec3(0.f, up * 0.7f, 0.f), 0);
        } else if (d.kind == DetailKind::Cluster) {
          spawnOne(d.position + glm::vec3(0.f, d.scale * 0.4f, 0.f), 0.09f, 0.35f,
                   glm::vec3(0.45f, 0.9f, 1.1f), glm::vec3(0.f, 0.8f, 0.f), 2);
          if (ctx_.sprint.sprinting && d2 < 14.f * 14.f && speed > 10.f) {
            for (int s = 0; s < 3 && particles_.size() < 512; ++s) {
              const float ang = static_cast<float>(std::rand() % 628) / 100.f;
              spawnOne(d.position + glm::vec3(0.f, 0.3f, 0.f), 0.12f, 0.45f,
                       glm::vec3(0.55f, 0.85f, 1.15f),
                       glm::vec3(std::cos(ang) * 2.5f, 1.5f + score, std::sin(ang) * 2.5f), 2);
            }
          }
        } else if (d.kind == DetailKind::Rune && score > 0.4f) {
          spawnOne(d.position + glm::vec3(0.f, 0.25f, 0.f), 0.1f, 0.5f,
                   glm::vec3(0.95f, 0.75f, 0.4f), glm::vec3(0.f, 0.6f, 0.f), 3);
        }
      }
    }
  }

  // —— FlyingGenerator atmosphere: spores, flyer motes, ruin wisps, high-sprint swarm ——
  {
    std::vector<FoliageInstance> flyingRaw;
    std::vector<RuinInstance> ruins;
    streamer_.gatherFlying(flyingRaw);
    streamer_.gatherRuins(ruins);
    std::vector<FlyingParticleSpawn> fx;
    flyingGenerator_.harvestParticles(fx, dt, ctx_.sprint, pos, groundY, flyingRaw, ruins);
    for (const auto& s : fx) {
      if (particles_.size() >= 512) break;
      spawnOne(s.pos, s.size, s.life, s.color, s.vel, s.particleKind);
    }
  }

  particleGpu_.clear();
  particleGpu_.reserve(particles_.size());
  for (const auto& p : particles_) {
    ParticleGPU g;
    const float t = p.maxLife > 1e-4f ? std::clamp(p.life / p.maxLife, 0.f, 1.f) : 0.f;
    g.posSize = glm::vec4(p.pos, p.size);
    g.colorLife = glm::vec4(p.color, t);
    g.params = glm::vec4(static_cast<float>(p.kind), 0.f, 0.f, 0.f);
    particleGpu_.push_back(g);
  }
  vulkan_.uploadParticles(particleGpu_);
}

void Application::frameUpdate(float dt) {
  materials_.pollHotReload();

  // Sprint look-ahead: terrain features grow more dramatic ahead of Bolt
  ctx_.terrainFeatures.updateLookAhead(ctx_.sprint);
  ctx_.height.setFeatures(&ctx_.terrainFeatures);

  // Chunk streaming + terrain patch + path ribbon
  rebuildTerrainAroundPlayer(false);
  refreshWorldStreaming();

  pathRebuildAccum_ += dt;
  // Rebuild path on a timer (was nearly every frame while sprinting → GPU stall)
  const float pathPeriod = ctx_.sprint.sprinting ? 0.45f : 0.7f;
  if (pathRebuildAccum_ > pathPeriod) {
    rebuildPathRibbon();
    pathRebuildAccum_ = 0.f;
  }

  updateParticles(dt);

  // SkyGenerator — sprint-reactive nebula / god rays / streams
  skyGenerator_.update(dt, ctx_.sprint);

  runSpawnSystems(registry_, ctx_, dt);
  ctx_.elapsed = time_.elapsed;
}

void Application::render() {
  // Free orbit camera around Bolt (RMB steers camOrbitYaw_/Pitch_, scroll via +/-)
  const float groundY =
      ctx_.height.sample(ctx_.sprint.position.x, ctx_.sprint.position.z, ctx_.sprint.score);
  const float camJump = jumpT_ >= 0.f ? boltJumpHeightOffset(jumpT_) : 0.f;
  const glm::vec3 focus =
      glm::vec3(ctx_.sprint.position.x, groundY + 0.95f + camJump, ctx_.sprint.position.z);

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

  // Halton(2,3) TAA subpixel jitter in pixel units → NDC
  static const float kHalton2[] = {0.5f, 0.25f, 0.75f, 0.125f, 0.625f, 0.375f, 0.875f, 0.0625f};
  static const float kHalton3[] = {1.f / 3.f, 2.f / 3.f, 1.f / 9.f, 4.f / 9.f,
                                   7.f / 9.f, 2.f / 9.f, 5.f / 9.f, 8.f / 9.f};
  const uint32_t ji = taaFrame_ & 7u;
  const float jx = (kHalton2[ji] * 2.f - 1.f) / static_cast<float>(std::max(width_, 1));
  const float jy = (kHalton3[ji] * 2.f - 1.f) / static_cast<float>(std::max(height_, 1));
  // Jitter projection (clip-space offset)
  proj[2][0] += jx;
  proj[2][1] += jy;

  const glm::mat4 viewProjJittered = proj * view;
  // Unjittered for velocity reprojection
  glm::mat4 projClean = glm::perspective(glm::radians(55.f), aspect, 0.12f, 520.f);
  projClean[1][1] *= -1.f;
  const glm::mat4 viewProjClean = projClean * view;
  const glm::mat4 invClean = glm::inverse(viewProjClean);

  FrameUBO ubo{};
  ubo.viewProj = viewProjJittered;
  ubo.invViewProj = invClean;
  ubo.prevViewProj = prevViewProjValid_ ? prevViewProj_ : viewProjClean;
  ubo.taaJitter = glm::vec4(jx, jy, prevJitter_.x, prevJitter_.y);
  ubo.cameraPos_time = glm::vec4(eye, static_cast<float>(time_.elapsed));
  const float camSpeed = prevViewProjValid_
                             ? glm::length(eye - prevEye_) /
                                   std::max(time_.realDt, 1e-4f)
                             : ctx_.sprint.speed;
  // w = SkyGenerator energy pack (nebula + streams + aurora) for sky.frag
  ubo.sprintScore_flags =
      glm::vec4(ctx_.sprint.score, vulkan_.materialFlags(), camSpeed, skyGenerator_.skyEnergyPack());
  const float pathHw = ctx_.paths.ribbonHalfWidth(ctx_.sprint.score);
  // Higher tiling = finer ground grit (was 0.008 → flat purple slab)
  ubo.tiling_pad = glm::vec4(0.014f, pathHw, 4.0f, 4.5f);

  // Sun orthographic shadow volume centered on Bolt (tighter = sharper local shadows)
  {
    const glm::vec3 sunDir = glm::normalize(glm::vec3(0.35f, 0.88f, 0.35f));
    const glm::vec3 center = glm::vec3(ctx_.sprint.position.x, ctx_.sprint.position.y, ctx_.sprint.position.z);
    const float ext = 58.f; // slightly tighter frustum → more texels on near ground
    const glm::mat4 lightView =
        glm::lookAt(center + sunDir * 85.f, center, glm::vec3(0.f, 1.f, 0.f));
    glm::mat4 lightProj = glm::ortho(-ext, ext, -ext, ext, 4.f, 180.f);
    lightProj[1][1] *= -1.f; // Vulkan Y
    ubo.lightViewProj = lightProj * lightView;
    // x=bias, y=strength (punch), z=enabled, w=1/mapSize
    ubo.shadowParams = glm::vec4(0.0018f, 0.96f, 1.f, 1.f / 1024.f);
  }

  // Path 2: idle / run / jump state from speed + jump timer
  const float speedF = std::clamp(ctx_.sprint.speed / 280.f, 0.f, 1.5f);
  BoltMotion motion = BoltMotion::Idle;
  if (jumpT_ >= 0.f)
    motion = BoltMotion::Jump;
  else if (ctx_.sprint.speed > 2.5f)
    motion = BoltMotion::Run;

  // Phase rate: slow breathe idle, faster run (gait cycles), mid jump stretch
  float phaseRate = 1.0f;
  if (motion == BoltMotion::Run)
    phaseRate = 1.8f + speedF * 5.0f; // snappier gallop so body rock reads
  else if (motion == BoltMotion::Jump)
    phaseRate = 2.4f;
  animPhase_ = std::fmod(animPhase_ + time_.realDt * phaseRate, 1.f);
  if (animPhase_ < 0.f) animPhase_ = 0.f;

  // Lower energy → less ghost shell / body emit; still ramps with sprint
  const float energy = std::clamp(0.06f + ctx_.sprint.score * 0.28f + ctx_.sprint.momentum * 0.22f +
                                      (jumpT_ >= 0.f ? 0.15f : 0.f),
                                  0.f, 0.85f);
  const float jumpH = jumpT_ >= 0.f ? boltJumpHeightOffset(jumpT_) : 0.f;

  // Dog faces sprint.yaw (movement); camera orbits independently
  const float yaw = ctx_.sprint.yaw;
  // Lateral weave + turn bank offset (not locked to a laser-line path)
  const float lat =
      boltLateralSway(animPhase_, speedF, turnRate_, motion);
  const float rightX = std::cos(yaw);
  const float rightZ = -std::sin(yaw);

  glm::mat4 root(1.f);
  root = glm::translate(
      root, glm::vec3(ctx_.sprint.position.x + rightX * lat, groundY + jumpH,
                      ctx_.sprint.position.z + rightZ * lat));
  root = glm::rotate(root, yaw, glm::vec3(0.f, 1.f, 0.f));
  root = glm::scale(root, glm::vec3(boltFullMesh_ ? 1.55f : 1.65f));

  std::array<glm::mat4, static_cast<int>(BoltPart::Count)> local{};
  boltAnimTransforms(animPhase_, speedF, energy, motion, jumpT_ >= 0.f ? jumpT_ : 0.f, turnRate_,
                     local);

  // hop: whole-body athleticism (head/spine VS + leg matrices)
  float hop = 0.4f;
  if (motion == BoltMotion::Idle)
    hop = 0.25f;
  else if (motion == BoltMotion::Jump)
    hop = 0.5f;
  else if (speedF > 0.08f)
    hop = std::clamp(0.8f + speedF * 0.55f, 0.8f, 1.4f);

  // VS limb deform only if still a rigid full mesh (no separate leg draws)
  const float fullMeshDeform = boltFullMesh_ ? 1.f : 0.f;

  std::array<ObjectPush, VulkanContext::kBoltPartCount> boltDraw{};
  for (int i = 0; i < VulkanContext::kBoltPartCount; ++i) {
    glm::mat4 partLocal = local[static_cast<size_t>(i)];
    // Aura follows body stretch
    if (i == static_cast<int>(BoltPart::Aura)) {
      partLocal = local[static_cast<int>(BoltPart::Body)];
      // Tighter shell so aura hugs silhouette, not a big ghost bubble
      partLocal = glm::scale(partLocal, glm::vec3(1.05f + (jumpT_ >= 0.f ? 0.03f : 0.f)));
    }
    boltDraw[static_cast<size_t>(i)].model = root * partLocal;
    float e = energy;
    if (i == static_cast<int>(BoltPart::Aura)) e *= (jumpT_ >= 0.f ? 0.55f : 0.38f);
    boltDraw[static_cast<size_t>(i)].color = glm::vec4(1.f, 1.f, 1.f, e);
    // Multi-part legs use matrices (deform=0). Rigid full mesh uses VS deform.
    const float deform =
        (fullMeshDeform > 0.5f && i == static_cast<int>(BoltPart::Body)) ? 1.f : 0.f;
    boltDraw[static_cast<size_t>(i)].anim = glm::vec4(animPhase_, speedF, hop, deform);
  }

  // Project SkyGenerator key light to screen for post god rays
  {
    const auto& sky = skyGenerator_.state();
    const glm::vec3 sunDir = glm::normalize(sky.sunDir);
    const glm::vec3 sunWorld = eye + sunDir * 400.f;
    glm::vec4 clip = viewProjClean * glm::vec4(sunWorld, 1.f);
    glm::vec2 sunUv(0.5f, 0.28f);
    if (std::abs(clip.w) > 1e-4f) {
      glm::vec3 ndc = glm::vec3(clip) / clip.w;
      sunUv = glm::vec2(ndc.x * 0.5f + 0.5f, ndc.y * 0.5f + 0.5f);
    }
    vulkan_.setSunScreen(sunUv, sky.godRayStrength, sky.gradeStrength);
  }
  vulkan_.setPostParams(ctx_.sprint.score, time_.elapsed, camSpeed);
  vulkan_.setTemporalMatrices(invClean, ubo.prevViewProj, ubo.taaJitter);
  vulkan_.drawFrame(ubo, instCounts_, boltDraw.data(), VulkanContext::kBoltPartCount,
                    static_cast<uint32_t>(particleGpu_.size()));

  // History for next frame
  prevViewProj_ = viewProjClean;
  prevJitter_ = glm::vec2(jx, jy);
  prevEye_ = eye;
  prevViewProjValid_ = true;
  ++taaFrame_;
}

void Application::run() {
  double last = glfwGetTime();
  while (running_ && window_ && !glfwWindowShouldClose(window_)) {
    glfwPollEvents();
    // Escape handled in key callback: leave fullscreen first, then quit

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
