#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>

namespace bolt::tools {

struct RgbaImage {
  int w = 0;
  int h = 0;
  std::vector<std::uint8_t> rgba; // w*h*4
};

bool loadRgba(const std::filesystem::path& path, RgbaImage& out);
bool savePngRgb(const std::filesystem::path& path, int w, int h, const std::vector<std::uint8_t>& rgb);
bool savePngRgba(const std::filesystem::path& path, int w, int h, const std::vector<std::uint8_t>& rgba);
bool savePngGrey(const std::filesystem::path& path, int w, int h, const std::vector<std::uint8_t>& grey);

/** Luma height 0..255 from albedo. */
std::vector<std::uint8_t> heightFromLuma(const RgbaImage& src);

/** Sobel normal map RGB from height greyscale. */
std::vector<std::uint8_t> normalFromHeightSobel(int w, int h, const std::vector<std::uint8_t>& height,
                                                 float strength = 2.5f);

/** Roughness greyscale: inverted contrast of luma + detail. */
std::vector<std::uint8_t> roughnessFromLuma(const RgbaImage& src);

/** Metallic greyscale: boost cooler/brighter crystal-like pixels. */
std::vector<std::uint8_t> metallicFromAlbedo(const RgbaImage& src);

} // namespace bolt::tools
