#pragma once
#include <filesystem>
#include <string>

namespace bolt::tools {

struct GrokImportOptions {
  std::filesystem::path inputImage;
  std::filesystem::path outDir;
  std::string name = "material";
  bool generateNormalFromLuma = true;
  bool generateRoughness = true;
  bool generateHeight = true;
};

/**
 * Offline: Grok Imagine PNG → PBR set + JSON manifest for MaterialLibrary.
 * Current scaffold: copies albedo, writes procedural placeholder maps + JSON.
 * Later: real normal-from-height, tiling check, optional external AI upscalers.
 */
bool runGrokMaterialPipeline(const GrokImportOptions& opt);

} // namespace bolt::tools
