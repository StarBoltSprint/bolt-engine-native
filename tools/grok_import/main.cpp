#include "GrokMaterialPipeline.hpp"
#include <cstring>
#include <cstdio>
#include <string>

static void usage() {
  std::fprintf(stderr,
               "bolt_grok_import — Grok Imagine PNG → PBR material set\n"
               "  --in   <image.png|jpg>   source Imagine export (prefer seamless tileable)\n"
               "  --out  <dir>             output folder (e.g. assets/materials/crystal_nebula)\n"
               "  --name <material_name>   base name (default: material)\n"
               "\n"
               "Writes: name_albedo/normal/roughness/metallic/height.png + name.json\n");
}

int main(int argc, char** argv) {
  bolt::tools::GrokImportOptions opt;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--in") == 0 && i + 1 < argc) opt.inputImage = argv[++i];
    else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) opt.outDir = argv[++i];
    else if (std::strcmp(argv[i], "--name") == 0 && i + 1 < argc) opt.name = argv[++i];
    else if (std::strcmp(argv[i], "--help") == 0) {
      usage();
      return 0;
    }
  }
  if (opt.inputImage.empty() || opt.outDir.empty()) {
    usage();
    return 1;
  }
  return bolt::tools::runGrokMaterialPipeline(opt) ? 0 : 2;
}
