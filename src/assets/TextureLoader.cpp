#include "bolt/assets/TextureLoader.hpp"
#include "bolt/core/Log.hpp"
#include <algorithm>
#include <cmath>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace bolt {
namespace {

inline float clampf(float v, float a, float b) { return std::max(a, std::min(b, v)); }

inline uint8_t sampleGrey(const ImageData& img, int x, int y) {
  x = std::clamp(x, 0, img.width - 1);
  y = std::clamp(y, 0, img.height - 1);
  const size_t o = static_cast<size_t>(y * img.width + x) * 4;
  // Luma if RGB height, or R
  return img.pixels[o];
}

inline void decodeN(uint8_t r, uint8_t g, uint8_t b, float& nx, float& ny, float& nz) {
  nx = r / 255.f * 2.f - 1.f;
  ny = g / 255.f * 2.f - 1.f;
  nz = b / 255.f * 2.f - 1.f;
  const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
  if (len > 1e-5f) {
    nx /= len;
    ny /= len;
    nz /= len;
  } else {
    nx = 0.f;
    ny = 0.f;
    nz = 1.f;
  }
}

inline void encodeN(float nx, float ny, float nz, uint8_t& r, uint8_t& g, uint8_t& b) {
  const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
  if (len > 1e-5f) {
    nx /= len;
    ny /= len;
    nz /= len;
  }
  r = static_cast<uint8_t>(clampf(nx * 0.5f + 0.5f, 0.f, 1.f) * 255.f + 0.5f);
  g = static_cast<uint8_t>(clampf(ny * 0.5f + 0.5f, 0.f, 1.f) * 255.f + 0.5f);
  b = static_cast<uint8_t>(clampf(nz * 0.5f + 0.5f, 0.f, 1.f) * 255.f + 0.5f);
}

} // namespace

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

void enhanceNormalStrength(ImageData& nrm, float strength) {
  if (nrm.pixels.empty() || nrm.width <= 0 || nrm.height <= 0) return;
  const float s = std::max(0.1f, strength);
  for (int i = 0; i < nrm.width * nrm.height; ++i) {
    const size_t o = static_cast<size_t>(i) * 4;
    float nx, ny, nz;
    decodeN(nrm.pixels[o], nrm.pixels[o + 1], nrm.pixels[o + 2], nx, ny, nz);
    nx *= s;
    ny *= s;
    // keep z positive for hemisphere
    nz = std::max(0.05f, nz);
    encodeN(nx, ny, nz, nrm.pixels[o], nrm.pixels[o + 1], nrm.pixels[o + 2]);
    nrm.pixels[o + 3] = 255;
  }
}

bool sobelNormalFromHeight(const ImageData& height, ImageData& outNormal, float strength) {
  if (height.pixels.empty() || height.width < 3 || height.height < 3) return false;
  const int w = height.width, h = height.height;
  outNormal.width = w;
  outNormal.height = h;
  outNormal.channels = 4;
  outNormal.pixels.assign(static_cast<size_t>(w * h * 4), 255);
  const float s = std::max(0.25f, strength);

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      // Sobel 3x3 on height
      const float tl = sampleGrey(height, x - 1, y - 1) / 255.f;
      const float t = sampleGrey(height, x, y - 1) / 255.f;
      const float tr = sampleGrey(height, x + 1, y - 1) / 255.f;
      const float l = sampleGrey(height, x - 1, y) / 255.f;
      const float r = sampleGrey(height, x + 1, y) / 255.f;
      const float bl = sampleGrey(height, x - 1, y + 1) / 255.f;
      const float b = sampleGrey(height, x, y + 1) / 255.f;
      const float br = sampleGrey(height, x + 1, y + 1) / 255.f;
      const float dx = (tr + 2.f * r + br) - (tl + 2.f * l + bl);
      const float dy = (bl + 2.f * b + br) - (tl + 2.f * t + tr);
      float nx = -dx * s;
      float ny = -dy * s;
      float nz = 1.f;
      const size_t o = static_cast<size_t>(y * w + x) * 4;
      encodeN(nx, ny, nz, outNormal.pixels[o], outNormal.pixels[o + 1], outNormal.pixels[o + 2]);
      outNormal.pixels[o + 3] = 255;
    }
  }
  return true;
}

void blendNormals(ImageData& baseNrm, const ImageData& detailNrm, float detailWeight) {
  if (baseNrm.pixels.empty() || detailNrm.pixels.empty()) return;
  if (baseNrm.width != detailNrm.width || baseNrm.height != detailNrm.height) return;
  const float w = clampf(detailWeight, 0.f, 2.f);
  const int n = baseNrm.width * baseNrm.height;
  for (int i = 0; i < n; ++i) {
    const size_t o = static_cast<size_t>(i) * 4;
    float bx, by, bz, dx, dy, dz;
    decodeN(baseNrm.pixels[o], baseNrm.pixels[o + 1], baseNrm.pixels[o + 2], bx, by, bz);
    decodeN(detailNrm.pixels[o], detailNrm.pixels[o + 1], detailNrm.pixels[o + 2], dx, dy, dz);
    // Whiteout blend
    float nx = bx + dx * w;
    float ny = by + dy * w;
    float nz = bz;
    encodeN(nx, ny, nz, baseNrm.pixels[o], baseNrm.pixels[o + 1], baseNrm.pixels[o + 2]);
    baseNrm.pixels[o + 3] = 255;
  }
}

void enhanceRoughness(ImageData& rough, PbrSurface surface) {
  if (rough.pixels.empty()) return;
  float contrast = 1.45f;
  float bias = 0.f;
  float minR = 0.08f, maxR = 0.95f;
  switch (surface) {
  case PbrSurface::Ground:
    contrast = 1.55f;
    bias = 0.08f; // moss matte
    minR = 0.35f;
    maxR = 0.92f;
    break;
  case PbrSurface::Rock:
    contrast = 1.5f;
    bias = 0.02f;
    minR = 0.28f;
    maxR = 0.88f;
    break;
  case PbrSurface::Path:
    contrast = 1.65f;
    bias = -0.12f; // wet/shiny energy highway
    minR = 0.08f;
    maxR = 0.55f;
    break;
  case PbrSurface::Stalk:
    contrast = 1.6f;
    bias = -0.05f; // crystal glints
    minR = 0.12f;
    maxR = 0.7f;
    break;
  case PbrSurface::Fur:
    contrast = 1.4f;
    bias = 0.05f; // soft matte fur, some sheen in dark
    minR = 0.28f;
    maxR = 0.85f;
    break;
  }
  for (int i = 0; i < rough.width * rough.height; ++i) {
    const size_t o = static_cast<size_t>(i) * 4;
    float r = rough.pixels[o] / 255.f;
    r = clampf((r - 0.5f) * contrast + 0.5f + bias, 0.f, 1.f);
    r = minR + r * (maxR - minR);
    const uint8_t v = static_cast<uint8_t>(clampf(r, 0.f, 1.f) * 255.f + 0.5f);
    rough.pixels[o] = v;
    rough.pixels[o + 1] = v;
    rough.pixels[o + 2] = v;
    rough.pixels[o + 3] = 255;
  }
}

void enhancePbrMaps(ImageData& normal, ImageData& roughness, const ImageData* heightOrNull,
                    PbrSurface surface) {
  // 1) Strengthen existing normals
  float nStr = 1.55f;
  float sobelStr = 2.2f;
  float blendW = 0.55f;
  switch (surface) {
  case PbrSurface::Ground:
    nStr = 1.65f;
    sobelStr = 2.6f;
    blendW = 0.65f;
    break;
  case PbrSurface::Rock:
    nStr = 1.7f;
    sobelStr = 2.8f;
    blendW = 0.7f;
    break;
  case PbrSurface::Path:
    nStr = 1.4f;
    sobelStr = 1.8f;
    blendW = 0.45f;
    break;
  case PbrSurface::Stalk:
    nStr = 1.75f;
    sobelStr = 3.0f;
    blendW = 0.75f;
    break;
  case PbrSurface::Fur:
    nStr = 1.5f;
    sobelStr = 2.4f;
    blendW = 0.6f;
    break;
  }
  enhanceNormalStrength(normal, nStr);

  // 2) Height → detail normals (crystal facets, moss micro)
  if (heightOrNull && !heightOrNull->pixels.empty() &&
      heightOrNull->width == normal.width && heightOrNull->height == normal.height) {
    ImageData sobel;
    if (sobelNormalFromHeight(*heightOrNull, sobel, sobelStr)) {
      blendNormals(normal, sobel, blendW);
      // Second pass: slight extra strength after blend
      enhanceNormalStrength(normal, 1.12f);
    }
  }

  // 3) Roughness contrast + surface bias
  enhanceRoughness(roughness, surface);
}

} // namespace bolt
