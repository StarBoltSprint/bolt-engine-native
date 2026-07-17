#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

namespace bolt {

struct VertexPC {
  glm::vec3 pos{0.f};
  glm::vec3 normal{0.f, 1.f, 0.f};
  glm::vec2 uv{0.f};
  float matId = 0.f; // 0 fur, 1 eye, 2 nose, 3 ear, 4 pad, 5 aura
  float pad = 0.f;
};

struct FoliageInstanceGPU {
  glm::vec4 posScale; // xyz, w=scale
  glm::vec4 yawKind;  // x=yaw, y=kind
};

/** GPU particle (dust / trail) — matches particle.vert Particle struct */
struct ParticleGPU {
  glm::vec4 posSize;   // xyz, w=size
  glm::vec4 colorLife; // rgb, a=life 0..1
};

/** Push constants for bolt mesh (std430-ish: mat4 + vec4) */
struct ObjectPush {
  glm::mat4 model;
  glm::vec4 color;
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
