#pragma once
#include "bolt/render/GpuMesh.hpp"
#include <vector>

namespace bolt {

/** Simple crystal stalk (CPU mesh) for instancing. */
void buildStalkMesh(std::vector<VertexPC>& outVerts, std::vector<uint32_t>& outIndices);

} // namespace bolt
