#pragma once
#include "bolt/assets/ObjLoader.hpp"
#include <string>

namespace bolt {

/**
 * Minimal glTF 2.0 mesh loader (POSITION + TEXCOORD_0, optional indices).
 * Supports .glb and .gltf + external .bin. Computes normals if missing.
 * Does not load skins/animations (static bind pose).
 */
bool loadGltfMesh(const std::string& path, ObjMesh& out);

} // namespace bolt
