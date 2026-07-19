#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <vector>

namespace bolt {

struct GpuTexture {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkSampler sampler = VK_NULL_HANDLE;
  uint32_t width = 0;
  uint32_t height = 0;
  VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
};

struct MaterialGpu {
  GpuTexture albedo;
  GpuTexture normal;
  /** Packed ORM-ish: R=roughness G=metallic B=height (0.5 = flat) */
  GpuTexture roughness;
  /** Optional RGB emissive (Imagine maps); black if missing */
  GpuTexture emissive;
  bool valid = false;
  bool hasEmissive = false;
  bool hasMetallic = false;
  bool hasHeight = false;
};

} // namespace bolt
