#pragma once
#include "bolt/render/GpuMesh.hpp"
#include <vector>

namespace bolt {

/** Soft contact-shadow disc (XZ unit quad, UV for radial falloff). */
void buildShadowBlobMesh(std::vector<VertexPC>& outVerts, std::vector<uint32_t>& outIndices);

/** Simple pure-white Bolt silhouette (capsule body + head) for scale. */
void buildBoltMesh(std::vector<VertexPC>& outVerts, std::vector<uint32_t>& outIndices);

} // namespace bolt
