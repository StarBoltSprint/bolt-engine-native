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
 * Offline: Grok Imagine PNG → real PBR set + JSON for MaterialLibrary / Vulkan.
 *  albedo (RGB)
 *  height  (greyscale from luma)
 *  normal  (RGB Sobel from height)
 *  roughness / metallic (derived greyscale)
 */
bool runGrokMaterialPipeline(const GrokImportOptions& opt);

} // namespace bolt::tools
