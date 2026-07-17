#pragma once
#include "bolt/render/GpuMesh.hpp"
#include <vector>

namespace bolt {

/** Soft contact-shadow disc (XZ unit quad, UV for radial falloff). */
void buildShadowBlobMesh(std::vector<VertexPC>& outVerts, std::vector<uint32_t>& outIndices);

/**
 * StarBoltSprint — procedural pure-white German Shepherd mesh.
 * Feet at y≈0, faces +Z (sprint forward). UV.x = material id:
 *   0 = fur, 1 = eye/energy, 2 = nose, 3 = ear-inner, 4 = pad
 */
void buildBoltMesh(std::vector<VertexPC>& outVerts, std::vector<uint32_t>& outIndices);

} // namespace bolt
