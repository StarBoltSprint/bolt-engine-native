#pragma once
#include "bolt/render/GpuMesh.hpp"
#include <vector>

namespace bolt {

/** Soft contact-shadow disc (XZ unit quad, UV for radial falloff). */
void buildShadowBlobMesh(std::vector<VertexPC>& outVerts, std::vector<uint32_t>& outIndices);

/** Simple pure-white Bolt silhouette (capsule body + head) — legacy scale placeholder. */
void buildBoltMesh(std::vector<VertexPC>& outVerts, std::vector<uint32_t>& outIndices);

/**
 * Vertical card for Imagine GSD billboard (feet at y=0, faces +Z).
 * width/height in meters; UV covers full sprite.
 */
void buildBoltBillboardMesh(std::vector<VertexPC>& outVerts, std::vector<uint32_t>& outIndices,
                            float width = 1.55f, float height = 1.55f);

} // namespace bolt
