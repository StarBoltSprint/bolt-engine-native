#include "GrokMaterialPipeline.hpp"
#include "bolt/core/Log.hpp"
#include <fstream>
#include <nlohmann/json.hpp>

namespace bolt::tools {

bool runGrokMaterialPipeline(const GrokImportOptions& opt) {
  namespace fs = std::filesystem;
  if (!fs::exists(opt.inputImage)) {
    bolt::logError("Grok import: input missing: " + opt.inputImage.string());
    return false;
  }
  fs::create_directories(opt.outDir);

  const auto albedoOut = opt.outDir / (opt.name + "_albedo.png");
  // Copy source as albedo (user should prefer tileable Imagine outputs)
  std::error_code ec;
  fs::copy_file(opt.inputImage, albedoOut, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    bolt::logError("Copy albedo failed: " + ec.message());
    return false;
  }

  // Placeholder sidecar maps — replace with real generators / Substance / custom
  auto touch = [&](const fs::path& p) {
    std::ofstream o(p, std::ios::binary);
    o << "PNG_PLACEHOLDER"; // real tool writes PNG via stb
  };
  if (opt.generateNormalFromLuma) touch(opt.outDir / (opt.name + "_normal.png"));
  if (opt.generateRoughness) touch(opt.outDir / (opt.name + "_roughness.png"));
  if (opt.generateHeight) touch(opt.outDir / (opt.name + "_height.png"));
  touch(opt.outDir / (opt.name + "_metallic.png"));

  nlohmann::json j;
  j["name"] = opt.name;
  j["albedo"] = opt.name + "_albedo.png";
  j["normal"] = opt.name + "_normal.png";
  j["roughness"] = opt.name + "_roughness.png";
  j["metallic"] = opt.name + "_metallic.png";
  j["height"] = opt.name + "_height.png";
  j["tiling"] = 5.0;
  j["detailTiling"] = 18.0;
  j["triplanar"] = true;
  j["hotReload"] = true;
  j["source"] = "grok_imagine";
  j["notes"] = "Use triplanar + height blend in terrain shader for natural integration";

  const auto jsonPath = opt.outDir / (opt.name + ".json");
  std::ofstream out(jsonPath);
  out << j.dump(2);

  bolt::logInfo("Grok pipeline wrote material: " + jsonPath.string());
  return true;
}

} // namespace bolt::tools
