#include "bolt/assets/ObjLoader.hpp"
#include "bolt/core/Log.hpp"
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace bolt {
namespace {

struct Key {
  int v = 0, t = 0, n = 0;
  bool operator==(const Key& o) const { return v == o.v && t == o.t && n == o.n; }
};
struct KeyHash {
  size_t operator()(const Key& k) const {
    return (static_cast<size_t>(k.v) * 73856093u) ^ (static_cast<size_t>(k.t) * 19349663u) ^
           (static_cast<size_t>(k.n) * 83492791u);
  }
};

} // namespace

bool loadObj(const std::string& path, ObjMesh& out, float defaultMatId) {
  std::ifstream in(path);
  if (!in) {
    logWarn("OBJ not found: " + path);
    return false;
  }
  std::vector<glm::vec3> pos;
  std::vector<glm::vec3> nrm;
  std::vector<glm::vec2> uv;
  out.vertices.clear();
  out.indices.clear();
  std::unordered_map<Key, uint32_t, KeyHash> map;
  float matId = defaultMatId;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ss(line);
    std::string tag;
    ss >> tag;
    if (tag == "v") {
      glm::vec3 p;
      ss >> p.x >> p.y >> p.z;
      pos.push_back(p);
    } else if (tag == "vn") {
      glm::vec3 n;
      ss >> n.x >> n.y >> n.z;
      nrm.push_back(n);
    } else if (tag == "vt") {
      glm::vec2 t;
      ss >> t.x >> t.y;
      uv.push_back(t);
    } else if (tag == "usemtl") {
      std::string name;
      ss >> name;
      if (name == "eye" || name == "energy") matId = 1.f;
      else if (name == "nose") matId = 2.f;
      else if (name == "ear") matId = 3.f;
      else if (name == "pad") matId = 4.f;
      else if (name == "aura") matId = 5.f;
      else matId = 0.f;
    } else if (tag == "f") {
      std::vector<uint32_t> face;
      std::string tok;
      while (ss >> tok) {
        Key k;
        // v / v/t / v//n / v/t/n
        int slash1 = static_cast<int>(tok.find('/'));
        if (slash1 < 0) {
          k.v = std::stoi(tok);
        } else {
          k.v = std::stoi(tok.substr(0, static_cast<size_t>(slash1)));
          int slash2 = static_cast<int>(tok.find('/', static_cast<size_t>(slash1 + 1)));
          if (slash2 < 0) {
            k.t = std::stoi(tok.substr(static_cast<size_t>(slash1 + 1)));
          } else {
            if (slash2 > slash1 + 1)
              k.t = std::stoi(tok.substr(static_cast<size_t>(slash1 + 1),
                                         static_cast<size_t>(slash2 - slash1 - 1)));
            if (slash2 + 1 < static_cast<int>(tok.size()))
              k.n = std::stoi(tok.substr(static_cast<size_t>(slash2 + 1)));
          }
        }
        auto it = map.find(k);
        if (it != map.end()) {
          face.push_back(it->second);
        } else {
          VertexPC vert{};
          const int vi = k.v > 0 ? k.v - 1 : static_cast<int>(pos.size()) + k.v;
          vert.pos = (vi >= 0 && vi < static_cast<int>(pos.size())) ? pos[static_cast<size_t>(vi)]
                                                                    : glm::vec3(0);
          if (k.n != 0) {
            const int ni = k.n > 0 ? k.n - 1 : static_cast<int>(nrm.size()) + k.n;
            vert.normal =
                (ni >= 0 && ni < static_cast<int>(nrm.size())) ? nrm[static_cast<size_t>(ni)]
                                                               : glm::vec3(0, 1, 0);
          } else {
            vert.normal = {0, 1, 0};
          }
          if (k.t != 0) {
            const int ti = k.t > 0 ? k.t - 1 : static_cast<int>(uv.size()) + k.t;
            vert.uv = (ti >= 0 && ti < static_cast<int>(uv.size())) ? uv[static_cast<size_t>(ti)]
                                                                    : glm::vec2(0);
          }
          vert.matId = matId;
          const uint32_t idx = static_cast<uint32_t>(out.vertices.size());
          out.vertices.push_back(vert);
          map[k] = idx;
          face.push_back(idx);
        }
      }
      // fan triangulate
      for (size_t i = 1; i + 1 < face.size(); ++i) {
        out.indices.push_back(face[0]);
        out.indices.push_back(face[i]);
        out.indices.push_back(face[i + 1]);
      }
    }
  }
  // Fix missing normals
  if (nrm.empty() && !out.indices.empty()) {
    for (auto& v : out.vertices) v.normal = {0, 0, 0};
    for (size_t i = 0; i + 2 < out.indices.size(); i += 3) {
      auto& a = out.vertices[out.indices[i]];
      auto& b = out.vertices[out.indices[i + 1]];
      auto& c = out.vertices[out.indices[i + 2]];
      glm::vec3 fn = glm::normalize(glm::cross(b.pos - a.pos, c.pos - a.pos));
      a.normal += fn;
      b.normal += fn;
      c.normal += fn;
    }
    for (auto& v : out.vertices) {
      if (glm::length(v.normal) > 1e-6f) v.normal = glm::normalize(v.normal);
      else v.normal = {0, 1, 0};
    }
  }
  logInfo("OBJ loaded " + path + " verts=" + std::to_string(out.vertices.size()) +
          " idx=" + std::to_string(out.indices.size()));
  return !out.vertices.empty() && !out.indices.empty();
}

bool saveObj(const std::string& path, const std::vector<VertexPC>& verts,
             const std::vector<uint32_t>& indices) {
  std::ofstream out(path);
  if (!out) return false;
  out << "# Bolt Engine OBJ\n";
  for (const auto& v : verts)
    out << "v " << v.pos.x << " " << v.pos.y << " " << v.pos.z << "\n";
  for (const auto& v : verts)
    out << "vt " << v.uv.x << " " << v.uv.y << "\n";
  for (const auto& v : verts)
    out << "vn " << v.normal.x << " " << v.normal.y << " " << v.normal.z << "\n";
  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    auto w = [&](uint32_t idx) {
      const int i1 = static_cast<int>(idx) + 1;
      out << i1 << "/" << i1 << "/" << i1;
    };
    out << "f ";
    w(indices[i]);
    out << " ";
    w(indices[i + 1]);
    out << " ";
    w(indices[i + 2]);
    out << "\n";
  }
  return true;
}

} // namespace bolt
