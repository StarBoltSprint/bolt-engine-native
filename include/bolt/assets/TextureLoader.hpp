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

/** Material enhancement (normals + roughness) — high visual impact without new polygons. */
enum class PbrSurface : int { Ground, Rock, Path, Stalk, Fur };

/** Boost normal map intensity (channel n = (c/255)*2-1, scale xy, re-encode). */
void enhanceNormalStrength(ImageData& nrm, float strength);

/** Sobel normal from height map (RGBA or grey); strength ~1.5–4. */
bool sobelNormalFromHeight(const ImageData& height, ImageData& outNormal, float strength);

/** Blend two normals (Whiteout-ish): out = normalize(base.xy + detail.xy * w, base.z). */
void blendNormals(ImageData& baseNrm, const ImageData& detailNrm, float detailWeight);

/**
 * Expand roughness contrast + surface bias.
 * dark→smooth/shiny, bright→rough/matte after remap.
 */
void enhanceRoughness(ImageData& rough, PbrSurface surface);

/** Full pipeline: strengthen map, optional height Sobel detail, roughness remap. */
void enhancePbrMaps(ImageData& normal, ImageData& roughness, const ImageData* heightOrNull,
                    PbrSurface surface);

} // namespace bolt
