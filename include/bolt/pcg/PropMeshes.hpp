#pragma once
#include "bolt/render/GpuMesh.hpp"
#include <vector>

namespace bolt {

/** Crystal bush / low mound for veg variety */
void buildBushMesh(std::vector<VertexPC>& v, std::vector<uint32_t>& i);

/** Small crystal shard / pebble for detail PCG */
void buildDetailShardMesh(std::vector<VertexPC>& v, std::vector<uint32_t>& i);

/** Ruin pillar stump for hero ruins */
void buildRuinPillarMesh(std::vector<VertexPC>& v, std::vector<uint32_t>& i);

/** Broken arch piece for ruins */
void buildRuinArchMesh(std::vector<VertexPC>& v, std::vector<uint32_t>& i);

/** Tall thin crystal for veg kind variety */
void buildTallCrystalMesh(std::vector<VertexPC>& v, std::vector<uint32_t>& i);

} // namespace bolt
