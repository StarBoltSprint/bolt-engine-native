#include "bolt/render/VulkanContext.hpp"
#include "bolt/core/Log.hpp"

namespace bolt {

bool VulkanContext::initialize(void* glfwWindow) {
  (void)glfwWindow;
#if BOLT_VULKAN
  // TODO: VkInstance, surface from GLFW, device, swapchain (vk-bootstrap)
  logWarn("VulkanContext: stub — link Vulkan SDK and implement device init");
  valid_ = false; // set true when real init succeeds
  return false;
#else
  logWarn("VulkanContext: built without BOLT_VULKAN");
  valid_ = false;
  return false;
#endif
}

void VulkanContext::shutdown() {
  valid_ = false;
}

void VulkanContext::beginFrame() {
  ++frameIndex_;
}

void VulkanContext::endFrame() {
  // TODO: present
}

} // namespace bolt
