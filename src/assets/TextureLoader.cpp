#include "bolt/assets/TextureLoader.hpp"
#include "bolt/core/Log.hpp"

// Optional: #define STB_IMAGE_IMPLEMENTATION in one TU and include stb_image.h
// For scaffold, fail gracefully if file missing.

namespace bolt {

bool loadImage(const std::filesystem::path& path, ImageData& out) {
  if (!std::filesystem::exists(path)) {
    logWarn("Texture not found: " + path.string());
    return false;
  }
  // TODO: stb_image_load
  // Placeholder 1x1 so pipeline compiles
  out.width = 1;
  out.height = 1;
  out.channels = 4;
  out.pixels = {200, 230, 255, 255};
  logInfo("TextureLoader stub loaded: " + path.string());
  return true;
}

} // namespace bolt
