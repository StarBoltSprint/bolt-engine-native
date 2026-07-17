#pragma once
#include "bolt/render/GpuMesh.hpp"
#include <array>
#include <string>
#include <vector>

namespace bolt {

/** Bolt mesh parts for animation / multi-draw */
enum class BoltPart : int {
  Body = 0, // torso, chest, hip, neck, head, ears, snout
  LegFL,
  LegFR,
  LegBL,
  LegBR,
  Tail,
  Aura, // energy shell (slightly larger, additive)
  Count
};

struct BoltPartMesh {
  std::vector<VertexPC> vertices;
  std::vector<uint32_t> indices;
};

struct BoltCharacterMeshes {
  std::array<BoltPartMesh, static_cast<int>(BoltPart::Count)> parts;
};

/**
 * Mid-poly procedural white GSD with clean cylindrical/planar UVs.
 * Faces +Z, feet near y=0. UV.xy = fur texture coords; matId on vertex.
 * If objPath exists, loads that instead (artist override).
 */
bool buildOrLoadBoltCharacter(BoltCharacterMeshes& out, const std::string& objPath = "");

/** Write parts as a single OBJ (for editing / future re-import). */
bool saveBoltCharacterObj(const BoltCharacterMeshes& meshes, const std::string& objPath);

/**
 * Run-cycle local transforms for each part (relative to character root).
 * phase 0..1, speedFactor 0..1+ from sprint.
 */
void boltAnimTransforms(float phase, float speedFactor, float energy,
                        std::array<glm::mat4, static_cast<int>(BoltPart::Count)>& outLocal);

} // namespace bolt
