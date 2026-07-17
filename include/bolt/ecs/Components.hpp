#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <string>

namespace bolt {

// ---------------------------------------------------------------------------
// Core transform / motion
// ---------------------------------------------------------------------------
struct Transform {
  glm::vec3 position{0.f};
  glm::quat rotation{1.f, 0.f, 0.f, 0.f};
  glm::vec3 scale{1.f};
};

struct Velocity {
  glm::vec3 linear{0.f};
};

struct PlayerTag {};
struct BoltAura {
  float intensity = 0.f;     // 0..1 from sprint / score
  float pulsePhase = 0.f;
};

// ---------------------------------------------------------------------------
// Rendering handles (GPU resources live in RenderWorld, referenced by id)
// ---------------------------------------------------------------------------
using MaterialId = std::uint32_t;
using MeshId = std::uint32_t;
constexpr MaterialId kInvalidMaterial = 0;
constexpr MeshId kInvalidMesh = 0;

struct MeshRenderer {
  MeshId mesh = kInvalidMesh;
  MaterialId material = kInvalidMaterial;
  bool castShadow = false;
};

/** GPU instanced foliage batch — one entity, thousands of instances. */
struct FoliageBatch {
  MeshId mesh = kInvalidMesh;
  MaterialId material = kInvalidMaterial;
  std::uint32_t instanceCount = 0;
  std::uint32_t instanceBufferId = 0; // renderer-owned SSBO/VBO
  float lodBias = 0.f;               // + when sprinting fast
};

struct TerrainChunk {
  int cx = 0, cz = 0;
  MeshId mesh = kInvalidMesh;
  MaterialId material = kInvalidMaterial;
  float scoreBake = 0.f;             // sprint score at build time
  bool gpuHeight = true;
};

struct PathSegment {
  MeshId mesh = kInvalidMesh;
  MaterialId material = kInvalidMaterial;
  float life = 12.f;
  float age = 0.f;
};

// ---------------------------------------------------------------------------
// PCG bookkeeping
// ---------------------------------------------------------------------------
enum class VegKind : std::uint8_t {
  Stalk, Flower, Crystal, Bush, Floater
};

struct ProceduralTag {
  float spawnTime = 0.f;
  float life = 10.f;
};

struct Fade {
  float value = 0.f;   // 0..1
  bool fadingOut = false;
};

// ---------------------------------------------------------------------------
// Sprint-aware lights / VFX
// ---------------------------------------------------------------------------
struct ParticleEmitter {
  std::uint32_t budget = 64;
  float density = 1.f;     // multiplied by sprint particleMul
  MaterialId material = kInvalidMaterial;
};

struct NameComponent {
  std::string name;
};

} // namespace bolt
