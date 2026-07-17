#pragma once
#include <cstdint>
#include <string>

namespace bolt {

/**
 * Vulkan device bootstrap stub.
 * Replace with vk-bootstrap / VMA production path when SDK is linked.
 */
class VulkanContext {
public:
  bool initialize(void* glfwWindow);
  void shutdown();
  bool isValid() const { return valid_; }

  void beginFrame();
  void endFrame();

  std::uint32_t frameIndex() const { return frameIndex_; }

private:
  bool valid_ = false;
  std::uint32_t frameIndex_ = 0;
};

} // namespace bolt
