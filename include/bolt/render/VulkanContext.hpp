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
  // z/w reserved
  glm::vec4 sprintScore_flags;
  // x = triplanar tiling, y = path half-width, z = path edge falloff, w = path meander amp
  glm::vec4 tiling_pad;
  glm::mat4 invViewProj;        // sky ray reconstruction
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
  bool uploadFoliage(const std::vector<FoliageInstanceGPU>& instances);
  bool uploadParticles(const std::vector<ParticleGPU>& particles);

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

  void drawFrame(const FrameUBO& ubo, uint32_t foliageCount,
                 const ObjectPush* boltParts, int boltPartCount, uint32_t particleCount);

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
  bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                    GpuBuffer& out);
  void destroyBuffer(GpuBuffer& b);
  bool copyToBuffer(GpuBuffer& dst, const void* data, VkDeviceSize size);
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

  VkRenderPass renderPass_ = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
  VkPipeline skyPipeline_ = VK_NULL_HANDLE;
  VkPipeline terrainPipeline_ = VK_NULL_HANDLE;
  VkPipeline foliagePipeline_ = VK_NULL_HANDLE;
  VkPipeline blobPipeline_ = VK_NULL_HANDLE;
  VkPipeline boltPipeline_ = VK_NULL_HANDLE;
  VkPipeline particlePipeline_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout descLayout_ = VK_NULL_HANDLE;
  VkDescriptorPool descPool_ = VK_NULL_HANDLE;
  std::vector<VkDescriptorSet> descSets_;

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
  GpuMesh blob_;
  GpuMesh bolt_; // legacy single mesh
  std::array<GpuMesh, kBoltPartCount> boltParts_{};
  GpuBuffer foliageInstanceBuf_{};
  GpuBuffer particleBuf_{};
  uint32_t foliageCapacity_ = 0;
  uint32_t foliageCount_ = 0;
  uint32_t particleCapacity_ = 0;
  uint32_t particleCount_ = 0;

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
  GpuTexture boltRough_{};
  bool boltFurValid_ = false;

  int width_ = 1280;
  int height_ = 720;
  bool framebufferResized_ = false;
};

} // namespace bolt
