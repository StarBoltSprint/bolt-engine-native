#include "ImageOps.hpp"
#include <cmath>
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace bolt::tools {

bool loadRgba(const std::filesystem::path& path, RgbaImage& out) {
  int w = 0, h = 0, n = 0;
  stbi_uc* data = stbi_load(path.string().c_str(), &w, &h, &n, 4);
  if (!data) return false;
  out.w = w;
  out.h = h;
  out.rgba.assign(data, data + static_cast<size_t>(w) * h * 4);
  stbi_image_free(data);
  return true;
}

bool savePngRgb(const std::filesystem::path& path, int w, int h, const std::vector<std::uint8_t>& rgb) {
  return stbi_write_png(path.string().c_str(), w, h, 3, rgb.data(), w * 3) != 0;
}

bool savePngRgba(const std::filesystem::path& path, int w, int h, const std::vector<std::uint8_t>& rgba) {
  return stbi_write_png(path.string().c_str(), w, h, 4, rgba.data(), w * 4) != 0;
}

bool savePngGrey(const std::filesystem::path& path, int w, int h, const std::vector<std::uint8_t>& grey) {
  return stbi_write_png(path.string().c_str(), w, h, 1, grey.data(), w) != 0;
}

static float lumaAt(const RgbaImage& src, int x, int y) {
  x = std::clamp(x, 0, src.w - 1);
  y = std::clamp(y, 0, src.h - 1);
  const size_t i = static_cast<size_t>(y * src.w + x) * 4;
  const float r = src.rgba[i] / 255.f;
  const float g = src.rgba[i + 1] / 255.f;
  const float b = src.rgba[i + 2] / 255.f;
  return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

std::vector<std::uint8_t> heightFromLuma(const RgbaImage& src) {
  std::vector<std::uint8_t> h(static_cast<size_t>(src.w * src.h));
  for (int y = 0; y < src.h; ++y) {
    for (int x = 0; x < src.w; ++x) {
      float L = lumaAt(src, x, y);
      // Mild contrast stretch for terrain displacement / blending
      L = std::clamp((L - 0.15f) / 0.7f, 0.f, 1.f);
      h[static_cast<size_t>(y * src.w + x)] = static_cast<std::uint8_t>(L * 255.f + 0.5f);
    }
  }
  return h;
}

std::vector<std::uint8_t> normalFromHeightSobel(int w, int h, const std::vector<std::uint8_t>& height,
                                                 float strength) {
  std::vector<std::uint8_t> n(static_cast<size_t>(w * h * 3));
  auto H = [&](int x, int y) -> float {
    x = std::clamp(x, 0, w - 1);
    y = std::clamp(y, 0, h - 1);
    return height[static_cast<size_t>(y * w + x)] / 255.f;
  };
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      // Sobel
      const float dX =
          -H(x - 1, y - 1) - 2.f * H(x - 1, y) - H(x - 1, y + 1) +
           H(x + 1, y - 1) + 2.f * H(x + 1, y) + H(x + 1, y + 1);
      const float dY =
          -H(x - 1, y - 1) - 2.f * H(x, y - 1) - H(x + 1, y - 1) +
           H(x - 1, y + 1) + 2.f * H(x, y + 1) + H(x + 1, y + 1);
      float nx = -dX * strength;
      float ny = -dY * strength;
      float nz = 1.f;
      const float len = std::sqrt(nx * nx + ny * ny + nz * nz) + 1e-6f;
      nx /= len;
      ny /= len;
      nz /= len;
      const size_t i = static_cast<size_t>(y * w + x) * 3;
      n[i + 0] = static_cast<std::uint8_t>((nx * 0.5f + 0.5f) * 255.f);
      n[i + 1] = static_cast<std::uint8_t>((ny * 0.5f + 0.5f) * 255.f);
      n[i + 2] = static_cast<std::uint8_t>((nz * 0.5f + 0.5f) * 255.f);
    }
  }
  return n;
}

std::vector<std::uint8_t> roughnessFromLuma(const RgbaImage& src) {
  std::vector<std::uint8_t> r(static_cast<size_t>(src.w * src.h));
  for (int y = 0; y < src.h; ++y) {
    for (int x = 0; x < src.w; ++x) {
      float L = lumaAt(src, x, y);
      // Brighter crystals → smoother (lower roughness)
      float rough = std::clamp(1.05f - L * 0.85f, 0.15f, 0.95f);
      // Micro variation
      const float d = std::abs(lumaAt(src, x + 1, y) - lumaAt(src, x - 1, y)) +
                      std::abs(lumaAt(src, x, y + 1) - lumaAt(src, x, y - 1));
      rough = std::clamp(rough + d * 0.35f, 0.12f, 0.98f);
      r[static_cast<size_t>(y * src.w + x)] = static_cast<std::uint8_t>(rough * 255.f);
    }
  }
  return r;
}

std::vector<std::uint8_t> metallicFromAlbedo(const RgbaImage& src) {
  std::vector<std::uint8_t> m(static_cast<size_t>(src.w * src.h));
  for (int y = 0; y < src.h; ++y) {
    for (int x = 0; x < src.w; ++x) {
      const size_t i = static_cast<size_t>(y * src.w + x) * 4;
      const float r = src.rgba[i] / 255.f;
      const float g = src.rgba[i + 1] / 255.f;
      const float b = src.rgba[i + 2] / 255.f;
      const float L = 0.2126f * r + 0.7152f * g + 0.0722f * b;
      // Cool bright → more metal (crystal sheen); dark organic → dielectric
      const float cool = std::max(0.f, b - r * 0.5f);
      float metal = std::clamp(L * 0.35f + cool * 0.55f, 0.02f, 0.75f);
      m[static_cast<size_t>(y * src.w + x)] = static_cast<std::uint8_t>(metal * 255.f);
    }
  }
  return m;
}

} // namespace bolt::tools
