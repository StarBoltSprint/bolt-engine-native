#include "bolt/assets/TextureLoader.hpp"
#include "bolt/core/Log.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace bolt {

bool loadImage(const std::filesystem::path& path, ImageData& out) {
  if (!std::filesystem::exists(path)) {
    logWarn("Texture not found: " + path.string());
    return false;
  }
  int w = 0, h = 0, n = 0;
  stbi_uc* data = stbi_load(path.string().c_str(), &w, &h, &n, 4);
  if (!data) {
    logWarn("stbi_load failed: " + path.string());
    return false;
  }
  out.width = w;
  out.height = h;
  out.channels = 4;
  out.pixels.assign(data, data + static_cast<size_t>(w) * h * 4);
  stbi_image_free(data);
  logInfo("Loaded texture " + path.string() + " " + std::to_string(w) + "x" + std::to_string(h));
  return true;
}

} // namespace bolt
