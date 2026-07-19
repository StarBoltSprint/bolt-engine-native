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

/** Gameplay-driven motion state (Path 2 — pack cycles as timing reference). */
enum class BoltMotion : int {
  Idle = 0, // breathing stand
  Run = 1,  // walk / sprint
  Jump = 2  // crouch → air → land
};

/**
 * Local transforms for each part (relative to character root).
 * phase 0..1, speedFactor 0..1+, energy 0..1, jumpT 0..1 while jumping.
 * turnRate rad/s (positive = turning left / CCW in Y-up yaw).
 */
void boltAnimTransforms(float phase, float speedFactor, float energy, BoltMotion motion,
                        float jumpT, float turnRate,
                        std::array<glm::mat4, static_cast<int>(BoltPart::Count)>& outLocal);

/** Lateral body offset (meters, + = dog's right) from turn + gait weave. */
float boltLateralSway(float phase, float speedFactor, float turnRate, BoltMotion motion);

/** Vertical offset (meters) of root for jump arc — crouch dip then lift then land. */
float boltJumpHeightOffset(float jumpT);

} // namespace bolt
