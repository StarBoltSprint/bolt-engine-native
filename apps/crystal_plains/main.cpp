/**
 * Bolt Engine — Crystal Nebula Plains vertical slice entry.
 *
 * Usage:
 *   bolt_crystal.exe              windowed (F11 / Alt+Enter for fullscreen)
 *   bolt_crystal.exe --fullscreen start exclusive fullscreen
 *   bolt_crystal.exe -f
 */
#include "bolt/app/Application.hpp"
#include "bolt/core/Log.hpp"

#include <cstring>

int main(int argc, char** argv) {
  bolt::logInfo("=== Bolt Engine Native 0.1 — Crystal Nebula Plains ===");
  bolt::logInfo("Sprint shapes the world. Porting from Three.js prototype.");

  bool startFullscreen = false;
  for (int i = 1; i < argc; ++i) {
    if (!argv[i]) continue;
    if (std::strcmp(argv[i], "--fullscreen") == 0 || std::strcmp(argv[i], "-f") == 0 ||
        std::strcmp(argv[i], "/fullscreen") == 0) {
      startFullscreen = true;
    }
  }

  bolt::Application app;
  if (!app.init(startFullscreen)) {
    bolt::logError("Failed to init application");
    return 1;
  }
  app.run();
  app.shutdown();
  return 0;
}
