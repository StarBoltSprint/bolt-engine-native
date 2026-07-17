#pragma once
#include <filesystem>
#include <vector>
#include <cstdint>

namespace bolt {

struct ImageData {
  int width = 0;
  int height = 0;
  int channels = 0;
  std::vector<std::uint8_t> pixels;
};

/** stb_image wrapper (implement with stb in TextureLoader.cpp). */
bool loadImage(const std::filesystem::path& path, ImageData& out);

} // namespace bolt
