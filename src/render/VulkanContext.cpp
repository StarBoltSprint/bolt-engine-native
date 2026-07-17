#include "bolt/render/VulkanContext.hpp"
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
  for (auto& u : uniformBuffers_) destroyBuffer(u);
  uniformMapped_.clear();
  if (descPool_) vkDestroyDescriptorPool(device_, descPool_, nullptr);
  if (descLayout_) vkDestroyDescriptorSetLayout(device_, descLayout_, nullptr);
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

void VulkanContext::cleanupSwapchain() {
  for (auto fb : framebuffers_) vkDestroyFramebuffer(device_, fb, nullptr);
  framebuffers_.clear();
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

  VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1;
  sub.pColorAttachments = &colorRef;

  VkSubpassDependency dep{};
  dep.srcSubpass = VK_SUBPASS_EXTERNAL;
  dep.dstSubpass = 0;
  dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  ci.attachmentCount = 1;
  ci.pAttachments = &color;
  ci.subpassCount = 1;
  ci.pSubpasses = &sub;
  ci.dependencyCount = 1;
  ci.pDependencies = &dep;
  return vkCreateRenderPass(device_, &ci, nullptr, &renderPass_) == VK_SUCCESS;
}

bool VulkanContext::createFramebuffers() {
  framebuffers_.resize(swapViews_.size());
  for (size_t i = 0; i < swapViews_.size(); ++i) {
    VkImageView atts[] = {swapViews_[i]};
    VkFramebufferCreateInfo ci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    ci.renderPass = renderPass_;
    ci.attachmentCount = 1;
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

  std::array<VkDescriptorSetLayoutBinding, 2> binds = {ubo, inst};
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
  if (!vert || !frag) {
    if (vert) vkDestroyShaderModule(device_, vert, nullptr);
    if (frag) vkDestroyShaderModule(device_, frag, nullptr);
    if (fvert) vkDestroyShaderModule(device_, fvert, nullptr);
    if (ffrag) vkDestroyShaderModule(device_, ffrag, nullptr);
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

  VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkViewport vp{};
  vp.width = static_cast<float>(swapExtent_.width);
  vp.height = static_cast<float>(swapExtent_.height);
  vp.maxDepth = 1.f;
  VkRect2D sc{{0, 0}, swapExtent_};
  VkPipelineViewportStateCreateInfo vs{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vs.viewportCount = 1;
  vs.pViewports = &vp;
  vs.scissorCount = 1;
  vs.pScissors = &sc;

  VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_BACK_BIT;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.f;

  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

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
  gp.pColorBlendState = &cb;
  gp.layout = pipelineLayout_;
  gp.renderPass = renderPass_;
  gp.subpass = 0;
  if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &terrainPipeline_) !=
      VK_SUCCESS) {
    logError("terrain pipeline failed");
  }

  // Foliage pipeline: instance via storage buffer (gl_InstanceIndex)
  if (fvert && ffrag) {
    stages[0].module = fvert;
    stages[1].module = ffrag;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &foliagePipeline_) !=
        VK_SUCCESS) {
      logWarn("foliage pipeline failed");
    }
  }

  vkDestroyShaderModule(device_, vert, nullptr);
  vkDestroyShaderModule(device_, frag, nullptr);
  if (fvert) vkDestroyShaderModule(device_, fvert, nullptr);
  if (ffrag) vkDestroyShaderModule(device_, ffrag, nullptr);
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
  std::array<VkDescriptorPoolSize, 2> sizes{};
  sizes[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxFrames};
  sizes[1] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kMaxFrames};
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

  for (int i = 0; i < kMaxFrames; ++i) {
    VkDescriptorBufferInfo ubo{uniformBuffers_[i].buffer, 0, sizeof(FrameUBO)};
    VkDescriptorBufferInfo ssbo{foliageInstanceBuf_.buffer, 0, VK_WHOLE_SIZE};
    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descSets_[i];
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &ubo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descSets_[i];
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo = &ssbo;
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
  }
  return true;
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
  vkDeviceWaitIdle(device_);
  cleanupSwapchain();
  if (!createSwapchain()) return false;
  if (!createFramebuffers()) return false;
  framebufferResized_ = false;
  return true;
}

void VulkanContext::drawFrame(const FrameUBO& ubo, uint32_t foliageCount) {
  if (device_ == VK_NULL_HANDLE) return;
  vkWaitForFences(device_, 1, &inFlight_[frameIndex_], VK_TRUE, UINT64_MAX);

  uint32_t imageIndex = 0;
  VkResult acq = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, imageAvailable_[frameIndex_],
                                       VK_NULL_HANDLE, &imageIndex);
  if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
    recreateSwapchain();
    return;
  }
  currentImage_ = imageIndex;
  vkResetFences(device_, 1, &inFlight_[frameIndex_]);

  std::memcpy(uniformMapped_[frameIndex_], &ubo, sizeof(FrameUBO));

  VkCommandBuffer cmd = cmdBuffers_[frameIndex_];
  vkResetCommandBuffer(cmd, 0);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  vkBeginCommandBuffer(cmd, &bi);

  VkClearValue clear{};
  // Crystal night sky
  clear.color = {{0.02f, 0.04f, 0.10f, 1.f}};
  VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  rp.renderPass = renderPass_;
  rp.framebuffer = framebuffers_[imageIndex];
  rp.renderArea.extent = swapExtent_;
  rp.clearValueCount = 1;
  rp.pClearValues = &clear;
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
