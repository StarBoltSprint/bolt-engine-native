#include "GrokMaterialPipeline.hpp"
#include "ImageOps.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

namespace bolt::tools {

static void log(const std::string& s) { std::fprintf(stderr, "[bolt_grok_import] %s\n", s.c_str()); }

bool runGrokMaterialPipeline(const GrokImportOptions& opt) {
  namespace fs = std::filesystem;
  if (!fs::exists(opt.inputImage)) {
    log("input missing: " + opt.inputImage.string());
    return false;
  }
  fs::create_directories(opt.outDir);

  RgbaImage src;
  if (!loadRgba(opt.inputImage, src)) {
    log("failed to decode image (need PNG/JPG): " + opt.inputImage.string());
    return false;
  }
  log("loaded " + std::to_string(src.w) + "x" + std::to_string(src.h));

  // Albedo RGB (strip alpha for cleaner PBR)
  std::vector<std::uint8_t> albedoRgb(static_cast<size_t>(src.w * src.h * 3));
  for (int i = 0; i < src.w * src.h; ++i) {
    albedoRgb[static_cast<size_t>(i) * 3 + 0] = src.rgba[static_cast<size_t>(i) * 4 + 0];
    albedoRgb[static_cast<size_t>(i) * 3 + 1] = src.rgba[static_cast<size_t>(i) * 4 + 1];
    albedoRgb[static_cast<size_t>(i) * 3 + 2] = src.rgba[static_cast<size_t>(i) * 4 + 2];
  }

  const auto albedoPath = opt.outDir / (opt.name + "_albedo.png");
  const auto normalPath = opt.outDir / (opt.name + "_normal.png");
  const auto roughPath = opt.outDir / (opt.name + "_roughness.png");
  const auto metalPath = opt.outDir / (opt.name + "_metallic.png");
  const auto heightPath = opt.outDir / (opt.name + "_height.png");

  if (!savePngRgb(albedoPath, src.w, src.h, albedoRgb)) {
    log("failed write albedo");
    return false;
  }

  auto height = heightFromLuma(src);
  if (opt.generateHeight && !savePngGrey(heightPath, src.w, src.h, height)) {
    log("failed write height");
    return false;
  }

  if (opt.generateNormalFromLuma) {
    auto normal = normalFromHeightSobel(src.w, src.h, height, 2.8f);
    if (!savePngRgb(normalPath, src.w, src.h, normal)) {
      log("failed write normal");
      return false;
    }
  }

  if (opt.generateRoughness) {
    auto rough = roughnessFromLuma(src);
    if (!savePngGrey(roughPath, src.w, src.h, rough)) {
      log("failed write roughness");
      return false;
    }
  }

  {
    auto metal = metallicFromAlbedo(src);
    if (!savePngGrey(metalPath, src.w, src.h, metal)) {
      log("failed write metallic");
      return false;
    }
  }

  // Manifest for MaterialLibrary
  const auto jsonPath = opt.outDir / (opt.name + ".json");
  std::ostringstream j;
  j << "{\n"
    << "  \"name\": \"" << opt.name << "\",\n"
    << "  \"albedo\": \"" << opt.name << "_albedo.png\",\n"
    << "  \"normal\": \"" << opt.name << "_normal.png\",\n"
    << "  \"roughness\": \"" << opt.name << "_roughness.png\",\n"
    << "  \"metallic\": \"" << opt.name << "_metallic.png\",\n"
    << "  \"height\": \"" << opt.name << "_height.png\",\n"
    << "  \"tiling\": 5.0,\n"
    << "  \"detailTiling\": 18.0,\n"
    << "  \"triplanar\": true,\n"
    << "  \"hotReload\": true,\n"
    << "  \"source\": \"grok_imagine\",\n"
    << "  \"pipeline\": \"luma_height_sobel_normal_v1\"\n"
    << "}\n";
  std::ofstream out(jsonPath);
  out << j.str();

  log("wrote PBR set + " + jsonPath.string());
  log("  albedo / normal / roughness / metallic / height");
  return true;
}

} // namespace bolt::tools
