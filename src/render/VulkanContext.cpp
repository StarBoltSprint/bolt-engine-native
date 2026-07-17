#include "bolt/render/VulkanContext.hpp"
#include "bolt/assets/TextureLoader.hpp"
#include "bolt/core/Log.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <set>
#include <stdexcept>

namespace bolt {
namespace {

const std::vector<const char*> kDeviceExt = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

std::vector<char> readFile(const std::string& path) {
  std::ifstream f(path, std::ios::ate | std::ios::binary);
  if (!f) return {};
  const size_t sz = static_cast<size_t>(f.tellg());
  std::vector<char> buf(sz);
  f.seekg(0);
  f.read(buf.data(), static_cast<std::streamsize>(sz));
  return buf;
}

} // namespace

bool VulkanContext::initialize(GLFWwindow* window) {
  window_ = window;
  glfwGetFramebufferSize(window_, &width_, &height_);
  if (!createInstance()) return false;
  if (!createSurface(window)) return false;
  if (!pickPhysicalDevice()) return false;
  if (!createDevice()) return false;
  if (!createSwapchain()) return false;
  if (!createRenderPass()) return false;
  if (!createDescriptorSetLayout()) return false;
  if (!createPipelines()) {
    logWarn("Pipelines failed (missing SPIR-V?). Terrain/foliage draw disabled until shaders compile.");
  }
  if (!createFramebuffers()) return false;
  if (!createCommandPool()) return false;
  if (!createUniformBuffers()) return false;
  if (!createDescriptorPoolAndSets()) return false;
  if (!createSync()) return false;
  valid_ = true;
  logInfo("VulkanContext: device + swapchain ready");
  return true;
}

void VulkanContext::shutdown() {
  if (device_ != VK_NULL_HANDLE) vkDeviceWaitIdle(device_);
  cleanupSwapchain();
  destroyBuffer(terrain_.vertex);
  destroyBuffer(terrain_.index);
  destroyBuffer(stalk_.vertex);
  destroyBuffer(stalk_.index);
  destroyBuffer(foliageInstanceBuf_);
  destroyMaterialOwned(groundMat_);
  destroyMaterialOwned(rockMat_);
  destroyMaterialOwned(pathMat_);
  destroyMaterialOwned(stalkMat_);
  destroyTexture(defaultAlbedo_);
  destroyTexture(defaultNormal_);
  destroyTexture(defaultRough_);
  for (auto& u : uniformBuffers_) destroyBuffer(u);
  uniformMapped_.clear();
  if (descPool_) vkDestroyDescriptorPool(device_, descPool_, nullptr);
  if (descLayout_) vkDestroyDescriptorSetLayout(device_, descLayout_, nullptr);
  if (skyPipeline_) vkDestroyPipeline(device_, skyPipeline_, nullptr);
  if (terrainPipeline_) vkDestroyPipeline(device_, terrainPipeline_, nullptr);
  if (foliagePipeline_) vkDestroyPipeline(device_, foliagePipeline_, nullptr);
  if (pipelineLayout_) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
  if (renderPass_) vkDestroyRenderPass(device_, renderPass_, nullptr);
  for (int i = 0; i < kMaxFrames; ++i) {
    if (imageAvailable_[i]) vkDestroySemaphore(device_, imageAvailable_[i], nullptr);
    if (renderFinished_[i]) vkDestroySemaphore(device_, renderFinished_[i], nullptr);
    if (inFlight_[i]) vkDestroyFence(device_, inFlight_[i], nullptr);
  }
  if (cmdPool_) vkDestroyCommandPool(device_, cmdPool_, nullptr);
  if (device_) vkDestroyDevice(device_, nullptr);
  if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
  if (instance_) vkDestroyInstance(instance_, nullptr);
  valid_ = false;
}

bool VulkanContext::createInstance() {
  if (!glfwVulkanSupported()) {
    logError("GLFW reports no Vulkan support");
    return false;
  }
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "BoltEngine";
  app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  app.pEngineName = "BoltEngine";
  app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
  app.apiVersion = VK_API_VERSION_1_1;

  uint32_t extCount = 0;
  const char** glfwExt = glfwGetRequiredInstanceExtensions(&extCount);

  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ci.pApplicationInfo = &app;
  ci.enabledExtensionCount = extCount;
  ci.ppEnabledExtensionNames = glfwExt;

  if (vkCreateInstance(&ci, nullptr, &instance_) != VK_SUCCESS) {
    logError("vkCreateInstance failed");
    return false;
  }
  return true;
}

bool VulkanContext::createSurface(GLFWwindow* window) {
  if (glfwCreateWindowSurface(instance_, window, nullptr, &surface_) != VK_SUCCESS) {
    logError("glfwCreateWindowSurface failed");
    return false;
  }
  return true;
}

bool VulkanContext::pickPhysicalDevice() {
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(instance_, &count, nullptr);
  if (count == 0) {
    logError("No Vulkan GPUs");
    return false;
  }
  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(instance_, &count, devices.data());

  for (auto dev : devices) {
    uint32_t qCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, nullptr);
    std::vector<VkQueueFamilyProperties> qprops(qCount);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, qprops.data());

    std::optional<uint32_t> graphics, present;
    for (uint32_t i = 0; i < qCount; ++i) {
      if (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) graphics = i;
      VkBool32 presentSupport = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface_, &presentSupport);
      if (presentSupport) present = i;
    }
    if (!graphics || !present) continue;

    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, exts.data());
    bool hasSwap = false;
    for (auto& e : exts) {
      if (std::string(e.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME) hasSwap = true;
    }
    if (!hasSwap) continue;

    physical_ = dev;
    graphicsFamily_ = *graphics;
    presentFamily_ = *present;
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(dev, &props);
    logInfo(std::string("GPU: ") + props.deviceName);
    return true;
  }
  logError("No suitable GPU with swapchain");
  return false;
}

bool VulkanContext::createDevice() {
  std::set<uint32_t> unique = {graphicsFamily_, presentFamily_};
  std::vector<VkDeviceQueueCreateInfo> qcis;
  float prio = 1.f;
  for (uint32_t fam : unique) {
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex = fam;
    q.queueCount = 1;
    q.pQueuePriorities = &prio;
    qcis.push_back(q);
  }
  VkPhysicalDeviceFeatures feats{};
  VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  ci.queueCreateInfoCount = static_cast<uint32_t>(qcis.size());
  ci.pQueueCreateInfos = qcis.data();
  ci.pEnabledFeatures = &feats;
  ci.enabledExtensionCount = static_cast<uint32_t>(kDeviceExt.size());
  ci.ppEnabledExtensionNames = kDeviceExt.data();
  if (vkCreateDevice(physical_, &ci, nullptr, &device_) != VK_SUCCESS) {
    logError("vkCreateDevice failed");
    return false;
  }
  vkGetDeviceQueue(device_, graphicsFamily_, 0, &graphicsQueue_);
  vkGetDeviceQueue(device_, presentFamily_, 0, &presentQueue_);
  return true;
}

bool VulkanContext::createSwapchain() {
  VkSurfaceCapabilitiesKHR caps{};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps);
  uint32_t formatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &formatCount, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &formatCount, formats.data());
  VkSurfaceFormatKHR chosen = formats[0];
  for (auto& f : formats) {
    if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      chosen = f;
      break;
    }
  }
  swapFormat_ = chosen.format;

  uint32_t presentCount = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &presentCount, nullptr);
  std::vector<VkPresentModeKHR> presents(presentCount);
  vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &presentCount, presents.data());
  VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
  for (auto m : presents) {
    if (m == VK_PRESENT_MODE_MAILBOX_KHR) presentMode = m;
  }

  if (caps.currentExtent.width != UINT32_MAX) {
    swapExtent_ = caps.currentExtent;
  } else {
    swapExtent_.width = std::clamp(static_cast<uint32_t>(width_), caps.minImageExtent.width,
                                   caps.maxImageExtent.width);
    swapExtent_.height = std::clamp(static_cast<uint32_t>(height_), caps.minImageExtent.height,
                                    caps.maxImageExtent.height);
  }

  uint32_t imageCount = caps.minImageCount + 1;
  if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

  VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  ci.surface = surface_;
  ci.minImageCount = imageCount;
  ci.imageFormat = swapFormat_;
  ci.imageColorSpace = chosen.colorSpace;
  ci.imageExtent = swapExtent_;
  ci.imageArrayLayers = 1;
  ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  uint32_t qidx[] = {graphicsFamily_, presentFamily_};
  if (graphicsFamily_ != presentFamily_) {
    ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    ci.queueFamilyIndexCount = 2;
    ci.pQueueFamilyIndices = qidx;
  } else {
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }
  ci.preTransform = caps.currentTransform;
  ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  ci.presentMode = presentMode;
  ci.clipped = VK_TRUE;

  if (vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_) != VK_SUCCESS) {
    logError("vkCreateSwapchainKHR failed");
    return false;
  }
  vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr);
  swapImages_.resize(imageCount);
  vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, swapImages_.data());

  swapViews_.resize(imageCount);
  for (size_t i = 0; i < swapImages_.size(); ++i) {
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = swapImages_[i];
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = swapFormat_;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &vi, nullptr, &swapViews_[i]) != VK_SUCCESS) return false;
  }
  return true;
}

void VulkanContext::destroyDepthResources() {
  if (depthView_) vkDestroyImageView(device_, depthView_, nullptr);
  if (depthImage_) vkDestroyImage(device_, depthImage_, nullptr);
  if (depthMemory_) vkFreeMemory(device_, depthMemory_, nullptr);
  depthView_ = VK_NULL_HANDLE;
  depthImage_ = VK_NULL_HANDLE;
  depthMemory_ = VK_NULL_HANDLE;
}

bool VulkanContext::createDepthResources() {
  destroyDepthResources();
  VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ii.imageType = VK_IMAGE_TYPE_2D;
  ii.extent = {swapExtent_.width, swapExtent_.height, 1};
  ii.mipLevels = 1;
  ii.arrayLayers = 1;
  ii.format = depthFormat_;
  ii.tiling = VK_IMAGE_TILING_OPTIMAL;
  ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  ii.samples = VK_SAMPLE_COUNT_1_BIT;
  if (vkCreateImage(device_, &ii, nullptr, &depthImage_) != VK_SUCCESS) return false;
  VkMemoryRequirements req{};
  vkGetImageMemoryRequirements(device_, depthImage_, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (vkAllocateMemory(device_, &ai, nullptr, &depthMemory_) != VK_SUCCESS) return false;
  vkBindImageMemory(device_, depthImage_, depthMemory_, 0);

  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = depthImage_;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vi.format = depthFormat_;
  vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  vi.subresourceRange.levelCount = 1;
  vi.subresourceRange.layerCount = 1;
  return vkCreateImageView(device_, &vi, nullptr, &depthView_) == VK_SUCCESS;
}

void VulkanContext::cleanupSwapchain() {
  for (auto fb : framebuffers_) vkDestroyFramebuffer(device_, fb, nullptr);
  framebuffers_.clear();
  destroyDepthResources();
  for (auto v : swapViews_) vkDestroyImageView(device_, v, nullptr);
  swapViews_.clear();
  if (swapchain_) {
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
  }
}

bool VulkanContext::createRenderPass() {
  VkAttachmentDescription color{};
  color.format = swapFormat_;
  color.samples = VK_SAMPLE_COUNT_1_BIT;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentDescription depth{};
  depth.format = depthFormat_;
  depth.samples = VK_SAMPLE_COUNT_1_BIT;
  depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1;
  sub.pColorAttachments = &colorRef;
  sub.pDepthStencilAttachment = &depthRef;

  VkSubpassDependency dep{};
  dep.srcSubpass = VK_SUBPASS_EXTERNAL;
  dep.dstSubpass = 0;
  dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  std::array<VkAttachmentDescription, 2> atts = {color, depth};
  VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  ci.attachmentCount = static_cast<uint32_t>(atts.size());
  ci.pAttachments = atts.data();
  ci.subpassCount = 1;
  ci.pSubpasses = &sub;
  ci.dependencyCount = 1;
  ci.pDependencies = &dep;
  return vkCreateRenderPass(device_, &ci, nullptr, &renderPass_) == VK_SUCCESS;
}

bool VulkanContext::createFramebuffers() {
  if (!createDepthResources()) return false;
  framebuffers_.resize(swapViews_.size());
  for (size_t i = 0; i < swapViews_.size(); ++i) {
    VkImageView atts[] = {swapViews_[i], depthView_};
    VkFramebufferCreateInfo ci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    ci.renderPass = renderPass_;
    ci.attachmentCount = 2;
    ci.pAttachments = atts;
    ci.width = swapExtent_.width;
    ci.height = swapExtent_.height;
    ci.layers = 1;
    if (vkCreateFramebuffer(device_, &ci, nullptr, &framebuffers_[i]) != VK_SUCCESS) return false;
  }
  return true;
}

bool VulkanContext::createCommandPool() {
  VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  ci.queueFamilyIndex = graphicsFamily_;
  if (vkCreateCommandPool(device_, &ci, nullptr, &cmdPool_) != VK_SUCCESS) return false;
  cmdBuffers_.resize(kMaxFrames);
  VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ai.commandPool = cmdPool_;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = kMaxFrames;
  return vkAllocateCommandBuffers(device_, &ai, cmdBuffers_.data()) == VK_SUCCESS;
}

bool VulkanContext::createSync() {
  VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  for (int i = 0; i < kMaxFrames; ++i) {
    if (vkCreateSemaphore(device_, &si, nullptr, &imageAvailable_[i]) != VK_SUCCESS) return false;
    if (vkCreateSemaphore(device_, &si, nullptr, &renderFinished_[i]) != VK_SUCCESS) return false;
    if (vkCreateFence(device_, &fi, nullptr, &inFlight_[i]) != VK_SUCCESS) return false;
  }
  return true;
}

bool VulkanContext::createDescriptorSetLayout() {
  VkDescriptorSetLayoutBinding ubo{};
  ubo.binding = 0;
  ubo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  ubo.descriptorCount = 1;
  ubo.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutBinding inst{};
  inst.binding = 1;
  inst.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  inst.descriptorCount = 1;
  inst.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  // 4 material layers × 3 maps: ground(2-4), rock(5-7), path(8-10), stalk(11-13)
  std::array<VkDescriptorSetLayoutBinding, 14> binds{};
  binds[0] = ubo;
  binds[1] = inst;
  for (uint32_t b = 2; b <= 13; ++b) {
    binds[b] = {};
    binds[b].binding = b;
    binds[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[b].descriptorCount = 1;
    binds[b].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  }

  VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  ci.bindingCount = static_cast<uint32_t>(binds.size());
  ci.pBindings = binds.data();
  return vkCreateDescriptorSetLayout(device_, &ci, nullptr, &descLayout_) == VK_SUCCESS;
}

VkShaderModule VulkanContext::loadShaderModule(const std::string& path) const {
  auto code = readFile(path);
  if (code.empty()) {
    logWarn("Shader missing: " + path);
    return VK_NULL_HANDLE;
  }
  VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  ci.codeSize = code.size();
  ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
  VkShaderModule mod = VK_NULL_HANDLE;
  if (vkCreateShaderModule(device_, &ci, nullptr, &mod) != VK_SUCCESS) return VK_NULL_HANDLE;
  return mod;
}

bool VulkanContext::createPipelines() {
  // Prefer SPIR-V next to assets; compile with scripts/compile_shaders.ps1
  VkShaderModule vert = loadShaderModule("assets/shaders/terrain.vert.spv");
  VkShaderModule frag = loadShaderModule("assets/shaders/terrain.frag.spv");
  VkShaderModule fvert = loadShaderModule("assets/shaders/foliage.vert.spv");
  VkShaderModule ffrag = loadShaderModule("assets/shaders/foliage.frag.spv");
  VkShaderModule svert = loadShaderModule("assets/shaders/sky.vert.spv");
  VkShaderModule sfrag = loadShaderModule("assets/shaders/sky.frag.spv");
  if (!vert || !frag) {
    if (vert) vkDestroyShaderModule(device_, vert, nullptr);
    if (frag) vkDestroyShaderModule(device_, frag, nullptr);
    if (fvert) vkDestroyShaderModule(device_, fvert, nullptr);
    if (ffrag) vkDestroyShaderModule(device_, ffrag, nullptr);
    if (svert) vkDestroyShaderModule(device_, svert, nullptr);
    if (sfrag) vkDestroyShaderModule(device_, sfrag, nullptr);
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &descLayout_;
    return vkCreatePipelineLayout(device_, &plci, nullptr, &pipelineLayout_) == VK_SUCCESS;
  }

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag;
  stages[1].pName = "main";

  VkVertexInputBindingDescription bind{};
  bind.binding = 0;
  bind.stride = sizeof(VertexPC);
  bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  std::array<VkVertexInputAttributeDescription, 3> attrs{};
  attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexPC, pos)};
  attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexPC, normal)};
  attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VertexPC, uv)};

  VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vi.vertexBindingDescriptionCount = 1;
  vi.pVertexBindingDescriptions = &bind;
  vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
  vi.pVertexAttributeDescriptions = attrs.data();

  VkPipelineVertexInputStateCreateInfo viEmpty{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

  VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo vs{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vs.viewportCount = 1;
  vs.scissorCount = 1;

  std::array<VkDynamicState, 2> dynStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dyn.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
  dyn.pDynamicStates = dynStates.data();

  VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_BACK_BIT;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.f;

  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  ds.depthTestEnable = VK_TRUE;
  ds.depthWriteEnable = VK_TRUE;
  ds.depthCompareOp = VK_COMPARE_OP_LESS;

  VkPipelineDepthStencilStateCreateInfo dsSky{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  dsSky.depthTestEnable = VK_FALSE;
  dsSky.depthWriteEnable = VK_FALSE;

  VkPipelineColorBlendAttachmentState blendAtt{};
  blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cb.attachmentCount = 1;
  cb.pAttachments = &blendAtt;

  VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  plci.setLayoutCount = 1;
  plci.pSetLayouts = &descLayout_;
  if (vkCreatePipelineLayout(device_, &plci, nullptr, &pipelineLayout_) != VK_SUCCESS) return false;

  VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  gp.stageCount = 2;
  gp.pStages = stages;
  gp.pVertexInputState = &vi;
  gp.pInputAssemblyState = &ia;
  gp.pViewportState = &vs;
  gp.pRasterizationState = &rs;
  gp.pMultisampleState = &ms;
  gp.pDepthStencilState = &ds;
  gp.pColorBlendState = &cb;
  gp.pDynamicState = &dyn;
  gp.layout = pipelineLayout_;
  gp.renderPass = renderPass_;
  gp.subpass = 0;
  if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &terrainPipeline_) !=
      VK_SUCCESS) {
    logError("terrain pipeline failed");
  }

  if (fvert && ffrag) {
    stages[0].module = fvert;
    stages[1].module = ffrag;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &foliagePipeline_) !=
        VK_SUCCESS) {
      logWarn("foliage pipeline failed");
    }
  }

  // Sky: fullscreen triangle, no depth, no vertex buffer
  if (svert && sfrag) {
    stages[0].module = svert;
    stages[1].module = sfrag;
    gp.pVertexInputState = &viEmpty;
    gp.pDepthStencilState = &dsSky;
    rs.cullMode = VK_CULL_MODE_NONE;
    gp.pRasterizationState = &rs;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &skyPipeline_) !=
        VK_SUCCESS) {
      logWarn("sky pipeline failed");
    }
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
  }

  vkDestroyShaderModule(device_, vert, nullptr);
  vkDestroyShaderModule(device_, frag, nullptr);
  if (fvert) vkDestroyShaderModule(device_, fvert, nullptr);
  if (ffrag) vkDestroyShaderModule(device_, ffrag, nullptr);
  if (svert) vkDestroyShaderModule(device_, svert, nullptr);
  if (sfrag) vkDestroyShaderModule(device_, sfrag, nullptr);
  return terrainPipeline_ != VK_NULL_HANDLE;
}

uint32_t VulkanContext::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const {
  VkPhysicalDeviceMemoryProperties mem{};
  vkGetPhysicalDeviceMemoryProperties(physical_, &mem);
  for (uint32_t i = 0; i < mem.memoryTypeCount; ++i) {
    if ((typeBits & (1u << i)) && (mem.memoryTypes[i].propertyFlags & props) == props) return i;
  }
  return 0;
}

bool VulkanContext::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                 VkMemoryPropertyFlags props, GpuBuffer& out) {
  VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bi.size = size;
  bi.usage = usage;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(device_, &bi, nullptr, &out.buffer) != VK_SUCCESS) return false;
  VkMemoryRequirements req{};
  vkGetBufferMemoryRequirements(device_, out.buffer, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, props);
  if (vkAllocateMemory(device_, &ai, nullptr, &out.memory) != VK_SUCCESS) return false;
  vkBindBufferMemory(device_, out.buffer, out.memory, 0);
  out.size = size;
  return true;
}

void VulkanContext::destroyBuffer(GpuBuffer& b) {
  if (b.buffer) vkDestroyBuffer(device_, b.buffer, nullptr);
  if (b.memory) vkFreeMemory(device_, b.memory, nullptr);
  b = {};
}

bool VulkanContext::copyToBuffer(GpuBuffer& dst, const void* data, VkDeviceSize size) {
  void* mapped = nullptr;
  if (vkMapMemory(device_, dst.memory, 0, size, 0, &mapped) != VK_SUCCESS) return false;
  std::memcpy(mapped, data, static_cast<size_t>(size));
  vkUnmapMemory(device_, dst.memory);
  return true;
}

bool VulkanContext::createUniformBuffers() {
  uniformBuffers_.resize(kMaxFrames);
  uniformMapped_.resize(kMaxFrames);
  for (int i = 0; i < kMaxFrames; ++i) {
    if (!createBuffer(sizeof(FrameUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      uniformBuffers_[i]))
      return false;
    vkMapMemory(device_, uniformBuffers_[i].memory, 0, sizeof(FrameUBO), 0, &uniformMapped_[i]);
  }
  // empty foliage buffer
  createBuffer(sizeof(FoliageInstanceGPU) * 64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               foliageInstanceBuf_);
  foliageCapacity_ = 64;
  return true;
}

bool VulkanContext::createDescriptorPoolAndSets() {
  std::array<VkDescriptorPoolSize, 3> sizes{};
  sizes[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxFrames};
  sizes[1] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kMaxFrames};
  // 4 materials × 3 maps per frame
  sizes[2] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxFrames * 12};
  VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pci.poolSizeCount = static_cast<uint32_t>(sizes.size());
  pci.pPoolSizes = sizes.data();
  pci.maxSets = kMaxFrames;
  if (vkCreateDescriptorPool(device_, &pci, nullptr, &descPool_) != VK_SUCCESS) return false;

  std::vector<VkDescriptorSetLayout> layouts(kMaxFrames, descLayout_);
  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool = descPool_;
  ai.descriptorSetCount = kMaxFrames;
  ai.pSetLayouts = layouts.data();
  descSets_.resize(kMaxFrames);
  if (vkAllocateDescriptorSets(device_, &ai, descSets_.data()) != VK_SUCCESS) return false;

  if (!createDefault1x1Textures()) return false;
  updateMaterialDescriptors();
  return true;
}

void VulkanContext::destroyTexture(GpuTexture& t) {
  if (t.sampler) vkDestroySampler(device_, t.sampler, nullptr);
  if (t.view) vkDestroyImageView(device_, t.view, nullptr);
  if (t.image) vkDestroyImage(device_, t.image, nullptr);
  if (t.memory) vkFreeMemory(device_, t.memory, nullptr);
  t = {};
}

bool VulkanContext::createDefault1x1Textures() {
  // Teal albedo, flat normal, mid roughness
  std::vector<uint8_t> alb = {40, 120, 150, 255};
  std::vector<uint8_t> nrm = {128, 128, 255, 255};
  std::vector<uint8_t> rgh = {160};
  if (!createTextureFromRgba(alb, 1, 1, true, defaultAlbedo_)) return false;
  if (!createTextureFromRgba(nrm, 1, 1, false, defaultNormal_)) return false;
  if (!createTextureFromGrey(rgh, 1, 1, defaultRough_)) return false;
  auto assignDefault = [&](MaterialGpu& m) {
    m.albedo = defaultAlbedo_;
    m.normal = defaultNormal_;
    m.roughness = defaultRough_;
    m.valid = false;
  };
  assignDefault(groundMat_);
  assignDefault(rockMat_);
  assignDefault(pathMat_);
  assignDefault(stalkMat_);
  matFlags_ = 0;
  return true;
}

void VulkanContext::destroyMaterialOwned(MaterialGpu& m) {
  if (m.albedo.image && m.albedo.image != defaultAlbedo_.image) destroyTexture(m.albedo);
  if (m.normal.image && m.normal.image != defaultNormal_.image) destroyTexture(m.normal);
  if (m.roughness.image && m.roughness.image != defaultRough_.image) destroyTexture(m.roughness);
  m = {};
}

void VulkanContext::bindMaterialOrDefault(const MaterialGpu& m, VkDescriptorImageInfo& iAlb,
                                          VkDescriptorImageInfo& iNrm,
                                          VkDescriptorImageInfo& iRgh) const {
  const GpuTexture* alb = (m.valid && m.albedo.view) ? &m.albedo : &defaultAlbedo_;
  const GpuTexture* nrm = (m.valid && m.normal.view) ? &m.normal : &defaultNormal_;
  const GpuTexture* rgh = (m.valid && m.roughness.view) ? &m.roughness : &defaultRough_;
  iAlb = {alb->sampler, alb->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  iNrm = {nrm->sampler, nrm->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  iRgh = {rgh->sampler, rgh->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
}

VkCommandBuffer VulkanContext::beginOneTimeCommands() const {
  VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ai.commandPool = cmdPool_;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer cmd;
  vkAllocateCommandBuffers(device_, &ai, &cmd);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &bi);
  return cmd;
}

void VulkanContext::endOneTimeCommands(VkCommandBuffer cmd) const {
  vkEndCommandBuffer(cmd);
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  vkQueueSubmit(graphicsQueue_, 1, &si, VK_NULL_HANDLE);
  vkQueueWaitIdle(graphicsQueue_);
  vkFreeCommandBuffers(device_, cmdPool_, 1, &cmd);
}

bool VulkanContext::transitionImage(VkImage image, VkImageLayout oldL, VkImageLayout newL) {
  VkCommandBuffer cmd = beginOneTimeCommands();
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.oldLayout = oldL;
  b.newLayout = newL;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  b.subresourceRange.levelCount = 1;
  b.subresourceRange.layerCount = 1;
  VkPipelineStageFlags src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkPipelineStageFlags dst = VK_PIPELINE_STAGE_TRANSFER_BIT;
  if (newL == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    src = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dst = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else if (newL == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  }
  vkCmdPipelineBarrier(cmd, src, dst, 0, 0, nullptr, 0, nullptr, 1, &b);
  endOneTimeCommands(cmd);
  return true;
}

bool VulkanContext::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t w, uint32_t h) {
  VkCommandBuffer cmd = beginOneTimeCommands();
  VkBufferImageCopy region{};
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.layerCount = 1;
  region.imageExtent = {w, h, 1};
  vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  endOneTimeCommands(cmd);
  return true;
}

bool VulkanContext::createTextureFromRgba(const std::vector<uint8_t>& rgba, int w, int h, bool srgb,
                                          GpuTexture& out) {
  destroyTexture(out);
  const VkDeviceSize size = static_cast<VkDeviceSize>(rgba.size());
  GpuBuffer staging{};
  if (!createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging))
    return false;
  copyToBuffer(staging, rgba.data(), size);

  out.format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
  out.width = static_cast<uint32_t>(w);
  out.height = static_cast<uint32_t>(h);

  VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ii.imageType = VK_IMAGE_TYPE_2D;
  ii.extent = {out.width, out.height, 1};
  ii.mipLevels = 1;
  ii.arrayLayers = 1;
  ii.format = out.format;
  ii.tiling = VK_IMAGE_TILING_OPTIMAL;
  ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  ii.samples = VK_SAMPLE_COUNT_1_BIT;
  if (vkCreateImage(device_, &ii, nullptr, &out.image) != VK_SUCCESS) return false;
  VkMemoryRequirements req{};
  vkGetImageMemoryRequirements(device_, out.image, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (vkAllocateMemory(device_, &ai, nullptr, &out.memory) != VK_SUCCESS) return false;
  vkBindImageMemory(device_, out.image, out.memory, 0);

  transitionImage(out.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  copyBufferToImage(staging.buffer, out.image, out.width, out.height);
  transitionImage(out.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  destroyBuffer(staging);

  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = out.image;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vi.format = out.format;
  vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  vi.subresourceRange.levelCount = 1;
  vi.subresourceRange.layerCount = 1;
  if (vkCreateImageView(device_, &vi, nullptr, &out.view) != VK_SUCCESS) return false;

  VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  si.magFilter = VK_FILTER_LINEAR;
  si.minFilter = VK_FILTER_LINEAR;
  si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  si.maxAnisotropy = 1.f;
  if (vkCreateSampler(device_, &si, nullptr, &out.sampler) != VK_SUCCESS) return false;
  return true;
}

bool VulkanContext::createTextureFromGrey(const std::vector<uint8_t>& grey, int w, int h, GpuTexture& out) {
  // Expand to RGBA for simplicity
  std::vector<uint8_t> rgba(static_cast<size_t>(w * h * 4));
  for (int i = 0; i < w * h; ++i) {
    rgba[static_cast<size_t>(i) * 4 + 0] = grey[static_cast<size_t>(i)];
    rgba[static_cast<size_t>(i) * 4 + 1] = grey[static_cast<size_t>(i)];
    rgba[static_cast<size_t>(i) * 4 + 2] = grey[static_cast<size_t>(i)];
    rgba[static_cast<size_t>(i) * 4 + 3] = 255;
  }
  return createTextureFromRgba(rgba, w, h, false, out);
}

void VulkanContext::updateMaterialDescriptors() {
  for (int i = 0; i < kMaxFrames; ++i) {
    VkDescriptorBufferInfo ubo{uniformBuffers_[i].buffer, 0, sizeof(FrameUBO)};
    VkDescriptorBufferInfo ssbo{foliageInstanceBuf_.buffer, 0, VK_WHOLE_SIZE};

    VkDescriptorImageInfo gAlb{}, gNrm{}, gRgh{};
    VkDescriptorImageInfo rAlb{}, rNrm{}, rRgh{};
    VkDescriptorImageInfo pAlb{}, pNrm{}, pRgh{};
    VkDescriptorImageInfo sAlb{}, sNrm{}, sRgh{};
    bindMaterialOrDefault(groundMat_, gAlb, gNrm, gRgh);
    bindMaterialOrDefault(rockMat_, rAlb, rNrm, rRgh);
    bindMaterialOrDefault(pathMat_, pAlb, pNrm, pRgh);
    bindMaterialOrDefault(stalkMat_, sAlb, sNrm, sRgh);

    std::array<VkWriteDescriptorSet, 14> writes{};
    auto fill = [&](int idx, uint32_t binding, VkDescriptorType type, const void* pBuf,
                    const VkDescriptorImageInfo* pImg) {
      writes[static_cast<size_t>(idx)].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[static_cast<size_t>(idx)].dstSet = descSets_[i];
      writes[static_cast<size_t>(idx)].dstBinding = binding;
      writes[static_cast<size_t>(idx)].descriptorType = type;
      writes[static_cast<size_t>(idx)].descriptorCount = 1;
      if (pBuf) writes[static_cast<size_t>(idx)].pBufferInfo =
                    static_cast<const VkDescriptorBufferInfo*>(pBuf);
      if (pImg) writes[static_cast<size_t>(idx)].pImageInfo = pImg;
    };
    fill(0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &ubo, nullptr);
    fill(1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &ssbo, nullptr);
    fill(2, 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &gAlb);
    fill(3, 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &gNrm);
    fill(4, 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &gRgh);
    fill(5, 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &rAlb);
    fill(6, 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &rNrm);
    fill(7, 7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &rRgh);
    fill(8, 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &pAlb);
    fill(9, 9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &pNrm);
    fill(10, 10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &pRgh);
    fill(11, 11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &sAlb);
    fill(12, 12, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &sNrm);
    fill(13, 13, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &sRgh);
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
  }
}

bool VulkanContext::loadMaterialSet(const std::string& basePath, MaterialGpu& out) {
  if (device_ == VK_NULL_HANDLE || basePath.empty()) return false;
  ImageData alb, nrm, roughImg;
  if (!loadImage(basePath + "_albedo.png", alb)) return false;
  if (!loadImage(basePath + "_normal.png", nrm)) return false;
  if (!loadImage(basePath + "_roughness.png", roughImg)) return false;

  destroyMaterialOwned(out);

  if (!createTextureFromRgba(alb.pixels, alb.width, alb.height, true, out.albedo)) return false;
  if (!createTextureFromRgba(nrm.pixels, nrm.width, nrm.height, false, out.normal)) return false;
  std::vector<uint8_t> grey(static_cast<size_t>(roughImg.width * roughImg.height));
  for (int i = 0; i < roughImg.width * roughImg.height; ++i)
    grey[static_cast<size_t>(i)] = roughImg.pixels[static_cast<size_t>(i) * 4];
  if (!createTextureFromGrey(grey, roughImg.width, roughImg.height, out.roughness)) return false;

  out.valid = true;
  return true;
}

bool VulkanContext::loadTerrainMaterial(const std::string& albedoPath, const std::string& normalPath,
                                        const std::string& roughnessPath) {
  if (device_ == VK_NULL_HANDLE) return false;
  ImageData alb, nrm, roughImg;
  if (!loadImage(albedoPath, alb)) return false;
  if (!loadImage(normalPath, nrm)) return false;
  if (!loadImage(roughnessPath, roughImg)) return false;

  destroyMaterialOwned(groundMat_);

  if (!createTextureFromRgba(alb.pixels, alb.width, alb.height, true, groundMat_.albedo)) return false;
  if (!createTextureFromRgba(nrm.pixels, nrm.width, nrm.height, false, groundMat_.normal)) return false;
  std::vector<uint8_t> grey(static_cast<size_t>(roughImg.width * roughImg.height));
  for (int i = 0; i < roughImg.width * roughImg.height; ++i)
    grey[static_cast<size_t>(i)] = roughImg.pixels[static_cast<size_t>(i) * 4];
  if (!createTextureFromGrey(grey, roughImg.width, roughImg.height, groundMat_.roughness)) return false;

  groundMat_.valid = true;
  matFlags_ |= kMatGround;
  updateMaterialDescriptors();
  logInfo("Terrain ground PBR material loaded");
  return true;
}

bool VulkanContext::loadBiomeMaterials(const std::string& groundBase, const std::string& rockBase,
                                       const std::string& pathBase, const std::string& stalkBase) {
  if (device_ == VK_NULL_HANDLE) return false;
  int flags = 0;
  if (!groundBase.empty() && loadMaterialSet(groundBase, groundMat_)) flags |= kMatGround;
  if (!rockBase.empty() && loadMaterialSet(rockBase, rockMat_)) flags |= kMatRock;
  if (!pathBase.empty() && loadMaterialSet(pathBase, pathMat_)) flags |= kMatPath;
  if (!stalkBase.empty() && loadMaterialSet(stalkBase, stalkMat_)) flags |= kMatStalk;
  matFlags_ = flags;
  updateMaterialDescriptors();
  logInfo("Biome materials loaded flags=" + std::to_string(flags) +
          " (1=ground 2=rock 4=path 8=stalk)");
  return flags != 0;
}

bool VulkanContext::uploadTerrain(const std::vector<VertexPC>& verts,
                                  const std::vector<uint32_t>& indices) {
  if (!valid_ && device_ == VK_NULL_HANDLE) return false;
  vkDeviceWaitIdle(device_);
  destroyBuffer(terrain_.vertex);
  destroyBuffer(terrain_.index);
  const VkDeviceSize vsize = sizeof(VertexPC) * verts.size();
  const VkDeviceSize isize = sizeof(uint32_t) * indices.size();
  if (!createBuffer(vsize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    terrain_.vertex))
    return false;
  if (!createBuffer(isize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    terrain_.index))
    return false;
  copyToBuffer(terrain_.vertex, verts.data(), vsize);
  copyToBuffer(terrain_.index, indices.data(), isize);
  terrain_.vertexCount = static_cast<uint32_t>(verts.size());
  terrain_.indexCount = static_cast<uint32_t>(indices.size());
  logInfo("Terrain GPU upload: " + std::to_string(terrain_.vertexCount) + " verts, " +
          std::to_string(terrain_.indexCount) + " indices");
  return true;
}

bool VulkanContext::uploadStalkMesh(const std::vector<VertexPC>& verts,
                                    const std::vector<uint32_t>& indices) {
  if (device_ == VK_NULL_HANDLE) return false;
  vkDeviceWaitIdle(device_);
  destroyBuffer(stalk_.vertex);
  destroyBuffer(stalk_.index);
  const VkDeviceSize vsize = sizeof(VertexPC) * verts.size();
  const VkDeviceSize isize = sizeof(uint32_t) * indices.size();
  if (!createBuffer(vsize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    stalk_.vertex))
    return false;
  if (!createBuffer(isize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    stalk_.index))
    return false;
  copyToBuffer(stalk_.vertex, verts.data(), vsize);
  copyToBuffer(stalk_.index, indices.data(), isize);
  stalk_.vertexCount = static_cast<uint32_t>(verts.size());
  stalk_.indexCount = static_cast<uint32_t>(indices.size());
  logInfo("Stalk mesh GPU: " + std::to_string(stalk_.indexCount) + " indices");
  return true;
}

bool VulkanContext::uploadFoliage(const std::vector<FoliageInstanceGPU>& instances) {
  if (device_ == VK_NULL_HANDLE) return false;
  foliageCount_ = static_cast<uint32_t>(instances.size());
  if (foliageCount_ == 0) return true;
  if (foliageCount_ > foliageCapacity_) {
    vkDeviceWaitIdle(device_);
    destroyBuffer(foliageInstanceBuf_);
    const uint32_t cap = std::max(foliageCount_ * 2, 256u);
    createBuffer(sizeof(FoliageInstanceGPU) * cap, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 foliageInstanceBuf_);
    foliageCapacity_ = cap;
    // refresh descriptors
    for (int i = 0; i < kMaxFrames; ++i) {
      VkDescriptorBufferInfo ssbo{foliageInstanceBuf_.buffer, 0, VK_WHOLE_SIZE};
      VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      w.dstSet = descSets_[i];
      w.dstBinding = 1;
      w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      w.descriptorCount = 1;
      w.pBufferInfo = &ssbo;
      vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
    }
  }
  copyToBuffer(foliageInstanceBuf_, instances.data(),
               sizeof(FoliageInstanceGPU) * foliageCount_);
  return true;
}

void VulkanContext::beginFrame() {
  // no-op; drawFrame acquires
}

void VulkanContext::endFrame() {
  // no-op
}

void VulkanContext::resize(int w, int h) {
  width_ = w;
  height_ = h;
  framebufferResized_ = true;
}

bool VulkanContext::recreateSwapchain() {
  if (!window_) return false;
  // Minimize → 0×0; wait until the window has a real size again
  int w = 0, h = 0;
  glfwGetFramebufferSize(window_, &w, &h);
  while (w == 0 || h == 0) {
    glfwWaitEvents();
    if (!window_ || glfwWindowShouldClose(window_)) return false;
    glfwGetFramebufferSize(window_, &w, &h);
  }
  width_ = w;
  height_ = h;

  vkDeviceWaitIdle(device_);
  cleanupSwapchain();
  if (!createSwapchain()) return false;
  if (!createFramebuffers()) return false;
  framebufferResized_ = false;
  logInfo("Swapchain recreated " + std::to_string(swapExtent_.width) + "x" +
          std::to_string(swapExtent_.height));
  return true;
}

void VulkanContext::drawFrame(const FrameUBO& ubo, uint32_t foliageCount) {
  if (device_ == VK_NULL_HANDLE || !valid_) return;
  if (framebufferResized_) {
    recreateSwapchain();
  }
  if (swapExtent_.width == 0 || swapExtent_.height == 0) return;

  vkWaitForFences(device_, 1, &inFlight_[frameIndex_], VK_TRUE, UINT64_MAX);

  uint32_t imageIndex = 0;
  VkResult acq = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, imageAvailable_[frameIndex_],
                                       VK_NULL_HANDLE, &imageIndex);
  if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
    recreateSwapchain();
    return;
  }
  if (acq == VK_SUBOPTIMAL_KHR) {
    // still render this frame, recreate after present
  } else if (acq != VK_SUCCESS) {
    return;
  }
  vkResetFences(device_, 1, &inFlight_[frameIndex_]);

  std::memcpy(uniformMapped_[frameIndex_], &ubo, sizeof(FrameUBO));

  VkCommandBuffer cmd = cmdBuffers_[frameIndex_];
  vkResetCommandBuffer(cmd, 0);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  vkBeginCommandBuffer(cmd, &bi);

  VkClearValue clears[2]{};
  // Clear is overwritten by sky pass; keep deep fallback
  clears[0].color = {{0.015f, 0.03f, 0.08f, 1.f}};
  clears[1].depthStencil = {1.f, 0};
  VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  rp.renderPass = renderPass_;
  rp.framebuffer = framebuffers_[imageIndex];
  rp.renderArea.extent = swapExtent_;
  rp.clearValueCount = 2;
  rp.pClearValues = clears;
  vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport vp{};
  vp.width = static_cast<float>(swapExtent_.width);
  vp.height = static_cast<float>(swapExtent_.height);
  vp.maxDepth = 1.f;
  vkCmdSetViewport(cmd, 0, 1, &vp);
  VkRect2D sc{{0, 0}, swapExtent_};
  vkCmdSetScissor(cmd, 0, 1, &sc);

  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1,
                          &descSets_[frameIndex_], 0, nullptr);

  // Sky first (no depth write)
  if (skyPipeline_) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline_);
    vkCmdDraw(cmd, 3, 1, 0, 0);
  }

  if (terrainPipeline_ && terrain_.indexCount > 0) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipeline_);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &terrain_.vertex.buffer, &off);
    vkCmdBindIndexBuffer(cmd, terrain_.index.buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, terrain_.indexCount, 1, 0, 0, 0);
  }

  if (foliagePipeline_ && foliageCount > 0 && stalk_.indexCount > 0) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, foliagePipeline_);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &stalk_.vertex.buffer, &off);
    vkCmdBindIndexBuffer(cmd, stalk_.index.buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, stalk_.indexCount, foliageCount, 0, 0, 0);
  }

  vkCmdEndRenderPass(cmd);
  vkEndCommandBuffer(cmd);

  VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.waitSemaphoreCount = 1;
  submit.pWaitSemaphores = &imageAvailable_[frameIndex_];
  submit.pWaitDstStageMask = &waitStage;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;
  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = &renderFinished_[frameIndex_];
  vkQueueSubmit(graphicsQueue_, 1, &submit, inFlight_[frameIndex_]);

  VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  present.waitSemaphoreCount = 1;
  present.pWaitSemaphores = &renderFinished_[frameIndex_];
  present.swapchainCount = 1;
  present.pSwapchains = &swapchain_;
  present.pImageIndices = &imageIndex;
  VkResult pr = vkQueuePresentKHR(presentQueue_, &present);
  if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR || framebufferResized_) {
    recreateSwapchain();
  }
  frameIndex_ = (frameIndex_ + 1) % kMaxFrames;
}

} // namespace bolt
