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
  /** True when a full imported mesh is used (no separate leg parts). */
  bool fullMesh = false;
};

/**
 * Load free/imported low-poly dog mesh if present, else procedural multi-part.
 * Search order (first hit wins):
 *   assets/characters/bolt/bolt_gsd.glb
 *   assets/characters/bolt/bolt_gsd.obj
 *   explicit path argument
 * Full mesh → Body only + procedural Aura. Faces +Z, feet y=0.
 */
bool buildOrLoadBoltCharacter(BoltCharacterMeshes& out, const std::string& preferPath = "");

/** Write parts as a single OBJ (for editing / future re-import). */
bool saveBoltCharacterObj(const BoltCharacterMeshes& meshes, const std::string& objPath);

/**
 * Run-cycle local transforms for each part (relative to character root).
 * phase 0..1, speedFactor 0..1+ from sprint.
 */
void boltAnimTransforms(float phase, float speedFactor, float energy,
                        std::array<glm::mat4, static_cast<int>(BoltPart::Count)>& outLocal);

} // namespace bolt
