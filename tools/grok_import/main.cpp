#include "GrokMaterialPipeline.hpp"
#include "bolt/core/Log.hpp"
#include <cstring>
#include <string>

static void usage() {
  bolt::logInfo(
      "Usage: bolt_grok_import --in <image.png> --out <dir> --name <material_name>");
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
