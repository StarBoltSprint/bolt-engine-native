#pragma once
#include <string>
#include <vector>
#include <functional>
#include "bolt/render/QualityTier.hpp"
#include "bolt/sprint/SprintCore.hpp"

namespace bolt {

/**
 * Lightweight RenderGraph skeleton.
 * Passes record in order; sprint score biases LOD / motion blur / particles.
 */
struct RenderPass {
  std::string name;
  std::function<void()> execute;
};

class RenderGraph {
public:
  void clear();
  void addPass(std::string name, std::function<void()> exec);

  /**
   * Build Crystal frame graph:
   * depth → terrain → foliage → paths → bolt → transparent particles → post
   */
  void buildCrystalFrame(const SprintCore& sprint, const QualitySettings& quality);

  void execute();

private:
  std::vector<RenderPass> passes_;
};

} // namespace bolt
