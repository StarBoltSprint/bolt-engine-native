#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "bolt/render/GpuMesh.hpp"
#include "bolt/render/GpuTexture.hpp"
#include "bolt/sprint/SprintCore.hpp"
#include "bolt/render/QualityTier.hpp"

struct GLFWwindow;

namespace bolt {

struct FrameUBO {
  glm::mat4 viewProj;
  glm::vec4 cameraPos_time;     // xyz cam, w time
  // x = sprint score
  // y = material bitflags: 1=ground 2=rock 4=path 8=stalk
  // z = camera speed (m/s) for motion blur
  // w = SkyGenerator energy pack (nebula/streams/aurora drive for sky.frag)
  glm::vec4 sprintScore_flags;
  // x = triplanar tiling, y = path half-width, z = path edge falloff, w = path meander amp
  glm::vec4 tiling_pad;
  glm::mat4 invViewProj;        // sky ray reconstruction
  glm::mat4 prevViewProj;       // previous unjittered VP (TAA / velocity)
  glm::vec4 taaJitter;          // xy current NDC jitter, zw previous
  glm::mat4 lightViewProj;      // sun orthographic VP for shadow map
  // x = depth bias, y = shadow strength 0-1, z = enabled, w = 1/shadowMapSize
  glm::vec4 shadowParams;
};

/** std140 post UBO — TAA + motion blur + bloom + god rays */
struct PostUBOData {
  glm::vec4 res_time;        // xy = 1/resolution, z = time, w = score
  glm::vec4 near_far_mb_taa; // x=near y=far z=motionBlurStr w=taaHistory
  glm::mat4 invViewProj;
  glm::mat4 prevViewProj;
  glm::vec4 jitter;          // xy curr, zw prev
  glm::vec4 sun_ray;         // xy sun UV, z godRay strength, w grade strength
};

/** Material bitflags for FrameUBO::sprintScore_flags.y */
enum MaterialFlags : int {
  kMatGround = 1,
  kMatRock = 2,
  kMatPath = 4,
  kMatStalk = 8,
};

/**
 * Vulkan device + swapchain + Crystal renderer.
 * Terrain: HeightField mesh + multi-material triplanar (ground/rock/path blend).
 * Foliage: instanced stalks with stalk material.
 */
class VulkanContext {
public:
  bool initialize(GLFWwindow* window);
  void shutdown();
  bool isValid() const { return valid_; }

  void beginFrame();
  void endFrame();
  void resize(int w, int h);

  bool uploadTerrain(const std::vector<VertexPC>& verts, const std::vector<uint32_t>& indices);
  bool uploadStalkMesh(const std::vector<VertexPC>& verts, const std::vector<uint32_t>& indices);
  bool uploadBlobMesh(const std::vector<VertexPC>& verts, const std::vector<uint32_t>& indices);
  bool uploadBoltMesh(const std::vector<VertexPC>& verts, const std::vector<uint32_t>& indices);
  /** Upload one GSD part (0..BoltPart::Count-1). */
  bool uploadBoltPart(int partIndex, const std::vector<VertexPC>& verts,
                      const std::vector<uint32_t>& indices);
  /** Packed instance buffer (stalk|bush|tall|detail|ruins) — one SSBO for all instanced draws. */
  bool uploadFoliage(const std::vector<FoliageInstanceGPU>& instances);
  bool uploadMesh(GpuMesh& mesh, const std::vector<VertexPC>& verts,
                  const std::vector<uint32_t>& indices);
  bool uploadBushMesh(const std::vector<VertexPC>& verts, const std::vector<uint32_t>& indices) {
    return uploadMesh(bush_, verts, indices);
  }
  bool uploadDetailMesh(const std::vector<VertexPC>& verts, const std::vector<uint32_t>& indices) {
    return uploadMesh(detail_, verts, indices);
  }
  bool uploadRuinMesh(const std::vector<VertexPC>& verts, const std::vector<uint32_t>& indices) {
    return uploadMesh(ruin_, verts, indices);
  }
  bool uploadRuinArchMesh(const std::vector<VertexPC>& verts, const std::vector<uint32_t>& indices) {
    return uploadMesh(ruinArch_, verts, indices);
  }
  bool uploadRuinObsMesh(const std::vector<VertexPC>& verts, const std::vector<uint32_t>& indices) {
    return uploadMesh(ruinObs_, verts, indices);
  }
  bool uploadRuinTempleMesh(const std::vector<VertexPC>& verts, const std::vector<uint32_t>& indices) {
    return uploadMesh(ruinTemple_, verts, indices);
  }
  bool uploadTallMesh(const std::vector<VertexPC>& verts, const std::vector<uint32_t>& indices) {
    return uploadMesh(treeMeshes_[0], verts, indices);
  }
  /** Upload one of 10 Crystal Nebula tree species (0..kTreeTypes-1). */
  bool uploadTreeMesh(int treeIndex, const std::vector<VertexPC>& verts,
                      const std::vector<uint32_t>& indices) {
    if (treeIndex < 0 || treeIndex >= kTreeTypes) return false;
    return uploadMesh(treeMeshes_[static_cast<size_t>(treeIndex)], verts, indices);
  }
  static constexpr int kTreeTypes = 10;
  bool uploadPathMesh(const std::vector<VertexPC>& verts, const std::vector<uint32_t>& indices) {
    return uploadMesh(pathRibbon_, verts, indices);
  }
  bool uploadParticles(const std::vector<ParticleGPU>& particles);
  /** Many crystal / ruin point lights (SSBO binding 18). Cap ~64 nearest. */
  bool uploadCrystalLights(const std::vector<CrystalLightGPU>& lights);
  uint32_t crystalLightCount() const { return crystalLightCount_; }

  /**
   * Load bolt_fur PBR set (Imagine → grok_import) onto the 3D GSD mesh.
   * basePath e.g. assets/materials/bolt/bolt_fur (no extension).
   */
  bool loadBoltFurPBR(const std::string& furBasePath);
  bool hasBoltFur() const { return boltFurValid_; }

  static constexpr int kBoltPartCount = 7;

  /** Load single PBR set into ground slot (legacy helper). */
  bool loadTerrainMaterial(const std::string& albedoPath, const std::string& normalPath,
                           const std::string& roughnessPath);

  /**
   * Load Crystal biome pack from material base paths (no extension):
   *   .../crystal_ground, .../crystal_rock, .../crystal_path, .../crystal_stalk
   * Empty string skips that layer.
   */
  bool loadBiomeMaterials(const std::string& groundBase, const std::string& rockBase,
                          const std::string& pathBase, const std::string& stalkBase);

  /** Packed instance SSBO: stalk|bush|tree0..9|detail|monolith|arch|obs|temple */
  struct SceneInstanceCounts {
    uint32_t stalkCount = 0;
    uint32_t stalkFirst = 0;
    uint32_t bushCount = 0;
    uint32_t bushFirst = 0;
    uint32_t treeCount[10]{};
    uint32_t treeFirst[10]{};
    uint32_t tallCount = 0; // sum of treeCount (compat / logging)
    uint32_t tallFirst = 0; // first tree batch start
    uint32_t detailCount = 0;
    uint32_t detailFirst = 0;
    uint32_t ruinCount = 0; // monoliths
    uint32_t ruinFirst = 0;
    uint32_t ruinArchCount = 0;
    uint32_t ruinArchFirst = 0;
    uint32_t ruinObsCount = 0;
    uint32_t ruinObsFirst = 0;
    uint32_t ruinTempleCount = 0;
    uint32_t ruinTempleFirst = 0;
  };

  void drawFrame(const FrameUBO& ubo, const SceneInstanceCounts& counts,
                 const ObjectPush* boltParts, int boltPartCount, uint32_t particleCount);

  /** Enable GPU frustum/distance cull + DrawIndexedIndirect for veg/ruins. */
  void setGpuCulling(bool enabled) { gpuCullEnabled_ = enabled; }
  bool gpuCulling() const { return gpuCullEnabled_ && cullPipeline_ != VK_NULL_HANDLE; }

  /** Feed post (bloom, TAA, motion blur). */
  void setPostParams(float score, double timeSec, float camSpeed = 0.f) {
    postScore_ = score;
    postTime_ = timeSec;
    postCamSpeed_ = camSpeed;
  }
  void setTemporalMatrices(const glm::mat4& invViewProj, const glm::mat4& prevViewProj,
                           const glm::vec4& jitterCurrPrev) {
    postInvViewProj_ = invViewProj;
    postPrevViewProj_ = prevViewProj;
    postJitter_ = jitterCurrPrev;
  }
  void setSunScreen(const glm::vec2& sunUv, float godRayStr, float gradeStr) {
    postSunUv_ = sunUv;
    postGodRay_ = godRayStr;
    postGrade_ = gradeStr;
  }

  std::uint32_t frameIndex() const { return frameIndex_; }
  VkDevice device() const { return device_; }
  bool hasTerrainTextures() const { return groundMat_.valid; }
  float materialFlags() const { return static_cast<float>(matFlags_); }

private:
  bool createInstance();
  bool createSurface(GLFWwindow* window);
  bool pickPhysicalDevice();
  bool createDevice();
  bool createSwapchain();
  bool createRenderPass();
  bool createFramebuffers();
  bool createCommandPool();
  bool createSync();
  bool createDescriptorSetLayout();
  bool createPipelines();
  bool createUniformBuffers();
  bool createDescriptorPoolAndSets();
  void updateMaterialDescriptors();
  void cleanupSwapchain();
  bool recreateSwapchain();

  uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;
  bool createVmaAllocator();
  void destroyVmaAllocator();
  bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                    GpuBuffer& out);
  void destroyBuffer(GpuBuffer& b);
  bool copyToBuffer(GpuBuffer& dst, const void* data, VkDeviceSize size);
  bool mapBufferPersistent(GpuBuffer& b, void** outMapped);
  bool createTextureFromRgba(const std::vector<uint8_t>& rgba, int w, int h, bool srgb, GpuTexture& out,
                             bool clampToEdge = false);
  bool createTextureFromGrey(const std::vector<uint8_t>& grey, int w, int h, GpuTexture& out);
  void destroyTexture(GpuTexture& t);

  bool transitionImage(VkImage image, VkImageLayout oldL, VkImageLayout newL);
  bool copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t w, uint32_t h);
  VkShaderModule loadShaderModule(const std::string& path) const;
  VkCommandBuffer beginOneTimeCommands() const;
  void endOneTimeCommands(VkCommandBuffer cmd) const;
  bool createDefault1x1Textures();
  bool loadMaterialSet(const std::string& basePath, MaterialGpu& out);
  void destroyMaterialOwned(MaterialGpu& m);
  void bindMaterialOrDefault(const MaterialGpu& m, VkDescriptorImageInfo& iAlb,
                             VkDescriptorImageInfo& iNrm, VkDescriptorImageInfo& iRgh) const;

  bool valid_ = false;
  GLFWwindow* window_ = nullptr;
  VkInstance instance_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue graphicsQueue_ = VK_NULL_HANDLE;
  VkQueue presentQueue_ = VK_NULL_HANDLE;
  uint32_t graphicsFamily_ = 0;
  uint32_t presentFamily_ = 0;
  void* vmaAllocator_ = nullptr; // VmaAllocator when BOLT_USE_VMA

  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkFormat swapFormat_ = VK_FORMAT_B8G8R8A8_SRGB;
  VkExtent2D swapExtent_{};
  std::vector<VkImage> swapImages_;
  std::vector<VkImageView> swapViews_;
  std::vector<VkFramebuffer> framebuffers_;

  VkImage depthImage_ = VK_NULL_HANDLE;
  VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
  VkImageView depthView_ = VK_NULL_HANDLE;
  VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;

  bool createDepthResources();
  void destroyDepthResources();
  bool createSceneTarget();
  void destroySceneTarget();
  bool createPostPass();
  void destroyPostPass();
  bool createPostPipeline();
  void updatePostDescriptors();
  bool createCullPipeline();
  void ensureCullBuffers(uint32_t instanceCapacity);
  void dispatchGpuCull(VkCommandBuffer cmd, const FrameUBO& ubo,
                       const SceneInstanceCounts& counts);
  bool createDeferredResources();
  void destroyDeferredResources();
  bool createDeferredPipelines();
  void updateDeferredDescriptors();
  bool createShadowResources();
  void destroyShadowResources();
  bool createShadowPipelines();
  void renderShadowMap(VkCommandBuffer cmd, const FrameUBO& ubo,
                       const SceneInstanceCounts& counts);

  VkRenderPass renderPass_ = VK_NULL_HANDLE;
  // Deferred GBuffer (terrain/path) → lighting → forward rest
  VkRenderPass gbufferRenderPass_ = VK_NULL_HANDLE;
  VkRenderPass lightRenderPass_ = VK_NULL_HANDLE;
  VkRenderPass forwardOverlayPass_ = VK_NULL_HANDLE;
  VkFramebuffer gbufferFramebuffer_ = VK_NULL_HANDLE;
  VkFramebuffer lightFramebuffer_ = VK_NULL_HANDLE;
  VkFramebuffer forwardOverlayFb_ = VK_NULL_HANDLE;
  VkImage gAlbedoImage_ = VK_NULL_HANDLE;
  VkDeviceMemory gAlbedoMemory_ = VK_NULL_HANDLE;
  VkImageView gAlbedoView_ = VK_NULL_HANDLE;
  VkImage gNormalImage_ = VK_NULL_HANDLE;
  VkDeviceMemory gNormalMemory_ = VK_NULL_HANDLE;
  VkImageView gNormalView_ = VK_NULL_HANDLE;
  VkImage gEmitImage_ = VK_NULL_HANDLE;
  VkDeviceMemory gEmitMemory_ = VK_NULL_HANDLE;
  VkImageView gEmitView_ = VK_NULL_HANDLE;
  VkPipeline gbufTerrainPipeline_ = VK_NULL_HANDLE;
  VkPipeline deferredLightPipeline_ = VK_NULL_HANDLE;
  VkPipelineLayout deferredLightLayout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout deferredLightDescLayout_ = VK_NULL_HANDLE;
  VkDescriptorPool deferredLightDescPool_ = VK_NULL_HANDLE;
  VkDescriptorSet deferredLightDescSet_ = VK_NULL_HANDLE;
  bool deferredReady_ = false;

  // Directional sun shadow map
  static constexpr uint32_t kShadowMapSize = 1024;
  VkImage shadowImage_ = VK_NULL_HANDLE;
  VkDeviceMemory shadowMemory_ = VK_NULL_HANDLE;
  VkImageView shadowView_ = VK_NULL_HANDLE;
  VkSampler shadowSampler_ = VK_NULL_HANDLE;
  VkFramebuffer shadowFramebuffer_ = VK_NULL_HANDLE;
  VkRenderPass shadowRenderPass_ = VK_NULL_HANDLE;
  VkPipeline shadowMeshPipeline_ = VK_NULL_HANDLE;
  VkPipeline shadowFoliagePipeline_ = VK_NULL_HANDLE;
  bool shadowReady_ = false;
  VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
  VkPipeline skyPipeline_ = VK_NULL_HANDLE;
  VkPipeline terrainPipeline_ = VK_NULL_HANDLE;
  VkPipeline foliagePipeline_ = VK_NULL_HANDLE;
  VkPipeline blobPipeline_ = VK_NULL_HANDLE;
  VkPipeline boltPipeline_ = VK_NULL_HANDLE;
  VkPipeline particlePipeline_ = VK_NULL_HANDLE;
  VkPipeline postPipeline_ = VK_NULL_HANDLE;
  VkPipelineLayout postPipelineLayout_ = VK_NULL_HANDLE;
  VkRenderPass postRenderPass_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout descLayout_ = VK_NULL_HANDLE;
  VkDescriptorPool descPool_ = VK_NULL_HANDLE;
  std::vector<VkDescriptorSet> descSets_;
  VkDescriptorSetLayout postDescLayout_ = VK_NULL_HANDLE;
  VkDescriptorPool postDescPool_ = VK_NULL_HANDLE;
  VkDescriptorSet postDescSet_ = VK_NULL_HANDLE;
  VkSampler postSampler_ = VK_NULL_HANDLE;
  GpuBuffer postUbo_{};
  void* postUboMapped_ = nullptr;

  // Offscreen scene color for post (bloom / tonemap)
  VkImage sceneImage_ = VK_NULL_HANDLE;
  VkDeviceMemory sceneMemory_ = VK_NULL_HANDLE;
  VkImageView sceneView_ = VK_NULL_HANDLE;
  VkFramebuffer sceneFramebuffer_ = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> postFramebuffers_;

  // TAA history (previous frame scene color)
  VkImage historyImage_ = VK_NULL_HANDLE;
  VkDeviceMemory historyMemory_ = VK_NULL_HANDLE;
  VkImageView historyView_ = VK_NULL_HANDLE;
  bool historyValid_ = false;

  VkSampler depthSampler_ = VK_NULL_HANDLE;
  float postCamSpeed_ = 0.f;
  glm::mat4 postInvViewProj_{1.f};
  glm::mat4 postPrevViewProj_{1.f};
  glm::vec4 postJitter_{0.f};
  glm::vec2 postSunUv_{0.5f, 0.35f};
  float postGodRay_ = 0.45f;
  float postGrade_ = 0.5f;

  VkCommandPool cmdPool_ = VK_NULL_HANDLE;
  std::vector<VkCommandBuffer> cmdBuffers_;

  static constexpr int kMaxFrames = 2;
  VkSemaphore imageAvailable_[kMaxFrames]{};
  VkSemaphore renderFinished_[kMaxFrames]{};
  VkFence inFlight_[kMaxFrames]{};
  uint32_t frameIndex_ = 0;

  std::vector<GpuBuffer> uniformBuffers_;
  std::vector<void*> uniformMapped_;

  GpuMesh terrain_;
  GpuMesh stalk_;
  GpuMesh bush_;
  GpuMesh tall_; // legacy alias unused — trees use treeMeshes_
  std::array<GpuMesh, 10> treeMeshes_{};
  GpuMesh detail_;
  GpuMesh ruin_;       // Resonance Monolith
  GpuMesh ruinArch_;   // Floating Archway
  GpuMesh ruinObs_;    // Crystal Observatory
  GpuMesh ruinTemple_; // Buried Temple
  GpuMesh pathRibbon_;
  GpuMesh blob_;
  GpuMesh bolt_; // legacy single mesh
  std::array<GpuMesh, kBoltPartCount> boltParts_{};
  GpuBuffer foliageInstanceBuf_{}; // packed source: stalk|bush|tall|detail|ruins
  GpuBuffer foliageCulledBuf_{};   // GPU-compacted visible instances
  GpuBuffer cullParamBuf_{};
  GpuBuffer indirectCmdBuf_{};     // 8 × VkDrawIndexedIndirectCommand
  void* cullParamMapped_ = nullptr;
  void* indirectMapped_ = nullptr;
  GpuBuffer particleBuf_{};
  GpuBuffer crystalLightBuf_{};
  uint32_t foliageCapacity_ = 0;
  uint32_t foliageCount_ = 0;
  uint32_t particleCapacity_ = 0;
  uint32_t particleCount_ = 0;
  uint32_t crystalLightCapacity_ = 0;
  uint32_t crystalLightCount_ = 0;
  static constexpr uint32_t kMaxCrystalLights = 64;
  VkPipeline pathPipeline_ = VK_NULL_HANDLE;
  VkPipeline cullPipeline_ = VK_NULL_HANDLE;
  VkPipelineLayout cullPipelineLayout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout cullDescLayout_ = VK_NULL_HANDLE;
  VkDescriptorPool cullDescPool_ = VK_NULL_HANDLE;
  VkDescriptorSet cullDescSet_ = VK_NULL_HANDLE;
  bool gpuCullEnabled_ = true;
  /** stalk, bush, tree×10, detail, monolith, arch, obs, temple = 17 */
  static constexpr uint32_t kIndirectBatches = 17;
  /** +17 contact-shadow blob draws */
  static constexpr uint32_t kIndirectCmdSlots = 34;

  void bindSsbo(uint32_t setIndex, const GpuBuffer& buf);
  void bindSsboBinding(uint32_t setIndex, uint32_t binding, const GpuBuffer& buf);

  MaterialGpu groundMat_{};
  MaterialGpu rockMat_{};
  MaterialGpu pathMat_{};
  MaterialGpu stalkMat_{};
  int matFlags_ = 0;

  GpuTexture defaultAlbedo_{};
  GpuTexture defaultNormal_{};
  GpuTexture defaultRough_{};
  GpuTexture boltAlbedo_{};
  GpuTexture boltNormal_{};
  GpuTexture boltRough_{};     // packed R rough G metal B height
  GpuTexture boltEmissive_{};
  GpuTexture boltHeight_{};
  GpuTexture defaultEmissive_{}; // black 1x1
  bool boltFurValid_ = false;
  bool boltHasEmissive_ = false;
  bool boltHasHeight_ = false;

  int width_ = 1280;
  int height_ = 720;
  bool framebufferResized_ = false;
  float postScore_ = 0.5f;
  double postTime_ = 0.0;
};

} // namespace bolt
