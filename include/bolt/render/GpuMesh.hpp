#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

namespace bolt {

struct VertexPC {
  glm::vec3 pos;
  glm::vec3 normal;
  glm::vec2 uv;
};

struct FoliageInstanceGPU {
  glm::vec4 posScale; // xyz, w=scale
  glm::vec4 yawKind;  // x=yaw, y=kind
};

struct GpuBuffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
};

struct GpuMesh {
  GpuBuffer vertex;
  GpuBuffer index;
  uint32_t indexCount = 0;
  uint32_t vertexCount = 0;
};

} // namespace bolt
