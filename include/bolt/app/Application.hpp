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
#include "bolt/world/WorldStreamer.hpp"
#include "bolt/pcg/DetailSpawner.hpp"
#include "bolt/pcg/FlyingGenerator.hpp"
#include "bolt/pcg/SkyGenerator.hpp"
#include "bolt/pcg/RuinGenerator.hpp"
#include "bolt/pcg/PathGenerator.hpp"

struct GLFWwindow;
struct GLFWmonitor;

namespace bolt {

/** kind: 0 dust, 1 pawprint, 2 crystal, 3 aura, 4 near-cam dust, 5 cloud wisp */
struct CpuParticle {
  glm::vec3 pos{0.f};
  glm::vec3 vel{0.f};
  float life = 0.f;
  float maxLife = 1.f;
  float size = 0.3f;
  glm::vec3 color{0.5f, 0.8f, 1.f};
  int kind = 0;
};

class Application {
public:
  bool init(bool startFullscreen = false);
  void run();
  void shutdown();
  void onResize(int w, int h);
  void toggleFullscreen();
  void setFullscreen(bool enable);
  bool isFullscreen() const { return fullscreen_; }
  void adjustCamZoom(float delta) {
    camDist_ = std::clamp(camDist_ - delta * 0.85f, 3.5f, 22.f);
  }

private:
  GLFWmonitor* monitorForWindow() const;
  void createCrystalScene();
  void rebuildTerrainAroundPlayer(bool force);
  void refreshWorldStreaming();
  void rebuildPathRibbon();
  void packAndUploadInstances();
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

  WorldStreamer streamer_;
  DetailSpawner detailSpawner_;
  FlyingGenerator flyingGenerator_;
  SkyGenerator skyGenerator_;
  RuinGenerator ruinGenerator_;

  std::vector<FoliageInstanceGPU> packedInstances_;
  VulkanContext::SceneInstanceCounts instCounts_{};
  glm::vec3 lastPackPlayerPos_{0.f, 1e9f, 0.f}; // re-pack clear bubble as Bolt moves
  PathNetwork lastPathNetwork_{};
  std::vector<CpuParticle> particles_;
  std::vector<ParticleGPU> particleGpu_;
  float emitAccum_ = 0.f;
  float trailDistAccum_ = 0.f;
  float pathRebuildAccum_ = 0.f;
  float auraEmitAccum_ = 0.f;
  float detailFxAccum_ = 0.f;   // DetailGenerator vent/cluster/spore tick
  float wispAccum_ = 0.f;       // narrative wisps near ruins
  float atmosDustAccum_ = 0.f;  // near-camera dust motes
  float atmosCloudAccum_ = 0.f; // soft cloud sheets for depth/scale
  glm::vec3 lastTrailPos_{0.f};
  bool boltFullMesh_ = false;

  // Path 2 — motion state (idle / run / jump)
  float jumpT_ = -1.f;          // <0 = not jumping; 0..1 in jump
  float jumpDuration_ = 0.72f;  // seconds (pack jump cycle timing)
  float animPhase_ = 0.f;
  float prevSprintYaw_ = 0.f;
  float turnRate_ = 0.f; // rad/s, smoothed — banks body into turns
  bool jumpKeyWasDown_ = false;
  bool landBurstPending_ = false;
  bool running_ = false;
  int width_ = 1280;
  int height_ = 720;

  float camOrbitYaw_ = 0.55f;
  float camOrbitPitch_ = 0.32f;
  float camDist_ = 8.5f;
  double mouseLastX_ = 0.0;
  double mouseLastY_ = 0.0;
  bool mouseLookActive_ = false;

  // TAA / motion blur camera history
  glm::mat4 prevViewProj_{1.f};
  glm::vec2 prevJitter_{0.f};
  uint32_t taaFrame_ = 0;
  bool prevViewProjValid_ = false;
  glm::vec3 prevEye_{0.f};

  float terrainOriginX_ = 0.f;
  float terrainOriginZ_ = 0.f;
  float terrainSize_ = 432.f; // 6 chunks * 72m
  int terrainSegs_ = 72;
  float lastTerrainScoreBake_ = -1.f;
  glm::vec3 lastTerrainPlayerPos_{0.f, 1e9f, 0.f}; // refresh look-ahead drama
  bool fullscreen_ = false;
  int windowedX_ = 100;
  int windowedY_ = 100;
  int windowedW_ = 1280;
  int windowedH_ = 720;
};

} // namespace bolt
