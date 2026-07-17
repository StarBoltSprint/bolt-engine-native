#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <glm/glm.hpp>
#include "bolt/render/GpuMesh.hpp"
#include "bolt/sprint/SprintCore.hpp"
#include "bolt/render/QualityTier.hpp"

struct GLFWwindow;

namespace bolt {

struct FrameUBO {
  glm::mat4 viewProj;
  glm::vec4 cameraPos_time;     // xyz cam, w time
  glm::vec4 sprintScore_flags;  // x score, y gpuHeight flag
};

/**
 * Real Vulkan device + swapchain + simple forward renderer for Crystal slice.
 * Draws: heightfield terrain, instanced foliage, clear color sky.
 */
class VulkanContext {
public:
  bool initialize(GLFWwindow* window);
  void shutdown();
  bool isValid() const { return valid_; }

  void beginFrame();
  void endFrame();
  void resize(int w, int h);

  /** Upload / replace terrain mesh (CPU → GPU). */
  bool uploadTerrain(const std::vector<VertexPC>& verts, const std::vector<uint32_t>& indices);

  /** Unit stalk mesh for instancing. */
  bool uploadStalkMesh(const std::vector<VertexPC>& verts, const std::vector<uint32_t>& indices);

  /** Upload foliage instances (dynamic). */
  bool uploadFoliage(const std::vector<FoliageInstanceGPU>& instances);

  /** Per-frame camera + sprint uniforms + draw. */
  void drawFrame(const FrameUBO& ubo, uint32_t foliageCount);

  std::uint32_t frameIndex() const { return frameIndex_; }
  VkDevice device() const { return device_; }

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
  void cleanupSwapchain();
  bool recreateSwapchain();

  uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;
  bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                    GpuBuffer& out);
  void destroyBuffer(GpuBuffer& b);
  bool copyToBuffer(GpuBuffer& dst, const void* data, VkDeviceSize size);
  VkShaderModule loadShaderModule(const std::string& path) const;
  VkCommandBuffer beginOneTimeCommands() const;
  void endOneTimeCommands(VkCommandBuffer cmd) const;

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

  VkRenderPass renderPass_ = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
  VkPipeline terrainPipeline_ = VK_NULL_HANDLE;
  VkPipeline foliagePipeline_ = VK_NULL_HANDLE;
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
  uint32_t currentImage_ = 0;

  std::vector<GpuBuffer> uniformBuffers_;
  std::vector<void*> uniformMapped_;

  GpuMesh terrain_;
  GpuMesh stalk_;
  GpuBuffer foliageInstanceBuf_{};
  uint32_t foliageCapacity_ = 0;
  uint32_t foliageCount_ = 0;

  int width_ = 1280;
  int height_ = 720;
  bool framebufferResized_ = false;
};

} // namespace bolt
