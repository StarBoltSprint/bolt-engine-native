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
  // x=yaw, y=kind, z=colorSeed 0-1, w=meshMorph (lean/squash variation)
  glm::vec4 yawKind;
};

/** GPU particle — matches particle.vert Particle struct
 *  kind: 0 soft dust, 1 pawprint trail, 2 crystal spark, 3 aura spark
 */
struct ParticleGPU {
  glm::vec4 posSize;   // xyz, w=size
  glm::vec4 colorLife; // rgb, a=life 0..1
  glm::vec4 params;    // x=kind, yzw reserved
};

/** Push constants for bolt mesh (mat4 + 2×vec4 = 96 bytes) */
struct ObjectPush {
  glm::mat4 model;
  glm::vec4 color; // rgb unused/tint, w = energy 0..1
  /** x=phase 0..1, y=speedFactor, z=hop (0 idle..1 run), w=1 if fullMesh limb deform */
  glm::vec4 anim{0.f};
};

/** Point light from crystals / ruins / aura (many-lights path toward deferred). */
struct CrystalLightGPU {
  glm::vec4 posRange;       // xyz world, w attenuation radius
  glm::vec4 colorIntensity; // rgb, w intensity
};

struct GpuBuffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE; // legacy path (non-VMA)
  void* vmaAllocation = nullptr;         // VmaAllocation when BOLT_USE_VMA
  void* mapped = nullptr;                // persistent map if any
  VkDeviceSize size = 0;
};

struct GpuMesh {
  GpuBuffer vertex;
  GpuBuffer index;
  uint32_t indexCount = 0;
  uint32_t vertexCount = 0;
};

} // namespace bolt
