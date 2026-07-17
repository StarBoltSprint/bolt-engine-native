#include "bolt/assets/GltfLoader.hpp"
#include "bolt/core/Log.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace bolt {
namespace {

using json = nlohmann::json;

std::vector<uint8_t> readAll(const std::string& path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) return {};
  const auto sz = static_cast<size_t>(f.tellg());
  f.seekg(0);
  std::vector<uint8_t> buf(sz);
  f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz));
  return buf;
}

bool parseGlb(const std::vector<uint8_t>& data, json& outJson, std::vector<uint8_t>& outBin) {
  if (data.size() < 12) return false;
  const uint32_t magic = *reinterpret_cast<const uint32_t*>(data.data());
  if (magic != 0x46546C67u) return false; // glTF
  size_t off = 12;
  std::string jsonStr;
  while (off + 8 <= data.size()) {
    const uint32_t chunkLen = *reinterpret_cast<const uint32_t*>(data.data() + off);
    const uint32_t chunkType = *reinterpret_cast<const uint32_t*>(data.data() + off + 4);
    off += 8;
    if (off + chunkLen > data.size()) return false;
    if (chunkType == 0x4E4F534Au) { // JSON
      jsonStr.assign(reinterpret_cast<const char*>(data.data() + off), chunkLen);
    } else if (chunkType == 0x004E4942u) { // BIN
      outBin.assign(data.begin() + static_cast<std::ptrdiff_t>(off),
                    data.begin() + static_cast<std::ptrdiff_t>(off + chunkLen));
    }
    off += chunkLen;
  }
  if (jsonStr.empty()) return false;
  outJson = json::parse(jsonStr, nullptr, false);
  return !outJson.is_discarded();
}

template <typename T>
std::vector<T> readAccessor(const json& root, const std::vector<uint8_t>& bin, int accessorIndex) {
  std::vector<T> out;
  if (accessorIndex < 0) return out;
  const auto& acc = root["accessors"][accessorIndex];
  const int bvIndex = acc.value("bufferView", -1);
  if (bvIndex < 0) return out;
  const auto& bv = root["bufferViews"][bvIndex];
  const size_t bvOff = bv.value("byteOffset", 0);
  const size_t accOff = acc.value("byteOffset", 0);
  const size_t count = acc.value("count", 0);
  size_t stride = bv.value("byteStride", 0);
  if (stride == 0) stride = sizeof(T);
  out.resize(count);
  const uint8_t* base = bin.data() + bvOff + accOff;
  for (size_t i = 0; i < count; ++i) {
    std::memcpy(&out[i], base + i * stride, sizeof(T));
  }
  return out;
}

void computeNormals(ObjMesh& mesh) {
  for (auto& v : mesh.vertices) v.normal = {0, 0, 0};
  for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    auto& a = mesh.vertices[mesh.indices[i]];
    auto& b = mesh.vertices[mesh.indices[i + 1]];
    auto& c = mesh.vertices[mesh.indices[i + 2]];
    glm::vec3 fn = glm::cross(b.pos - a.pos, c.pos - a.pos);
    float len = glm::length(fn);
    if (len > 1e-8f) fn /= len;
    a.normal += fn;
    b.normal += fn;
    c.normal += fn;
  }
  for (auto& v : mesh.vertices) {
    float len = glm::length(v.normal);
    v.normal = len > 1e-8f ? v.normal / len : glm::vec3(0, 1, 0);
  }
}

void boundsOf(const ObjMesh& mesh, glm::vec3& bmin, glm::vec3& bmax) {
  bmin = bmax = mesh.vertices[0].pos;
  for (const auto& v : mesh.vertices) {
    bmin = glm::min(bmin, v.pos);
    bmax = glm::max(bmax, v.pos);
  }
}

bool extractMesh(const json& root, const std::vector<uint8_t>& bin, ObjMesh& out) {
  if (!root.contains("meshes") || root["meshes"].empty()) return false;
  const auto& prim = root["meshes"][0]["primitives"][0];
  const auto& attrs = prim["attributes"];
  if (!attrs.contains("POSITION")) return false;

  const int posAcc = attrs["POSITION"];
  const int uvAcc = attrs.contains("TEXCOORD_0") ? attrs["TEXCOORD_0"].get<int>() : -1;
  const int nrmAcc = attrs.contains("NORMAL") ? attrs["NORMAL"].get<int>() : -1;
  const int idxAcc = prim.contains("indices") ? prim["indices"].get<int>() : -1;

  struct V3 {
    float x, y, z;
  };
  struct V2 {
    float x, y;
  };
  auto positions = readAccessor<V3>(root, bin, posAcc);
  auto uvs = uvAcc >= 0 ? readAccessor<V2>(root, bin, uvAcc) : std::vector<V2>{};
  auto normals = nrmAcc >= 0 ? readAccessor<V3>(root, bin, nrmAcc) : std::vector<V3>{};

  if (positions.empty()) return false;

  out.vertices.clear();
  out.indices.clear();
  out.vertices.reserve(positions.size());
  for (size_t i = 0; i < positions.size(); ++i) {
    VertexPC v{};
    v.pos = {positions[i].x, positions[i].y, positions[i].z};
    if (i < normals.size()) v.normal = {normals[i].x, normals[i].y, normals[i].z};
    if (i < uvs.size()) v.uv = {uvs[i].x, uvs[i].y};
    v.matId = 0.f;
    out.vertices.push_back(v);
  }

  if (idxAcc >= 0) {
    const auto& acc = root["accessors"][idxAcc];
    const int comp = acc.value("componentType", 5123);
    const size_t count = acc.value("count", 0);
    const int bvIndex = acc.value("bufferView", -1);
    const auto& bv = root["bufferViews"][bvIndex];
    const size_t off = bv.value("byteOffset", 0) + acc.value("byteOffset", 0);
    out.indices.resize(count);
    if (comp == 5123) {
      const uint16_t* p = reinterpret_cast<const uint16_t*>(bin.data() + off);
      for (size_t i = 0; i < count; ++i) out.indices[i] = p[i];
    } else if (comp == 5125) {
      const uint32_t* p = reinterpret_cast<const uint32_t*>(bin.data() + off);
      for (size_t i = 0; i < count; ++i) out.indices[i] = p[i];
    } else {
      return false;
    }
  } else {
    out.indices.resize(out.vertices.size());
    for (uint32_t i = 0; i < out.indices.size(); ++i) out.indices[i] = i;
  }

  // Always recompute smooth-ish normals for lighting (skinned meshes often lack NORMAL)
  computeNormals(out);
  return !out.vertices.empty() && !out.indices.empty();
}

/**
 * Fit imported quadruped into engine space:
 * - Y-up, feet on y=0
 * - longest horizontal axis → forward length
 * - nose points +Z (sprint forward)
 * - target height ~2.0 m so character fills chase cam
 */
void normalizeDogMesh(ObjMesh& mesh) {
  if (mesh.vertices.empty()) return;

  glm::vec3 bmin, bmax;
  boundsOf(mesh, bmin, bmax);
  glm::vec3 size = bmax - bmin;
  logInfo("glTF raw bounds size=(" + std::to_string(size.x) + "," + std::to_string(size.y) + "," +
          std::to_string(size.z) + ")");

  // If model is Z-up (Z largest, Y small), swap Y/Z
  if (size.z > size.y * 1.15f && size.y < size.x) {
    for (auto& v : mesh.vertices) {
      float y = v.pos.y, z = v.pos.z;
      v.pos.y = z;
      v.pos.z = -y;
      float ny = v.normal.y, nz = v.normal.z;
      v.normal.y = nz;
      v.normal.z = -ny;
    }
    boundsOf(mesh, bmin, bmax);
    size = bmax - bmin;
    logInfo("glTF applied Z-up → Y-up conversion");
  }

  // Scale so height is ~2.0m (readable in game)
  const float h = std::max(size.y, 1e-3f);
  const float targetH = 2.05f;
  const float s = targetH / h;
  const glm::vec3 center((bmin.x + bmax.x) * 0.5f, bmin.y, (bmin.z + bmax.z) * 0.5f);

  for (auto& v : mesh.vertices) {
    glm::vec3 p = (v.pos - center) * s;
    // 180° yaw: many sample animals face -Z
    p = glm::vec3(-p.x, p.y, -p.z);
    v.pos = p;
    v.normal = glm::normalize(glm::vec3(-v.normal.x, v.normal.y, -v.normal.z));
  }

  // Snap feet exactly to y=0 after scale
  boundsOf(mesh, bmin, bmax);
  const float y0 = bmin.y;
  for (auto& v : mesh.vertices) v.pos.y -= y0;

  boundsOf(mesh, bmin, bmax);
  size = bmax - bmin;
  logInfo("glTF normalized size=(" + std::to_string(size.x) + "," + std::to_string(size.y) + "," +
          std::to_string(size.z) + ") y=[" + std::to_string(bmin.y) + ".." +
          std::to_string(bmax.y) + "]");
}

} // namespace

bool loadGltfMesh(const std::string& path, ObjMesh& out) {
  out = {};
  json root;
  std::vector<uint8_t> bin;

  const bool isGlb = path.size() >= 4 && (path.compare(path.size() - 4, 4, ".glb") == 0 ||
                                          path.compare(path.size() - 4, 4, ".GLB") == 0);

  if (isGlb) {
    auto data = readAll(path);
    if (data.empty() || !parseGlb(data, root, bin)) {
      logWarn("Failed to parse GLB: " + path);
      return false;
    }
  } else {
    auto data = readAll(path);
    if (data.empty()) {
      logWarn("Failed to read glTF: " + path);
      return false;
    }
    root = json::parse(data.begin(), data.end(), nullptr, false);
    if (root.is_discarded()) return false;
    if (root.contains("buffers") && !root["buffers"].empty()) {
      std::string uri = root["buffers"][0].value("uri", "");
      if (!uri.empty() && uri.find("data:") != 0) {
        const auto slash = path.find_last_of("/\\");
        const std::string dir = slash == std::string::npos ? "" : path.substr(0, slash + 1);
        bin = readAll(dir + uri);
      }
    }
    if (bin.empty()) {
      logWarn("glTF has no binary buffer: " + path);
      return false;
    }
  }

  if (!extractMesh(root, bin, out)) {
    logWarn("No mesh extracted from: " + path);
    return false;
  }
  normalizeDogMesh(out);
  logInfo("glTF mesh ready " + path + " verts=" + std::to_string(out.vertices.size()) +
          " tris=" + std::to_string(out.indices.size() / 3));
  return true;
}

} // namespace bolt
