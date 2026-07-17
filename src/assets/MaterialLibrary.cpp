#include "bolt/assets/MaterialLibrary.hpp"
#include "bolt/core/Log.hpp"
#include <fstream>
#include <nlohmann/json.hpp>

namespace bolt {

void MaterialLibrary::setRoot(const std::filesystem::path& assetsRoot) {
  root_ = assetsRoot;
}

bool MaterialLibrary::scanAndLoad() {
  const auto matRoot = root_ / "materials";
  if (!std::filesystem::exists(matRoot)) {
    logWarn("MaterialLibrary: materials/ missing under assets");
    return false;
  }
  int loaded = 0;
  for (auto& entry : std::filesystem::recursive_directory_iterator(matRoot)) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() == ".json") {
      if (loadManifest(entry.path()) != kInvalidMaterial) ++loaded;
    }
  }
  logInfo("MaterialLibrary: loaded " + std::to_string(loaded) + " materials");
  return loaded > 0;
}

MaterialId MaterialLibrary::loadManifest(const std::filesystem::path& jsonPath) {
  try {
    std::ifstream in(jsonPath);
    if (!in) return kInvalidMaterial;
    nlohmann::json j;
    in >> j;

    MaterialDesc d;
    d.name = j.value("name", jsonPath.stem().string());
    const auto dir = jsonPath.parent_path();
    auto resolve = [&](const char* key) -> std::string {
      if (!j.contains(key)) return {};
      auto p = dir / j[key].get<std::string>();
      return p.string();
    };
    d.albedoPath = resolve("albedo");
    d.normalPath = resolve("normal");
    d.roughnessPath = resolve("roughness");
    d.metallicPath = resolve("metallic");
    d.heightPath = resolve("height");
    d.emissivePath = resolve("emissive");
    d.tiling = j.value("tiling", 5.f);
    d.detailTiling = j.value("detailTiling", 18.f);
    d.triplanar = j.value("triplanar", true);
    d.hotReload = j.value("hotReload", true);

    // Replace if same name
    if (auto it = byName_.find(d.name); it != byName_.end()) {
      byId_[it->second] = d;
      mtimes_[jsonPath.string()] = std::filesystem::last_write_time(jsonPath);
      return it->second;
    }

    const MaterialId id = nextId_++;
    byId_[id] = d;
    byName_[d.name] = id;
    if (std::filesystem::exists(jsonPath)) {
      mtimes_[jsonPath.string()] = std::filesystem::last_write_time(jsonPath);
    }
    return id;
  } catch (const std::exception& ex) {
    logError(std::string("MaterialLibrary load failed: ") + ex.what());
    return kInvalidMaterial;
  }
}

const MaterialDesc* MaterialLibrary::get(MaterialId id) const {
  auto it = byId_.find(id);
  return it == byId_.end() ? nullptr : &it->second;
}

MaterialId MaterialLibrary::findByName(const std::string& name) const {
  auto it = byName_.find(name);
  return it == byName_.end() ? kInvalidMaterial : it->second;
}

bool MaterialLibrary::pollHotReload() {
  bool any = false;
  for (auto& [path, mt] : mtimes_) {
    std::error_code ec;
    auto now = std::filesystem::last_write_time(path, ec);
    if (ec) continue;
    if (now != mt) {
      mt = now;
      loadManifest(path);
      any = true;
      logInfo("Hot-reload material: " + path);
    }
  }
  return any;
}

} // namespace bolt
