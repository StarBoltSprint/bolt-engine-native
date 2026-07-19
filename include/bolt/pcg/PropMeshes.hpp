#pragma once
#include "bolt/render/GpuMesh.hpp"
#include <vector>

namespace bolt {

/** Crystal bush / low mound for veg variety */
void buildBushMesh(std::vector<VertexPC>& v, std::vector<uint32_t>& i);

/** Small crystal shard / pebble for detail PCG */
void buildDetailShardMesh(std::vector<VertexPC>& v, std::vector<uint32_t>& i);

/** Resonance Monolith — tall crystal pillar with growths */
void buildRuinPillarMesh(std::vector<VertexPC>& v, std::vector<uint32_t>& i);

/** Floating Archway — two pillars + broken lintel */
void buildRuinArchMesh(std::vector<VertexPC>& v, std::vector<uint32_t>& i);

/** Crystal Observatory — low ring / dome with crystal lenses */
void buildRuinObservatoryMesh(std::vector<VertexPC>& v, std::vector<uint32_t>& i);

/** Buried Temple — broad sunk mass with altar crystal */
void buildRuinTempleMesh(std::vector<VertexPC>& v, std::vector<uint32_t>& i);

/** Crystal Nebula tree types (hero vegetation) — 10 very distinct silhouettes */
static constexpr int kCrystalTreeTypes = 10;

/**
 * 0 Resonance Spear — classic tall thin pillar + crown
 * 1 Twisted Conduit — dual offset trunks
 * 2 Nebula Cap — short stout bole + wide canopy
 * 3 Amethyst Spire — multi-tier angular spires
 * 4 Weeping Crystal — hanging droop crystals
 * 5 Floating Crown — trunk + levitating cluster
 * 6 Rooted Prism — wide geometric root base
 * 7 Lumen Fern-Tree — stacked plate fronds
 * 8 Broken Sentinel — shattered pillar + regrowth
 * 9 Halo Bloom — ring of crystals around stem
 */
void buildCrystalTree(int typeIndex, std::vector<VertexPC>& v, std::vector<uint32_t>& i);

/** Legacy aliases */
void buildTallCrystalMesh(std::vector<VertexPC>& v, std::vector<uint32_t>& i);
void buildTallCrystalMeshB(std::vector<VertexPC>& v, std::vector<uint32_t>& i);
void buildTallCrystalMeshC(std::vector<VertexPC>& v, std::vector<uint32_t>& i);

} // namespace bolt
