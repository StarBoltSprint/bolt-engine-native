#pragma once
#include "bolt/render/GpuMesh.hpp"
#include <string>
#include <vector>

namespace bolt {

struct ObjMesh {
  std::vector<VertexPC> vertices;
  std::vector<uint32_t> indices;
};

/** Minimal OBJ loader (v / vn / vt / f). Groups optional via usemtl matId names. */
bool loadObj(const std::string& path, ObjMesh& out, float defaultMatId = 0.f);

bool saveObj(const std::string& path, const std::vector<VertexPC>& verts,
             const std::vector<uint32_t>& indices);

} // namespace bolt
