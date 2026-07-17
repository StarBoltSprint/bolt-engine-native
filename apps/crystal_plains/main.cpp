/**
 * Bolt Engine — Crystal Nebula Plains vertical slice entry.
 */
#include "bolt/app/Application.hpp"
#include "bolt/core/Log.hpp"

int main() {
  bolt::logInfo("=== Bolt Engine Native 0.1 — Crystal Nebula Plains ===");
  bolt::logInfo("Sprint shapes the world. Porting from Three.js prototype.");

  bolt::Application app;
  if (!app.init()) {
    bolt::logError("Failed to init application");
    return 1;
  }
  app.run();
  app.shutdown();
  return 0;
}
