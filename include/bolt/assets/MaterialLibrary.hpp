#pragma once
#include <string>
#include <unordered_map>
#include <filesystem>
#include <optional>
#include <cstdint>
#include "bolt/ecs/Components.hpp"

namespace bolt {

struct MaterialDesc {
  std::string name;
  std::string albedoPath;
  std::string normalPath;
  std::string roughnessPath;
  std::string metallicPath;
  std::string heightPath;
  std::string emissivePath;
  float tiling = 5.f;
  float detailTiling = 18.f;
  bool triplanar = true;
  bool hotReload = true;
};

/**
 * Loads material manifests (JSON) + textures; supports hot-reload.
 * GPU upload is Render layer responsibility — this owns CPU paths + ids.
 */
class MaterialLibrary {
public:
  void setRoot(const std::filesystem::path& assetsRoot);

  /** Load or reload assets/materials/**\/ *.json */
  bool scanAndLoad();

  MaterialId loadManifest(const std::filesystem::path& jsonPath);
  const MaterialDesc* get(MaterialId id) const;
  MaterialId findByName(const std::string& name) const;

  /** Poll mtimes; returns true if any material reloaded. */
  bool pollHotReload();

  std::uint32_t count() const { return static_cast<std::uint32_t>(byId_.size()); }

private:
  std::filesystem::path root_;
  std::unordered_map<MaterialId, MaterialDesc> byId_;
  std::unordered_map<std::string, MaterialId> byName_;
  std::unordered_map<std::string, std::filesystem::file_time_type> mtimes_;
  MaterialId nextId_ = 1;
};

} // namespace bolt
