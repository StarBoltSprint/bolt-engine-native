#include "bolt/render/VulkanContext.hpp"
#include "bolt/assets/TextureLoader.hpp"
#include "bolt/core/Log.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>

#if defined(BOLT_USE_VMA)
#include <vk_mem_alloc.h>
#endif

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
  if (!createVmaAllocator()) {
    logWarn("VMA allocator failed — using legacy vkAllocateMemory for buffers");
  }
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
  if (!createPostPipeline()) {
    logWarn("Post pipeline failed — bloom/tonemap disabled");
  } else {
    updatePostDescriptors();
  }
  if (!createCullPipeline()) {
    logWarn("GPU cull pipeline failed — falling back to CPU instance draws");
    gpuCullEnabled_ = false;
  }
  if (!createDeferredResources() || !createDeferredPipelines()) {
    logWarn("Deferred GBuffer path unavailable — forward IBL lighting only");
    destroyDeferredResources();
    deferredReady_ = false;
  } else {
    deferredReady_ = true;
    updateDeferredDescriptors();
    logInfo("Deferred GBuffer + lighting ready (terrain/path MRT)");
  }
  if (!createShadowResources() || !createShadowPipelines()) {
    logWarn("Shadow map unavailable — unshadowed lighting");
    destroyShadowResources();
    shadowReady_ = false;
  } else {
    shadowReady_ = true;
    updateMaterialDescriptors();
    if (deferredReady_) updateDeferredDescriptors(); // bind CSM array into deferred light set
    logInfo("Cascaded sun shadows ready (3x1024 PCF CSM)");
  }
  if (!createSync()) return false;
  valid_ = true;
  logInfo(std::string("VulkanContext: device + swapchain + post") +
          (cullPipeline_ ? " + GPU cull/indirect" : "") +
          (vmaAllocator_ ? " + VMA" : "") +
          (deferredReady_ ? " + deferred GBuffer" : " + forward IBL") +
          (shadowReady_ ? " + shadows" : "") + " + SSAO/IBL + crystal lights ready");
  return true;
}

void VulkanContext::shutdown() {
  if (device_ != VK_NULL_HANDLE) vkDeviceWaitIdle(device_);
  cleanupSwapchain();
  destroyBuffer(terrain_.vertex);
  destroyBuffer(terrain_.index);
  destroyBuffer(stalk_.vertex);
  destroyBuffer(stalk_.index);
  destroyBuffer(bush_.vertex);
  destroyBuffer(bush_.index);
  destroyBuffer(tall_.vertex);
  destroyBuffer(tall_.index);
  for (auto& tm : treeMeshes_) {
    destroyBuffer(tm.vertex);
    destroyBuffer(tm.index);
  }
  destroyBuffer(detail_.vertex);
  destroyBuffer(detail_.index);
  destroyBuffer(ruin_.vertex);
  destroyBuffer(ruin_.index);
  destroyBuffer(ruinArch_.vertex);
  destroyBuffer(ruinArch_.index);
  destroyBuffer(ruinObs_.vertex);
  destroyBuffer(ruinObs_.index);
  destroyBuffer(ruinTemple_.vertex);
  destroyBuffer(ruinTemple_.index);
  destroyBuffer(pathRibbon_.vertex);
  destroyBuffer(pathRibbon_.index);
  destroyBuffer(blob_.vertex);
  destroyBuffer(blob_.index);
  destroyBuffer(bolt_.vertex);
  destroyBuffer(bolt_.index);
  for (auto& p : boltParts_) {
    destroyBuffer(p.vertex);
    destroyBuffer(p.index);
  }
  destroyBuffer(foliageInstanceBuf_);
  destroyBuffer(foliageCulledBuf_);
  destroyBuffer(cullParamBuf_);
  destroyBuffer(indirectCmdBuf_);
  cullParamMapped_ = nullptr;
  indirectMapped_ = nullptr;
  destroyBuffer(particleBuf_);
  destroyBuffer(crystalLightBuf_);
  if (cullPipeline_) vkDestroyPipeline(device_, cullPipeline_, nullptr);
  if (cullPipelineLayout_) vkDestroyPipelineLayout(device_, cullPipelineLayout_, nullptr);
  if (cullDescPool_) vkDestroyDescriptorPool(device_, cullDescPool_, nullptr);
  if (cullDescLayout_) vkDestroyDescriptorSetLayout(device_, cullDescLayout_, nullptr);
  destroyMaterialOwned(groundMat_);
  destroyMaterialOwned(rockMat_);
  destroyMaterialOwned(pathMat_);
  destroyMaterialOwned(stalkMat_);
  destroyTexture(defaultAlbedo_);
  destroyTexture(defaultNormal_);
  destroyTexture(defaultRough_);
  destroyTexture(boltAlbedo_);
  destroyTexture(boltNormal_);
  destroyTexture(boltRough_);
  destroyTexture(boltEmissive_);
  destroyTexture(boltHeight_);
  destroyTexture(defaultEmissive_);
  boltFurValid_ = false;
  boltHasEmissive_ = false;
  boltHasHeight_ = false;
  for (auto& u : uniformBuffers_) destroyBuffer(u);
  uniformMapped_.clear();
  if (descPool_) vkDestroyDescriptorPool(device_, descPool_, nullptr);
  if (descLayout_) vkDestroyDescriptorSetLayout(device_, descLayout_, nullptr);
  if (skyPipeline_) vkDestroyPipeline(device_, skyPipeline_, nullptr);
  if (terrainPipeline_) vkDestroyPipeline(device_, terrainPipeline_, nullptr);
  if (foliagePipeline_) vkDestroyPipeline(device_, foliagePipeline_, nullptr);
  if (blobPipeline_) vkDestroyPipeline(device_, blobPipeline_, nullptr);
  if (boltPipeline_) vkDestroyPipeline(device_, boltPipeline_, nullptr);
  if (particlePipeline_) vkDestroyPipeline(device_, particlePipeline_, nullptr);
  if (postPipeline_) vkDestroyPipeline(device_, postPipeline_, nullptr);
  if (pipelineLayout_) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
  if (postPipelineLayout_) vkDestroyPipelineLayout(device_, postPipelineLayout_, nullptr);
  if (postDescPool_) vkDestroyDescriptorPool(device_, postDescPool_, nullptr);
  if (postDescLayout_) vkDestroyDescriptorSetLayout(device_, postDescLayout_, nullptr);
  if (postSampler_) vkDestroySampler(device_, postSampler_, nullptr);
  if (depthSampler_) vkDestroySampler(device_, depthSampler_, nullptr);
  destroyBuffer(postUbo_);
  postUboMapped_ = nullptr;
  if (shadowMeshPipeline_) vkDestroyPipeline(device_, shadowMeshPipeline_, nullptr);
  if (shadowFoliagePipeline_) vkDestroyPipeline(device_, shadowFoliagePipeline_, nullptr);
  destroyShadowResources();
  if (shadowRenderPass_) {
    vkDestroyRenderPass(device_, shadowRenderPass_, nullptr);
    shadowRenderPass_ = VK_NULL_HANDLE;
  }
  if (gbufTerrainPipeline_) vkDestroyPipeline(device_, gbufTerrainPipeline_, nullptr);
  if (deferredLightPipeline_) vkDestroyPipeline(device_, deferredLightPipeline_, nullptr);
  if (deferredLightLayout_) vkDestroyPipelineLayout(device_, deferredLightLayout_, nullptr);
  if (deferredLightDescPool_) vkDestroyDescriptorPool(device_, deferredLightDescPool_, nullptr);
  if (deferredLightDescLayout_) vkDestroyDescriptorSetLayout(device_, deferredLightDescLayout_, nullptr);
  destroyDeferredResources();
  if (gbufferRenderPass_) {
    vkDestroyRenderPass(device_, gbufferRenderPass_, nullptr);
    gbufferRenderPass_ = VK_NULL_HANDLE;
  }
  if (lightRenderPass_) {
    vkDestroyRenderPass(device_, lightRenderPass_, nullptr);
    lightRenderPass_ = VK_NULL_HANDLE;
  }
  if (forwardOverlayPass_) {
    vkDestroyRenderPass(device_, forwardOverlayPass_, nullptr);
    forwardOverlayPass_ = VK_NULL_HANDLE;
  }
  if (renderPass_) vkDestroyRenderPass(device_, renderPass_, nullptr);
  destroyPostPass();
  destroySceneTarget();
  for (int i = 0; i < kMaxFrames; ++i) {
    if (imageAvailable_[i]) vkDestroySemaphore(device_, imageAvailable_[i], nullptr);
    if (renderFinished_[i]) vkDestroySemaphore(device_, renderFinished_[i], nullptr);
    if (inFlight_[i]) vkDestroyFence(device_, inFlight_[i], nullptr);
  }
  if (cmdPool_) vkDestroyCommandPool(device_, cmdPool_, nullptr);
  destroyVmaAllocator();
  if (device_) vkDestroyDevice(device_, nullptr);
  if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
  if (instance_) vkDestroyInstance(instance_, nullptr);
  valid_ = false;
}

bool VulkanContext::createVmaAllocator() {
#if defined(BOLT_USE_VMA)
  if (!device_ || !physical_ || !instance_) return false;
  VmaAllocatorCreateInfo aci{};
  aci.physicalDevice = physical_;
  aci.device = device_;
  aci.instance = instance_;
  aci.vulkanApiVersion = VK_API_VERSION_1_1;
  VmaAllocator alloc = nullptr;
  if (vmaCreateAllocator(&aci, &alloc) != VK_SUCCESS) {
    logError("vmaCreateAllocator failed");
    return false;
  }
  vmaAllocator_ = alloc;
  logInfo("VMA allocator ready (stable GPU memory for streaming)");
  return true;
#else
  return false;
#endif
}

void VulkanContext::destroyVmaAllocator() {
#if defined(BOLT_USE_VMA)
  if (vmaAllocator_) {
    vmaDestroyAllocator(static_cast<VmaAllocator>(vmaAllocator_));
    vmaAllocator_ = nullptr;
  }
#endif
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

  // Mesh shaders (Vulkan EXT) — not available on Intel HD 620 / most iGPUs
  {
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(physical_, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    vkEnumerateDeviceExtensionProperties(physical_, nullptr, &extCount, exts.data());
    bool mesh = false;
    for (const auto& e : exts) {
      if (std::strcmp(e.extensionName, "VK_EXT_mesh_shader") == 0 ||
          std::strcmp(e.extensionName, "VK_NV_mesh_shader") == 0) {
        mesh = true;
        break;
      }
    }
    if (mesh) {
      logInfo("Mesh shaders: extension present (not wired — use GPU cull/indirect for veg)");
    } else {
      logInfo("Mesh shaders: unsupported on this GPU — using compute cull + DrawIndirect for dense veg");
    }
    bool rt = false;
    for (const auto& e : exts) {
      if (std::strcmp(e.extensionName, "VK_KHR_ray_tracing_pipeline") == 0 ||
          std::strcmp(e.extensionName, "VK_KHR_ray_query") == 0 ||
          std::strcmp(e.extensionName, "VK_NV_ray_tracing") == 0) {
        rt = true;
        break;
      }
    }
    if (rt) {
      logInfo("Ray tracing: extension present (optional quality path not wired on this build)");
    } else {
      logInfo("Ray tracing: unsupported on this GPU — raster shadows + SSAO + IBL instead");
    }
  }
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
  ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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
  destroyPostPass();
  destroyDeferredResources();
  destroySceneTarget();
  destroyDepthResources();
  for (auto v : swapViews_) vkDestroyImageView(device_, v, nullptr);
  swapViews_.clear();
  if (swapchain_) {
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
  }
}

bool VulkanContext::createRenderPass() {
  // Scene pass: offscreen color + depth → SHADER_READ for post
  VkAttachmentDescription color{};
  color.format = swapFormat_;
  color.samples = VK_SAMPLE_COUNT_1_BIT;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkAttachmentDescription depth{};
  depth.format = depthFormat_;
  depth.samples = VK_SAMPLE_COUNT_1_BIT;
  depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // TAA needs sampleable depth
  depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depth.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1;
  sub.pColorAttachments = &colorRef;
  sub.pDepthStencilAttachment = &depthRef;

  std::array<VkSubpassDependency, 2> deps{};
  deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  deps[0].dstSubpass = 0;
  deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
  deps[0].dstAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

  deps[1].srcSubpass = 0;
  deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

  std::array<VkAttachmentDescription, 2> atts = {color, depth};
  VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  ci.attachmentCount = static_cast<uint32_t>(atts.size());
  ci.pAttachments = atts.data();
  ci.subpassCount = 1;
  ci.pSubpasses = &sub;
  ci.dependencyCount = static_cast<uint32_t>(deps.size());
  ci.pDependencies = deps.data();
  return vkCreateRenderPass(device_, &ci, nullptr, &renderPass_) == VK_SUCCESS;
}

bool VulkanContext::createSceneTarget() {
  destroySceneTarget();
  auto makeColor = [&](VkImage& img, VkDeviceMemory& mem, VkImageView& view,
                       VkImageUsageFlags extraUsage) -> bool {
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = swapFormat_;
    ii.extent = {swapExtent_.width, swapExtent_.height, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
               VK_IMAGE_USAGE_TRANSFER_SRC_BIT | extraUsage;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device_, &ii, nullptr, &img) != VK_SUCCESS) return false;
    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(device_, img, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device_, &ai, nullptr, &mem) != VK_SUCCESS) return false;
    vkBindImageMemory(device_, img, mem, 0);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = img;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = swapFormat_;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    return vkCreateImageView(device_, &vi, nullptr, &view) == VK_SUCCESS;
  };

  if (!makeColor(sceneImage_, sceneMemory_, sceneView_, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
    return false;
  // History for TAA (previous frame scene) — first use is black/undefined → low blend
  if (!makeColor(historyImage_, historyMemory_, historyView_, 0)) return false;
  historyValid_ = false;

  VkImageView atts[] = {sceneView_, depthView_};
  VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  fci.renderPass = renderPass_;
  fci.attachmentCount = 2;
  fci.pAttachments = atts;
  fci.width = swapExtent_.width;
  fci.height = swapExtent_.height;
  fci.layers = 1;
  return vkCreateFramebuffer(device_, &fci, nullptr, &sceneFramebuffer_) == VK_SUCCESS;
}

void VulkanContext::destroySceneTarget() {
  if (sceneFramebuffer_) {
    vkDestroyFramebuffer(device_, sceneFramebuffer_, nullptr);
    sceneFramebuffer_ = VK_NULL_HANDLE;
  }
  if (sceneView_) {
    vkDestroyImageView(device_, sceneView_, nullptr);
    sceneView_ = VK_NULL_HANDLE;
  }
  if (sceneImage_) {
    vkDestroyImage(device_, sceneImage_, nullptr);
    sceneImage_ = VK_NULL_HANDLE;
  }
  if (sceneMemory_) {
    vkFreeMemory(device_, sceneMemory_, nullptr);
    sceneMemory_ = VK_NULL_HANDLE;
  }
  if (historyView_) {
    vkDestroyImageView(device_, historyView_, nullptr);
    historyView_ = VK_NULL_HANDLE;
  }
  if (historyImage_) {
    vkDestroyImage(device_, historyImage_, nullptr);
    historyImage_ = VK_NULL_HANDLE;
  }
  if (historyMemory_) {
    vkFreeMemory(device_, historyMemory_, nullptr);
    historyMemory_ = VK_NULL_HANDLE;
  }
  historyValid_ = false;
}

bool VulkanContext::createPostPass() {
  destroyPostPass();
  VkAttachmentDescription color{};
  color.format = swapFormat_;
  color.samples = VK_SAMPLE_COUNT_1_BIT;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
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
  if (vkCreateRenderPass(device_, &ci, nullptr, &postRenderPass_) != VK_SUCCESS) return false;

  postFramebuffers_.resize(swapViews_.size());
  for (size_t i = 0; i < swapViews_.size(); ++i) {
    VkImageView att = swapViews_[i];
    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass = postRenderPass_;
    fci.attachmentCount = 1;
    fci.pAttachments = &att;
    fci.width = swapExtent_.width;
    fci.height = swapExtent_.height;
    fci.layers = 1;
    if (vkCreateFramebuffer(device_, &fci, nullptr, &postFramebuffers_[i]) != VK_SUCCESS)
      return false;
  }
  return true;
}

void VulkanContext::destroyPostPass() {
  for (auto fb : postFramebuffers_)
    if (fb) vkDestroyFramebuffer(device_, fb, nullptr);
  postFramebuffers_.clear();
  if (postRenderPass_) {
    vkDestroyRenderPass(device_, postRenderPass_, nullptr);
    postRenderPass_ = VK_NULL_HANDLE;
  }
}

bool VulkanContext::createFramebuffers() {
  if (!createDepthResources()) return false;
  if (!createSceneTarget()) return false;
  if (!createPostPass()) return false;
  // Legacy framebuffers_ unused (scene uses sceneFramebuffer_)
  framebuffers_.clear();
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
  // 14 particles; 15–17 bolt alb/nrm/rough; 18 lights;
  // 19–22 material emissives; 23 bolt emissive; 24 sun shadow map
  std::array<VkDescriptorSetLayoutBinding, 25> binds{};
  binds[0] = ubo;
  binds[1] = inst;
  for (uint32_t b = 2; b <= 13; ++b) {
    binds[b] = {};
    binds[b].binding = b;
    binds[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[b].descriptorCount = 1;
    binds[b].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  }
  binds[14] = {};
  binds[14].binding = 14;
  binds[14].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  binds[14].descriptorCount = 1;
  binds[14].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  for (uint32_t b = 15; b <= 17; ++b) {
    binds[b] = {};
    binds[b].binding = b;
    binds[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[b].descriptorCount = 1;
    binds[b].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  }
  binds[18] = {};
  binds[18].binding = 18;
  binds[18].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  binds[18].descriptorCount = 1;
  binds[18].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  for (uint32_t b = 19; b <= 23; ++b) {
    binds[b] = {};
    binds[b].binding = b;
    binds[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[b].descriptorCount = 1;
    binds[b].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  }
  binds[24] = {};
  binds[24].binding = 24;
  binds[24].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  binds[24].descriptorCount = 1;
  binds[24].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

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
  std::array<VkVertexInputAttributeDescription, 4> attrs{};
  attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexPC, pos)};
  attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexPC, normal)};
  attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VertexPC, uv)};
  attrs[3] = {3, 0, VK_FORMAT_R32_SFLOAT, offsetof(VertexPC, matId)};

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

  VkPushConstantRange pcr{};
  pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pcr.offset = 0;
  pcr.size = sizeof(ObjectPush);

  VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  plci.setLayoutCount = 1;
  plci.pSetLayouts = &descLayout_;
  plci.pushConstantRangeCount = 1;
  plci.pPushConstantRanges = &pcr;
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

  // Alpha-blended soft shadow blobs + particles
  VkPipelineColorBlendAttachmentState blendAlpha = blendAtt;
  blendAlpha.blendEnable = VK_TRUE;
  blendAlpha.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blendAlpha.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blendAlpha.colorBlendOp = VK_BLEND_OP_ADD;
  blendAlpha.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  blendAlpha.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blendAlpha.alphaBlendOp = VK_BLEND_OP_ADD;
  VkPipelineColorBlendStateCreateInfo cbAlpha = cb;
  cbAlpha.pAttachments = &blendAlpha;

  VkPipelineDepthStencilStateCreateInfo dsNoWrite = ds;
  dsNoWrite.depthWriteEnable = VK_FALSE;
  dsNoWrite.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

  VkShaderModule bvert = loadShaderModule("assets/shaders/blob.vert.spv");
  VkShaderModule bfrag = loadShaderModule("assets/shaders/blob.frag.spv");
  if (bvert && bfrag) {
    stages[0].module = bvert;
    stages[1].module = bfrag;
    gp.pColorBlendState = &cbAlpha;
    gp.pDepthStencilState = &dsNoWrite;
    rs.cullMode = VK_CULL_MODE_NONE;
    gp.pRasterizationState = &rs;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &blobPipeline_) !=
        VK_SUCCESS) {
      logWarn("blob pipeline failed");
    }
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    gp.pRasterizationState = &rs;
    gp.pColorBlendState = &cb;
    gp.pDepthStencilState = &ds;
  }

  VkShaderModule boltV = loadShaderModule("assets/shaders/bolt.vert.spv");
  VkShaderModule boltF = loadShaderModule("assets/shaders/bolt.frag.spv");
  if (boltV && boltF) {
    stages[0].module = boltV;
    stages[1].module = boltF;
    // GSD + aura: alpha blend for aura shell, depth test on, depth write for solid
    gp.pColorBlendState = &cbAlpha;
    gp.pDepthStencilState = &ds;
    rs.cullMode = VK_CULL_MODE_NONE; // ears / thin parts
    gp.pRasterizationState = &rs;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &boltPipeline_) !=
        VK_SUCCESS) {
      logWarn("bolt pipeline failed");
    }
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    gp.pRasterizationState = &rs;
    gp.pColorBlendState = &cb;
    gp.pDepthStencilState = &ds;
  }

  VkShaderModule pvert = loadShaderModule("assets/shaders/particle.vert.spv");
  VkShaderModule pfrag = loadShaderModule("assets/shaders/particle.frag.spv");
  if (pvert && pfrag) {
    stages[0].module = pvert;
    stages[1].module = pfrag;
    gp.pColorBlendState = &cbAlpha;
    gp.pDepthStencilState = &dsNoWrite;
    rs.cullMode = VK_CULL_MODE_NONE;
    gp.pRasterizationState = &rs;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &particlePipeline_) !=
        VK_SUCCESS) {
      logWarn("particle pipeline failed");
    }
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    gp.pRasterizationState = &rs;
    gp.pColorBlendState = &cb;
    gp.pDepthStencilState = &ds;
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
  if (bvert) vkDestroyShaderModule(device_, bvert, nullptr);
  if (bfrag) vkDestroyShaderModule(device_, bfrag, nullptr);
  if (boltV) vkDestroyShaderModule(device_, boltV, nullptr);
  if (boltF) vkDestroyShaderModule(device_, boltF, nullptr);
  if (pvert) vkDestroyShaderModule(device_, pvert, nullptr);
  if (pfrag) vkDestroyShaderModule(device_, pfrag, nullptr);
  return terrainPipeline_ != VK_NULL_HANDLE;
}

bool VulkanContext::createPostPipeline() {
  // Color sampler (linear) + depth sampler (nearest)
  VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sci.magFilter = VK_FILTER_LINEAR;
  sci.minFilter = VK_FILTER_LINEAR;
  sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  if (vkCreateSampler(device_, &sci, nullptr, &postSampler_) != VK_SUCCESS) return false;
  sci.magFilter = VK_FILTER_NEAREST;
  sci.minFilter = VK_FILTER_NEAREST;
  if (vkCreateSampler(device_, &sci, nullptr, &depthSampler_) != VK_SUCCESS) return false;

  // UBO for TAA / motion blur / bloom
  if (!createBuffer(sizeof(PostUBOData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    postUbo_))
    return false;
  if (!mapBufferPersistent(postUbo_, &postUboMapped_)) return false;

  // 0 scene, 1 ubo, 2 depth, 3 history
  std::array<VkDescriptorSetLayoutBinding, 4> binds{};
  for (uint32_t i = 0; i < 4; ++i) {
    binds[i].binding = i;
    binds[i].descriptorCount = 1;
    binds[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[i].descriptorType =
        (i == 1) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  }
  VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  lci.bindingCount = 4;
  lci.pBindings = binds.data();
  if (vkCreateDescriptorSetLayout(device_, &lci, nullptr, &postDescLayout_) != VK_SUCCESS)
    return false;

  VkDescriptorPoolSize ps[2]{};
  ps[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  ps[0].descriptorCount = 3;
  ps[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  ps[1].descriptorCount = 1;
  VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pci.poolSizeCount = 2;
  pci.pPoolSizes = ps;
  pci.maxSets = 1;
  if (vkCreateDescriptorPool(device_, &pci, nullptr, &postDescPool_) != VK_SUCCESS) return false;

  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool = postDescPool_;
  ai.descriptorSetCount = 1;
  ai.pSetLayouts = &postDescLayout_;
  if (vkAllocateDescriptorSets(device_, &ai, &postDescSet_) != VK_SUCCESS) return false;

  VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  plci.setLayoutCount = 1;
  plci.pSetLayouts = &postDescLayout_;
  if (vkCreatePipelineLayout(device_, &plci, nullptr, &postPipelineLayout_) != VK_SUCCESS)
    return false;

  VkShaderModule vert = loadShaderModule("assets/shaders/post.vert.spv");
  VkShaderModule frag = loadShaderModule("assets/shaders/post.frag.spv");
  if (!vert || !frag) {
    if (vert) vkDestroyShaderModule(device_, vert, nullptr);
    if (frag) vkDestroyShaderModule(device_, frag, nullptr);
    return false;
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

  VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo ia{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo vs{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vs.viewportCount = 1;
  vs.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.f;
  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  ds.depthTestEnable = VK_FALSE;
  ds.depthWriteEnable = VK_FALSE;
  VkPipelineColorBlendAttachmentState blendAtt{};
  blendAtt.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cb.attachmentCount = 1;
  cb.pAttachments = &blendAtt;
  VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dyn.dynamicStateCount = 2;
  dyn.pDynamicStates = dynStates;

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
  gp.layout = postPipelineLayout_;
  gp.renderPass = postRenderPass_;
  gp.subpass = 0;
  const bool ok =
      vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &postPipeline_) ==
      VK_SUCCESS;
  vkDestroyShaderModule(device_, vert, nullptr);
  vkDestroyShaderModule(device_, frag, nullptr);
  if (!ok) logWarn("post pipeline create failed");
  return ok && postPipeline_ != VK_NULL_HANDLE;
}

void VulkanContext::updatePostDescriptors() {
  if (!postDescSet_ || !sceneView_ || !postSampler_ || !depthView_ || !historyView_) return;
  VkDescriptorImageInfo sceneImg{};
  sceneImg.sampler = postSampler_;
  sceneImg.imageView = sceneView_;
  sceneImg.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkDescriptorBufferInfo ubo{};
  ubo.buffer = postUbo_.buffer;
  ubo.offset = 0;
  ubo.range = sizeof(PostUBOData);
  VkDescriptorImageInfo depthImg{};
  depthImg.sampler = depthSampler_ ? depthSampler_ : postSampler_;
  depthImg.imageView = depthView_;
  depthImg.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkDescriptorImageInfo histImg{};
  histImg.sampler = postSampler_;
  histImg.imageView = historyView_;
  histImg.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkWriteDescriptorSet writes[4]{};
  for (int i = 0; i < 4; ++i) {
    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[i].dstSet = postDescSet_;
    writes[i].dstBinding = static_cast<uint32_t>(i);
    writes[i].descriptorCount = 1;
  }
  writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  writes[0].pImageInfo = &sceneImg;
  writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  writes[1].pBufferInfo = &ubo;
  writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  writes[2].pImageInfo = &depthImg;
  writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  writes[3].pImageInfo = &histImg;
  vkUpdateDescriptorSets(device_, 4, writes, 0, nullptr);
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
  out = {};
  VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bi.size = size;
  bi.usage = usage;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

#if defined(BOLT_USE_VMA)
  if (vmaAllocator_) {
    VmaAllocationCreateInfo aci{};
    const bool hostVisible =
        (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    if (hostVisible) {
      aci.usage = VMA_MEMORY_USAGE_AUTO;
      aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                  VMA_ALLOCATION_CREATE_MAPPED_BIT;
    } else {
      aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    }
    VmaAllocation alloc = nullptr;
    VmaAllocationInfo ainfo{};
    if (vmaCreateBuffer(static_cast<VmaAllocator>(vmaAllocator_), &bi, &aci, &out.buffer, &alloc,
                        &ainfo) != VK_SUCCESS) {
      logError("vmaCreateBuffer failed");
      return false;
    }
    out.vmaAllocation = alloc;
    out.mapped = ainfo.pMappedData;
    out.memory = VK_NULL_HANDLE;
    out.size = size;
    return true;
  }
#endif

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
#if defined(BOLT_USE_VMA)
  if (b.vmaAllocation && vmaAllocator_) {
    vmaDestroyBuffer(static_cast<VmaAllocator>(vmaAllocator_), b.buffer,
                     static_cast<VmaAllocation>(b.vmaAllocation));
    b = {};
    return;
  }
#endif
  if (b.buffer) vkDestroyBuffer(device_, b.buffer, nullptr);
  if (b.memory) vkFreeMemory(device_, b.memory, nullptr);
  b = {};
}

bool VulkanContext::mapBufferPersistent(GpuBuffer& b, void** outMapped) {
  if (!outMapped || !b.buffer) return false;
  if (b.mapped) {
    *outMapped = b.mapped;
    return true;
  }
#if defined(BOLT_USE_VMA)
  if (b.vmaAllocation && vmaAllocator_) {
    if (vmaMapMemory(static_cast<VmaAllocator>(vmaAllocator_),
                     static_cast<VmaAllocation>(b.vmaAllocation), outMapped) != VK_SUCCESS)
      return false;
    b.mapped = *outMapped;
    return true;
  }
#endif
  if (!b.memory) return false;
  if (vkMapMemory(device_, b.memory, 0, b.size ? b.size : VK_WHOLE_SIZE, 0, outMapped) !=
      VK_SUCCESS)
    return false;
  b.mapped = *outMapped;
  return true;
}

bool VulkanContext::copyToBuffer(GpuBuffer& dst, const void* data, VkDeviceSize size) {
  if (!dst.buffer || !data || size == 0) return false;
  if (dst.mapped) {
    std::memcpy(dst.mapped, data, static_cast<size_t>(size));
    return true;
  }
#if defined(BOLT_USE_VMA)
  if (dst.vmaAllocation && vmaAllocator_) {
    void* mapped = nullptr;
    if (vmaMapMemory(static_cast<VmaAllocator>(vmaAllocator_),
                     static_cast<VmaAllocation>(dst.vmaAllocation), &mapped) != VK_SUCCESS)
      return false;
    std::memcpy(mapped, data, static_cast<size_t>(size));
    vmaUnmapMemory(static_cast<VmaAllocator>(vmaAllocator_),
                   static_cast<VmaAllocation>(dst.vmaAllocation));
    return true;
  }
#endif
  if (!dst.memory) return false;
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
    if (!mapBufferPersistent(uniformBuffers_[i], &uniformMapped_[i])) return false;
  }
  // empty foliage + particle + crystal light buffers
  createBuffer(sizeof(FoliageInstanceGPU) * 64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               foliageInstanceBuf_);
  foliageCapacity_ = 64;
  createBuffer(sizeof(ParticleGPU) * 64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               particleBuf_);
  particleCapacity_ = 64;
  // Header (uvec4) + max lights
  const VkDeviceSize lightBytes =
      sizeof(uint32_t) * 4 + sizeof(CrystalLightGPU) * kMaxCrystalLights;
  createBuffer(lightBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               crystalLightBuf_);
  crystalLightCapacity_ = kMaxCrystalLights;
  crystalLightCount_ = 0;
  // Zero header so shaders see 0 lights until first upload
  std::vector<uint8_t> zero(static_cast<size_t>(lightBytes), 0);
  copyToBuffer(crystalLightBuf_, zero.data(), lightBytes);
  return true;
}

bool VulkanContext::createDescriptorPoolAndSets() {
  std::array<VkDescriptorPoolSize, 3> sizes{};
  sizes[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxFrames};
  // foliage instances + particles + crystal lights
  sizes[1] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kMaxFrames * 3};
  // materials (12) + bolt (3) + emissives (5) + shadow (1) per frame
  sizes[2] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxFrames * 25};
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
  // Teal albedo, flat normal, mid roughness, black emissive
  std::vector<uint8_t> alb = {40, 120, 150, 255};
  std::vector<uint8_t> nrm = {128, 128, 255, 255};
  std::vector<uint8_t> rgh = {160, 0, 128, 255}; // rough, metal0, height mid
  std::vector<uint8_t> emit = {0, 0, 0, 255};
  if (!createTextureFromRgba(alb, 1, 1, true, defaultAlbedo_)) return false;
  if (!createTextureFromRgba(nrm, 1, 1, false, defaultNormal_)) return false;
  if (!createTextureFromRgba(rgh, 1, 1, false, defaultRough_)) return false;
  if (!createTextureFromRgba(emit, 1, 1, true, defaultEmissive_)) return false;
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
  if (m.emissive.image) destroyTexture(m.emissive);
  m.hasEmissive = m.hasMetallic = m.hasHeight = false;
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
                                          GpuTexture& out, bool clampToEdge) {
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
  const VkSamplerAddressMode addr =
      clampToEdge ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
  si.addressModeU = addr;
  si.addressModeV = addr;
  si.addressModeW = addr;
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

    VkDescriptorBufferInfo particles{particleBuf_.buffer, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo lights{crystalLightBuf_.buffer, 0, VK_WHOLE_SIZE};
    const GpuTexture* bAlb =
        (boltFurValid_ && boltAlbedo_.view) ? &boltAlbedo_ : &defaultAlbedo_;
    const GpuTexture* bNrm =
        (boltFurValid_ && boltNormal_.view) ? &boltNormal_ : &defaultNormal_;
    const GpuTexture* bRgh =
        (boltFurValid_ && boltRough_.view) ? &boltRough_ : &defaultRough_;
    const GpuTexture* bEmit =
        (boltHasEmissive_ && boltEmissive_.view) ? &boltEmissive_ : &defaultEmissive_;
    VkDescriptorImageInfo iBAlb{bAlb->sampler, bAlb->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo iBNrm{bNrm->sampler, bNrm->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo iBRgh{bRgh->sampler, bRgh->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo iBEmit{bEmit->sampler, bEmit->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    auto emitInfo = [&](const MaterialGpu& m) -> VkDescriptorImageInfo {
      const GpuTexture* e =
          (m.hasEmissive && m.emissive.view) ? &m.emissive : &defaultEmissive_;
      return {e->sampler, e->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    };
    VkDescriptorImageInfo gEm = emitInfo(groundMat_);
    VkDescriptorImageInfo rEm = emitInfo(rockMat_);
    VkDescriptorImageInfo pEm = emitInfo(pathMat_);
    VkDescriptorImageInfo sEm = emitInfo(stalkMat_);

    VkDescriptorImageInfo iShadow{};
    if (shadowView_ && shadowSampler_) {
      iShadow = {shadowSampler_, shadowView_, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    } else {
      iShadow = {defaultRough_.sampler, defaultRough_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    }

    std::array<VkWriteDescriptorSet, 25> writes{};
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
    fill(14, 14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &particles, nullptr);
    fill(15, 15, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &iBAlb);
    fill(16, 16, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &iBNrm);
    fill(17, 17, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &iBRgh);
    fill(18, 18, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lights, nullptr);
    fill(19, 19, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &gEm);
    fill(20, 20, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &rEm);
    fill(21, 21, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &pEm);
    fill(22, 22, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &sEm);
    fill(23, 23, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &iBEmit);
    fill(24, 24, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &iShadow);
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
  }
}

bool VulkanContext::loadBoltFurPBR(const std::string& furBasePath) {
  if (device_ == VK_NULL_HANDLE || furBasePath.empty()) return false;
  ImageData alb, nrm, roughImg;
  if (!loadImage(furBasePath + "_albedo.png", alb)) return false;
  if (!loadImage(furBasePath + "_normal.png", nrm)) return false;
  if (!loadImage(furBasePath + "_roughness.png", roughImg)) return false;

  destroyTexture(boltAlbedo_);
  destroyTexture(boltNormal_);
  destroyTexture(boltRough_);
  destroyTexture(boltEmissive_);
  destroyTexture(boltHeight_);
  boltHasEmissive_ = false;
  boltHasHeight_ = false;

  // Pack R=rough G=metal B=height for fur
  ImageData metalImg, heightImg, emitImg;
  auto tryLoad = [](const std::string& p, ImageData& out) -> bool {
    return std::filesystem::exists(p) && loadImage(p, out);
  };
  const bool hasMetal = tryLoad(furBasePath + "_metallic.png", metalImg);
  const bool hasHeight = tryLoad(furBasePath + "_height.png", heightImg);
  const bool hasEmit = tryLoad(furBasePath + "_emissive.png", emitImg);

  // Premium normals + roughness (Sobel detail from height, contrast remap)
  enhancePbrMaps(nrm, roughImg, hasHeight ? &heightImg : nullptr, PbrSurface::Fur);

  if (!createTextureFromRgba(alb.pixels, alb.width, alb.height, true, boltAlbedo_)) return false;
  if (!createTextureFromRgba(nrm.pixels, nrm.width, nrm.height, false, boltNormal_)) return false;

  const int w = roughImg.width, h = roughImg.height;
  std::vector<uint8_t> packed(static_cast<size_t>(w * h * 4));
  for (int i = 0; i < w * h; ++i) {
    const size_t o = static_cast<size_t>(i) * 4;
    packed[o + 0] = roughImg.pixels[o];
    packed[o + 1] = hasMetal && metalImg.width == w
                        ? metalImg.pixels[o]
                        : static_cast<uint8_t>(0);
    packed[o + 2] = hasHeight && heightImg.width == w
                        ? heightImg.pixels[o]
                        : static_cast<uint8_t>(128);
    packed[o + 3] = 255;
  }
  if (!createTextureFromRgba(packed, w, h, false, boltRough_)) return false;

  if (hasEmit) {
    if (createTextureFromRgba(emitImg.pixels, emitImg.width, emitImg.height, true, boltEmissive_))
      boltHasEmissive_ = true;
  }
  if (hasHeight) {
    // Keep height also as standalone for stronger parallax sampling
    std::vector<uint8_t> hg(static_cast<size_t>(heightImg.width * heightImg.height));
    for (int i = 0; i < heightImg.width * heightImg.height; ++i)
      hg[static_cast<size_t>(i)] = heightImg.pixels[static_cast<size_t>(i) * 4];
    if (createTextureFromGrey(hg, heightImg.width, heightImg.height, boltHeight_))
      boltHasHeight_ = true;
  }

  boltFurValid_ = true;
  updateMaterialDescriptors();
  logInfo("Bolt fur PBR enhanced (normals+roughness/Sobel) " +
          std::string(boltHasEmissive_ ? "+emissive " : "") + furBasePath);
  return true;
}

bool VulkanContext::loadMaterialSet(const std::string& basePath, MaterialGpu& out) {
  if (device_ == VK_NULL_HANDLE || basePath.empty()) return false;
  ImageData alb, nrm, roughImg;
  if (!loadImage(basePath + "_albedo.png", alb)) return false;
  if (!loadImage(basePath + "_normal.png", nrm)) return false;
  if (!loadImage(basePath + "_roughness.png", roughImg)) return false;

  destroyMaterialOwned(out);

  ImageData metalImg, heightImg, emitImg;
  auto tryLoad = [](const std::string& p, ImageData& img) -> bool {
    return std::filesystem::exists(p) && loadImage(p, img);
  };
  out.hasMetallic = tryLoad(basePath + "_metallic.png", metalImg);
  out.hasHeight = tryLoad(basePath + "_height.png", heightImg);
  const bool hasEmit = tryLoad(basePath + "_emissive.png", emitImg);

  // Infer surface type from path for roughness bias
  PbrSurface surf = PbrSurface::Ground;
  if (basePath.find("rock") != std::string::npos)
    surf = PbrSurface::Rock;
  else if (basePath.find("path") != std::string::npos)
    surf = PbrSurface::Path;
  else if (basePath.find("stalk") != std::string::npos || basePath.find("foliage") != std::string::npos)
    surf = PbrSurface::Stalk;
  enhancePbrMaps(nrm, roughImg, out.hasHeight ? &heightImg : nullptr, surf);

  if (!createTextureFromRgba(alb.pixels, alb.width, alb.height, true, out.albedo)) return false;
  if (!createTextureFromRgba(nrm.pixels, nrm.width, nrm.height, false, out.normal)) return false;

  const int w = roughImg.width, h = roughImg.height;
  std::vector<uint8_t> packed(static_cast<size_t>(w * h * 4));
  for (int i = 0; i < w * h; ++i) {
    const size_t o = static_cast<size_t>(i) * 4;
    packed[o + 0] = roughImg.pixels[o]; // roughness (enhanced)
    packed[o + 1] = (out.hasMetallic && metalImg.width == w) ? metalImg.pixels[o] : uint8_t(0);
    packed[o + 2] = (out.hasHeight && heightImg.width == w) ? heightImg.pixels[o] : uint8_t(128);
    packed[o + 3] = 255;
  }
  if (!createTextureFromRgba(packed, w, h, false, out.roughness)) return false;

  if (hasEmit) {
    if (createTextureFromRgba(emitImg.pixels, emitImg.width, emitImg.height, true, out.emissive))
      out.hasEmissive = true;
  }

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
          " (enhanced normals+roughness; 1=ground 2=rock 4=path 8=stalk)");
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

bool VulkanContext::uploadMesh(GpuMesh& mesh, const std::vector<VertexPC>& verts,
                               const std::vector<uint32_t>& indices) {
  if (device_ == VK_NULL_HANDLE || verts.empty() || indices.empty()) return false;
  vkDeviceWaitIdle(device_);
  destroyBuffer(mesh.vertex);
  destroyBuffer(mesh.index);
  const VkDeviceSize vsize = sizeof(VertexPC) * verts.size();
  const VkDeviceSize isize = sizeof(uint32_t) * indices.size();
  if (!createBuffer(vsize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    mesh.vertex))
    return false;
  if (!createBuffer(isize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    mesh.index))
    return false;
  copyToBuffer(mesh.vertex, verts.data(), vsize);
  copyToBuffer(mesh.index, indices.data(), isize);
  mesh.vertexCount = static_cast<uint32_t>(verts.size());
  mesh.indexCount = static_cast<uint32_t>(indices.size());
  return true;
}

bool VulkanContext::uploadStalkMesh(const std::vector<VertexPC>& verts,
                                    const std::vector<uint32_t>& indices) {
  if (!uploadMesh(stalk_, verts, indices)) return false;
  logInfo("Stalk mesh GPU: " + std::to_string(stalk_.indexCount) + " indices");
  return true;
}

void VulkanContext::bindSsbo(uint32_t setIndex, const GpuBuffer& buf) {
  bindSsboBinding(setIndex, 1, buf);
}

void VulkanContext::bindSsboBinding(uint32_t setIndex, uint32_t binding, const GpuBuffer& buf) {
  if (setIndex >= descSets_.size() || !buf.buffer) return;
  VkDescriptorBufferInfo ssbo{buf.buffer, 0, VK_WHOLE_SIZE};
  VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  w.dstSet = descSets_[setIndex];
  w.dstBinding = binding;
  w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  w.descriptorCount = 1;
  w.pBufferInfo = &ssbo;
  vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
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
    for (int i = 0; i < kMaxFrames; ++i)
      bindSsboBinding(static_cast<uint32_t>(i), 1, foliageInstanceBuf_);
  }
  // Always keep culled SSBO + indirect cmds sized for GPU DrawIndirect path
  ensureCullBuffers(std::max(foliageCapacity_, foliageCount_));
  copyToBuffer(foliageInstanceBuf_, instances.data(),
               sizeof(FoliageInstanceGPU) * foliageCount_);
  return true;
}

void VulkanContext::ensureCullBuffers(uint32_t instanceCapacity) {
  if (device_ == VK_NULL_HANDLE || instanceCapacity == 0) return;
  // Culled instance SSBO (device-local preferred but host-visible OK for simplicity)
  if (foliageCulledBuf_.buffer == VK_NULL_HANDLE ||
      foliageCulledBuf_.size < sizeof(FoliageInstanceGPU) * instanceCapacity) {
    destroyBuffer(foliageCulledBuf_);
    createBuffer(sizeof(FoliageInstanceGPU) * instanceCapacity,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 foliageCulledBuf_);
  }
  if (cullParamBuf_.buffer == VK_NULL_HANDLE) {
    createBuffer(256, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 cullParamBuf_);
    mapBufferPersistent(cullParamBuf_, &cullParamMapped_);
  }
  if (indirectCmdBuf_.buffer == VK_NULL_HANDLE ||
      indirectCmdBuf_.size < sizeof(uint32_t) * 5 * kIndirectCmdSlots) {
    destroyBuffer(indirectCmdBuf_);
    indirectMapped_ = nullptr;
    createBuffer(sizeof(uint32_t) * 5 * kIndirectCmdSlots,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 indirectCmdBuf_);
    mapBufferPersistent(indirectCmdBuf_, &indirectMapped_);
  }
  // Update cull descriptor set
  if (cullDescSet_ && foliageInstanceBuf_.buffer && foliageCulledBuf_.buffer &&
      cullParamBuf_.buffer && indirectCmdBuf_.buffer) {
    VkDescriptorBufferInfo infos[4]{};
    infos[0] = {cullParamBuf_.buffer, 0, VK_WHOLE_SIZE};
    infos[1] = {foliageInstanceBuf_.buffer, 0, VK_WHOLE_SIZE};
    infos[2] = {foliageCulledBuf_.buffer, 0, VK_WHOLE_SIZE};
    infos[3] = {indirectCmdBuf_.buffer, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet writes[4]{};
    for (int i = 0; i < 4; ++i) {
      writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[i].dstSet = cullDescSet_;
      writes[i].dstBinding = static_cast<uint32_t>(i);
      writes[i].descriptorCount = 1;
      writes[i].descriptorType =
          (i == 0) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(device_, 4, writes, 0, nullptr);
  }
}

void VulkanContext::destroyDeferredResources() {
  if (gbufferFramebuffer_) {
    vkDestroyFramebuffer(device_, gbufferFramebuffer_, nullptr);
    gbufferFramebuffer_ = VK_NULL_HANDLE;
  }
  if (lightFramebuffer_) {
    vkDestroyFramebuffer(device_, lightFramebuffer_, nullptr);
    lightFramebuffer_ = VK_NULL_HANDLE;
  }
  if (forwardOverlayFb_) {
    vkDestroyFramebuffer(device_, forwardOverlayFb_, nullptr);
    forwardOverlayFb_ = VK_NULL_HANDLE;
  }
  auto killImg = [&](VkImage& img, VkDeviceMemory& mem, VkImageView& view) {
    if (view) {
      vkDestroyImageView(device_, view, nullptr);
      view = VK_NULL_HANDLE;
    }
    if (img) {
      vkDestroyImage(device_, img, nullptr);
      img = VK_NULL_HANDLE;
    }
    if (mem) {
      vkFreeMemory(device_, mem, nullptr);
      mem = VK_NULL_HANDLE;
    }
  };
  killImg(gAlbedoImage_, gAlbedoMemory_, gAlbedoView_);
  killImg(gNormalImage_, gNormalMemory_, gNormalView_);
  killImg(gEmitImage_, gEmitMemory_, gEmitView_);
  // Render passes destroyed only when device tears down (pipelines hold them)
}

bool VulkanContext::createDeferredResources() {
  if (!device_ || swapExtent_.width == 0) return false;
  destroyDeferredResources();

  auto makeRt = [&](VkImage& img, VkDeviceMemory& mem, VkImageView& view, VkFormat fmt) -> bool {
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = fmt;
    ii.extent = {swapExtent_.width, swapExtent_.height, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device_, &ii, nullptr, &img) != VK_SUCCESS) return false;
    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(device_, img, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device_, &ai, nullptr, &mem) != VK_SUCCESS) return false;
    vkBindImageMemory(device_, img, mem, 0);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = img;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    return vkCreateImageView(device_, &vi, nullptr, &view) == VK_SUCCESS;
  };

  const VkFormat gfmt = VK_FORMAT_R8G8B8A8_UNORM;
  if (!makeRt(gAlbedoImage_, gAlbedoMemory_, gAlbedoView_, gfmt)) return false;
  if (!makeRt(gNormalImage_, gNormalMemory_, gNormalView_, gfmt)) return false;
  if (!makeRt(gEmitImage_, gEmitMemory_, gEmitView_, gfmt)) return false;

  // GBuffer pass (create once)
  if (!gbufferRenderPass_) {
    auto colorAtt = [&](VkImageLayout finalL) {
      VkAttachmentDescription a{};
      a.format = gfmt;
      a.samples = VK_SAMPLE_COUNT_1_BIT;
      a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      a.finalLayout = finalL;
      return a;
    };
    VkAttachmentDescription depth{};
    depth.format = depthFormat_;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // Readable by deferred light pass after GBuffer
    depth.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkAttachmentDescription, 4> atts = {
        colorAtt(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
        colorAtt(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
        colorAtt(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), depth};
    std::array<VkAttachmentReference, 3> cols = {
        VkAttachmentReference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
    VkAttachmentReference depthRef{3, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 3;
    sub.pColorAttachments = cols.data();
    sub.pDepthStencilAttachment = &depthRef;
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dep.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    ci.attachmentCount = 4;
    ci.pAttachments = atts.data();
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    ci.dependencyCount = 1;
    ci.pDependencies = &dep;
    if (vkCreateRenderPass(device_, &ci, nullptr, &gbufferRenderPass_) != VK_SUCCESS) return false;
  }

  if (!lightRenderPass_) {
    VkAttachmentDescription color{};
    color.format = swapFormat_;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;
    VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    ci.attachmentCount = 1;
    ci.pAttachments = &color;
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    if (vkCreateRenderPass(device_, &ci, nullptr, &lightRenderPass_) != VK_SUCCESS) return false;
  }

  if (!forwardOverlayPass_) {
    VkAttachmentDescription color{};
    color.format = swapFormat_;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentDescription depth{};
    depth.format = depthFormat_;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    depth.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    std::array<VkAttachmentDescription, 2> atts = {color, depth};
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;
    sub.pDepthStencilAttachment = &depthRef;
    VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    ci.attachmentCount = 2;
    ci.pAttachments = atts.data();
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    if (vkCreateRenderPass(device_, &ci, nullptr, &forwardOverlayPass_) != VK_SUCCESS) return false;
  }

  {
    VkImageView atts[] = {gAlbedoView_, gNormalView_, gEmitView_, depthView_};
    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass = gbufferRenderPass_;
    fci.attachmentCount = 4;
    fci.pAttachments = atts;
    fci.width = swapExtent_.width;
    fci.height = swapExtent_.height;
    fci.layers = 1;
    if (vkCreateFramebuffer(device_, &fci, nullptr, &gbufferFramebuffer_) != VK_SUCCESS) return false;
  }
  {
    VkImageView att = sceneView_;
    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass = lightRenderPass_;
    fci.attachmentCount = 1;
    fci.pAttachments = &att;
    fci.width = swapExtent_.width;
    fci.height = swapExtent_.height;
    fci.layers = 1;
    if (vkCreateFramebuffer(device_, &fci, nullptr, &lightFramebuffer_) != VK_SUCCESS) return false;
  }
  {
    VkImageView atts[] = {sceneView_, depthView_};
    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass = forwardOverlayPass_;
    fci.attachmentCount = 2;
    fci.pAttachments = atts;
    fci.width = swapExtent_.width;
    fci.height = swapExtent_.height;
    fci.layers = 1;
    if (vkCreateFramebuffer(device_, &fci, nullptr, &forwardOverlayFb_) != VK_SUCCESS) return false;
  }
  return true;
}

bool VulkanContext::createDeferredPipelines() {
  if (!gbufferRenderPass_ || !lightRenderPass_ || !pipelineLayout_) return false;

  VkShaderModule vert = loadShaderModule("assets/shaders/terrain.vert.spv");
  VkShaderModule gfrag = loadShaderModule("assets/shaders/gbuffer_terrain.frag.spv");
  if (!vert || !gfrag) {
    if (vert) vkDestroyShaderModule(device_, vert, nullptr);
    if (gfrag) vkDestroyShaderModule(device_, gfrag, nullptr);
    return false;
  }

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = gfrag;
  stages[1].pName = "main";

  VkVertexInputBindingDescription bind{};
  bind.binding = 0;
  bind.stride = sizeof(VertexPC);
  bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  std::array<VkVertexInputAttributeDescription, 4> attrs{};
  attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexPC, pos)};
  attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexPC, normal)};
  attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VertexPC, uv)};
  attrs[3] = {3, 0, VK_FORMAT_R32_SFLOAT, offsetof(VertexPC, matId)};
  VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vi.vertexBindingDescriptionCount = 1;
  vi.pVertexBindingDescriptions = &bind;
  vi.vertexAttributeDescriptionCount = 4;
  vi.pVertexAttributeDescriptions = attrs.data();
  VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo vs{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vs.viewportCount = 1;
  vs.scissorCount = 1;
  std::array<VkDynamicState, 2> dynStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dyn.dynamicStateCount = 2;
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
  VkPipelineColorBlendAttachmentState blendAtt{};
  blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  std::array<VkPipelineColorBlendAttachmentState, 3> blend3 = {blendAtt, blendAtt, blendAtt};
  VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cb.attachmentCount = 3;
  cb.pAttachments = blend3.data();

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
  gp.renderPass = gbufferRenderPass_;
  gp.subpass = 0;
  const bool gbufOk =
      vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &gbufTerrainPipeline_) ==
      VK_SUCCESS;
  vkDestroyShaderModule(device_, vert, nullptr);
  vkDestroyShaderModule(device_, gfrag, nullptr);
  if (!gbufOk) return false;

  // Deferred light: 0 UBO, 1-4 GBuffer, 5 lights SSBO, 6 shadow map array (CSM)
  if (!deferredLightDescLayout_) {
    std::array<VkDescriptorSetLayoutBinding, 7> binds{};
    binds[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    for (uint32_t i = 1; i <= 4; ++i)
      binds[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                  nullptr};
    binds[5] = {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    binds[6] = {6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr};
    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.bindingCount = 7;
    lci.pBindings = binds.data();
    if (vkCreateDescriptorSetLayout(device_, &lci, nullptr, &deferredLightDescLayout_) != VK_SUCCESS)
      return false;
  }
  if (!deferredLightDescPool_) {
    std::array<VkDescriptorPoolSize, 3> ps{};
    ps[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
    ps[1] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5};
    ps[2] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.maxSets = 1;
    pci.poolSizeCount = 3;
    pci.pPoolSizes = ps.data();
    if (vkCreateDescriptorPool(device_, &pci, nullptr, &deferredLightDescPool_) != VK_SUCCESS)
      return false;
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = deferredLightDescPool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &deferredLightDescLayout_;
    if (vkAllocateDescriptorSets(device_, &ai, &deferredLightDescSet_) != VK_SUCCESS) return false;
  }
  if (!deferredLightLayout_) {
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &deferredLightDescLayout_;
    if (vkCreatePipelineLayout(device_, &plci, nullptr, &deferredLightLayout_) != VK_SUCCESS)
      return false;
  }

  VkShaderModule lv = loadShaderModule("assets/shaders/deferred_light.vert.spv");
  VkShaderModule lf = loadShaderModule("assets/shaders/deferred_light.frag.spv");
  if (!lv || !lf) {
    if (lv) vkDestroyShaderModule(device_, lv, nullptr);
    if (lf) vkDestroyShaderModule(device_, lf, nullptr);
    return false;
  }
  stages[0].module = lv;
  stages[1].module = lf;
  VkPipelineVertexInputStateCreateInfo viEmpty{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineDepthStencilStateCreateInfo dsOff{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  VkPipelineColorBlendStateCreateInfo cb1{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cb1.attachmentCount = 1;
  cb1.pAttachments = &blendAtt;
  gp.pVertexInputState = &viEmpty;
  gp.pDepthStencilState = &dsOff;
  gp.pColorBlendState = &cb1;
  gp.layout = deferredLightLayout_;
  gp.renderPass = lightRenderPass_;
  rs.cullMode = VK_CULL_MODE_NONE;
  gp.pRasterizationState = &rs;
  const bool lightOk =
      vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &deferredLightPipeline_) ==
      VK_SUCCESS;
  vkDestroyShaderModule(device_, lv, nullptr);
  vkDestroyShaderModule(device_, lf, nullptr);
  return lightOk;
}

void VulkanContext::updateDeferredDescriptors() {
  if (!deferredLightDescSet_ || !gAlbedoView_ || uniformBuffers_.empty()) return;
  VkDescriptorBufferInfo ubo{uniformBuffers_[0].buffer, 0, sizeof(FrameUBO)};
  // Use frame 0 UBO as template — drawFrame updates all frames; bind current in draw
  VkDescriptorImageInfo iAlb{postSampler_ ? postSampler_ : defaultAlbedo_.sampler, gAlbedoView_,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkDescriptorImageInfo iNrm{postSampler_ ? postSampler_ : defaultAlbedo_.sampler, gNormalView_,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkDescriptorImageInfo iDepth{depthSampler_ ? depthSampler_ : defaultAlbedo_.sampler, depthView_,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkDescriptorImageInfo iEmit{postSampler_ ? postSampler_ : defaultAlbedo_.sampler, gEmitView_,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  // Depth for deferred light is still DEPTH_ATTACHMENT after gbuffer — need transition or
  // sample as GENERAL. We transition in drawFrame before light. Descriptor uses SHADER_READ.
  VkDescriptorBufferInfo lights{crystalLightBuf_.buffer, 0, VK_WHOLE_SIZE};

  // CSM array for deferred sun shadows; fallback 2D default if shadows off
  VkDescriptorImageInfo iShadow{};
  if (shadowReady_ && shadowView_ && shadowSampler_) {
    iShadow = {shadowSampler_, shadowView_, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
  } else {
    // Valid placeholder (won't be sampled when shadowParams.z == 0)
    iShadow = {defaultRough_.sampler, defaultRough_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  }

  std::array<VkWriteDescriptorSet, 7> writes{};
  auto fill = [&](int i, uint32_t b, VkDescriptorType t, const void* buf,
                  const VkDescriptorImageInfo* img) {
    writes[static_cast<size_t>(i)].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[static_cast<size_t>(i)].dstSet = deferredLightDescSet_;
    writes[static_cast<size_t>(i)].dstBinding = b;
    writes[static_cast<size_t>(i)].descriptorType = t;
    writes[static_cast<size_t>(i)].descriptorCount = 1;
    if (buf) writes[static_cast<size_t>(i)].pBufferInfo =
                 static_cast<const VkDescriptorBufferInfo*>(buf);
    if (img) writes[static_cast<size_t>(i)].pImageInfo = img;
  };
  fill(0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &ubo, nullptr);
  fill(1, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &iAlb);
  fill(2, 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &iNrm);
  fill(3, 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &iDepth);
  fill(4, 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &iEmit);
  fill(5, 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lights, nullptr);
  fill(6, 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &iShadow);
  vkUpdateDescriptorSets(device_, 7, writes.data(), 0, nullptr);
}

void VulkanContext::destroyShadowResources() {
  for (uint32_t i = 0; i < kShadowCascades; ++i) {
    if (shadowFramebuffer_[i]) {
      vkDestroyFramebuffer(device_, shadowFramebuffer_[i], nullptr);
      shadowFramebuffer_[i] = VK_NULL_HANDLE;
    }
    if (shadowLayerView_[i]) {
      vkDestroyImageView(device_, shadowLayerView_[i], nullptr);
      shadowLayerView_[i] = VK_NULL_HANDLE;
    }
  }
  if (shadowSampler_) {
    vkDestroySampler(device_, shadowSampler_, nullptr);
    shadowSampler_ = VK_NULL_HANDLE;
  }
  if (shadowView_) {
    vkDestroyImageView(device_, shadowView_, nullptr);
    shadowView_ = VK_NULL_HANDLE;
  }
  if (shadowImage_) {
    vkDestroyImage(device_, shadowImage_, nullptr);
    shadowImage_ = VK_NULL_HANDLE;
  }
  if (shadowMemory_) {
    vkFreeMemory(device_, shadowMemory_, nullptr);
    shadowMemory_ = VK_NULL_HANDLE;
  }
}

bool VulkanContext::createShadowResources() {
  if (!device_) return false;
  destroyShadowResources();

  VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ii.imageType = VK_IMAGE_TYPE_2D;
  ii.format = VK_FORMAT_D32_SFLOAT;
  ii.extent = {kShadowMapSize, kShadowMapSize, 1};
  ii.mipLevels = 1;
  ii.arrayLayers = kShadowCascades;
  ii.samples = VK_SAMPLE_COUNT_1_BIT;
  ii.tiling = VK_IMAGE_TILING_OPTIMAL;
  ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (vkCreateImage(device_, &ii, nullptr, &shadowImage_) != VK_SUCCESS) return false;
  VkMemoryRequirements mr{};
  vkGetImageMemoryRequirements(device_, shadowImage_, &mr);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = mr.size;
  ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (vkAllocateMemory(device_, &ai, nullptr, &shadowMemory_) != VK_SUCCESS) return false;
  vkBindImageMemory(device_, shadowImage_, shadowMemory_, 0);

  // Full array view for sampling in lighting shaders
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = shadowImage_;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
  vi.format = VK_FORMAT_D32_SFLOAT;
  vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  vi.subresourceRange.levelCount = 1;
  vi.subresourceRange.baseArrayLayer = 0;
  vi.subresourceRange.layerCount = kShadowCascades;
  if (vkCreateImageView(device_, &vi, nullptr, &shadowView_) != VK_SUCCESS) return false;

  // Per-layer 2D views for cascade framebuffers
  for (uint32_t i = 0; i < kShadowCascades; ++i) {
    VkImageViewCreateInfo lvi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    lvi.image = shadowImage_;
    lvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    lvi.format = VK_FORMAT_D32_SFLOAT;
    lvi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    lvi.subresourceRange.levelCount = 1;
    lvi.subresourceRange.baseArrayLayer = i;
    lvi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &lvi, nullptr, &shadowLayerView_[i]) != VK_SUCCESS) return false;
  }

  VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  si.magFilter = VK_FILTER_LINEAR;
  si.minFilter = VK_FILTER_LINEAR;
  si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
  si.compareEnable = VK_FALSE;
  if (vkCreateSampler(device_, &si, nullptr, &shadowSampler_) != VK_SUCCESS) return false;

  if (!shadowRenderPass_) {
    VkAttachmentDescription depth{};
    depth.format = VK_FORMAT_D32_SFLOAT;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkAttachmentReference depthRef{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.pDepthStencilAttachment = &depthRef;
    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    ci.attachmentCount = 1;
    ci.pAttachments = &depth;
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    ci.dependencyCount = 2;
    ci.pDependencies = deps.data();
    if (vkCreateRenderPass(device_, &ci, nullptr, &shadowRenderPass_) != VK_SUCCESS) return false;
  }

  for (uint32_t i = 0; i < kShadowCascades; ++i) {
    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass = shadowRenderPass_;
    fci.attachmentCount = 1;
    fci.pAttachments = &shadowLayerView_[i];
    fci.width = kShadowMapSize;
    fci.height = kShadowMapSize;
    fci.layers = 1;
    if (vkCreateFramebuffer(device_, &fci, nullptr, &shadowFramebuffer_[i]) != VK_SUCCESS)
      return false;
  }
  return true;
}

bool VulkanContext::createShadowPipelines() {
  if (!shadowRenderPass_ || !pipelineLayout_) return false;
  VkShaderModule vMesh = loadShaderModule("assets/shaders/shadow.vert.spv");
  VkShaderModule vFol = loadShaderModule("assets/shaders/shadow_foliage.vert.spv");
  VkShaderModule frag = loadShaderModule("assets/shaders/shadow.frag.spv");
  if (!vMesh || !vFol || !frag) {
    if (vMesh) vkDestroyShaderModule(device_, vMesh, nullptr);
    if (vFol) vkDestroyShaderModule(device_, vFol, nullptr);
    if (frag) vkDestroyShaderModule(device_, frag, nullptr);
    return false;
  }

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vMesh;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag;
  stages[1].pName = "main";

  VkVertexInputBindingDescription bind{};
  bind.binding = 0;
  bind.stride = sizeof(VertexPC);
  bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  std::array<VkVertexInputAttributeDescription, 4> attrs{};
  attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexPC, pos)};
  attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexPC, normal)};
  attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VertexPC, uv)};
  attrs[3] = {3, 0, VK_FORMAT_R32_SFLOAT, offsetof(VertexPC, matId)};
  VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vi.vertexBindingDescriptionCount = 1;
  vi.pVertexBindingDescriptions = &bind;
  vi.vertexAttributeDescriptionCount = 4;
  vi.pVertexAttributeDescriptions = attrs.data();
  VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo vs{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vs.viewportCount = 1;
  vs.scissorCount = 1;
  std::array<VkDynamicState, 2> dynStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dyn.dynamicStateCount = 2;
  dyn.pDynamicStates = dynStates.data();
  VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_BACK_BIT;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.f;
  rs.depthBiasEnable = VK_TRUE;
  rs.depthBiasConstantFactor = 1.25f;
  rs.depthBiasSlopeFactor = 1.75f;
  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  ds.depthTestEnable = VK_TRUE;
  ds.depthWriteEnable = VK_TRUE;
  ds.depthCompareOp = VK_COMPARE_OP_LESS;
  VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cb.attachmentCount = 0;

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
  gp.renderPass = shadowRenderPass_;
  gp.subpass = 0;
  bool ok = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr,
                                      &shadowMeshPipeline_) == VK_SUCCESS;
  stages[0].module = vFol;
  ok = ok && vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr,
                                       &shadowFoliagePipeline_) == VK_SUCCESS;
  vkDestroyShaderModule(device_, vMesh, nullptr);
  vkDestroyShaderModule(device_, vFol, nullptr);
  vkDestroyShaderModule(device_, frag, nullptr);
  return ok;
}

void VulkanContext::renderShadowMap(VkCommandBuffer cmd, const FrameUBO& /*ubo*/,
                                    const SceneInstanceCounts& counts) {
  if (!shadowReady_ || !shadowMeshPipeline_ || !shadowFramebuffer_[0]) return;

  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1,
                          &descSets_[frameIndex_], 0, nullptr);

  for (uint32_t cascade = 0; cascade < kShadowCascades; ++cascade) {
    VkClearValue clear{};
    clear.depthStencil = {1.f, 0};
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = shadowRenderPass_;
    rp.framebuffer = shadowFramebuffer_[cascade];
    rp.renderArea.extent = {kShadowMapSize, kShadowMapSize};
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.width = static_cast<float>(kShadowMapSize);
    vp.height = static_cast<float>(kShadowMapSize);
    vp.maxDepth = 1.f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0, 0}, {kShadowMapSize, kShadowMapSize}};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    // color.w = cascade index for lightViewProj[cascade]
    ObjectPush identity{};
    identity.model = glm::mat4(1.f);
    identity.color = glm::vec4(1.f, 1.f, 1.f, static_cast<float>(cascade));
    identity.anim = glm::vec4(0.f);
    vkCmdPushConstants(cmd, pipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(ObjectPush), &identity);

    auto drawMesh = [&](GpuMesh& mesh) {
      if (mesh.indexCount == 0 || !mesh.vertex.buffer) return;
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowMeshPipeline_);
      VkDeviceSize off = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertex.buffer, &off);
      vkCmdBindIndexBuffer(cmd, mesh.index.buffer, 0, VK_INDEX_TYPE_UINT32);
      vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
    };
    drawMesh(terrain_);
    drawMesh(pathRibbon_);

    auto drawFol = [&](GpuMesh& mesh, uint32_t count, uint32_t first) {
      if (!shadowFoliagePipeline_ || mesh.indexCount == 0 || count == 0) return;
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowFoliagePipeline_);
      // Re-push cascade (pipeline bind may leave push intact, but keep explicit)
      vkCmdPushConstants(cmd, pipelineLayout_,
                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                         sizeof(ObjectPush), &identity);
      VkDeviceSize off = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertex.buffer, &off);
      vkCmdBindIndexBuffer(cmd, mesh.index.buffer, 0, VK_INDEX_TYPE_UINT32);
      vkCmdDrawIndexed(cmd, mesh.indexCount, count, 0, 0, first);
    };
    // Source counts (pre-cull) for stable shadows
    drawFol(stalk_, counts.stalkCount, counts.stalkFirst);
    drawFol(bush_, counts.bushCount, counts.bushFirst);
    for (int t = 0; t < kTreeTypes; ++t)
      drawFol(treeMeshes_[static_cast<size_t>(t)], counts.treeCount[t], counts.treeFirst[t]);
    // Skip dense detail on far cascade (noise / cost)
    if (cascade < 2)
      drawFol(detail_, counts.detailCount, counts.detailFirst);
    drawFol(ruin_, counts.ruinCount, counts.ruinFirst);
    drawFol(ruinArch_, counts.ruinArchCount, counts.ruinArchFirst);
    if (cascade < 2) {
      drawFol(ruinObs_, counts.ruinObsCount, counts.ruinObsFirst);
      drawFol(ruinTemple_, counts.ruinTempleCount, counts.ruinTempleFirst);
    }

    vkCmdEndRenderPass(cmd);
  }
}

bool VulkanContext::createCullPipeline() {
  // Descriptor layout: 0 UBO, 1 src SSBO, 2 dst SSBO, 3 indirect SSBO
  std::array<VkDescriptorSetLayoutBinding, 4> binds{};
  for (uint32_t i = 0; i < 4; ++i) {
    binds[i].binding = i;
    binds[i].descriptorCount = 1;
    binds[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    binds[i].descriptorType =
        (i == 0) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  }
  VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  lci.bindingCount = 4;
  lci.pBindings = binds.data();
  if (vkCreateDescriptorSetLayout(device_, &lci, nullptr, &cullDescLayout_) != VK_SUCCESS)
    return false;

  VkDescriptorPoolSize ps[2]{};
  ps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  ps[0].descriptorCount = 1;
  ps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  ps[1].descriptorCount = 3;
  VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pci.maxSets = 1;
  pci.poolSizeCount = 2;
  pci.pPoolSizes = ps;
  if (vkCreateDescriptorPool(device_, &pci, nullptr, &cullDescPool_) != VK_SUCCESS) return false;

  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool = cullDescPool_;
  ai.descriptorSetCount = 1;
  ai.pSetLayouts = &cullDescLayout_;
  if (vkAllocateDescriptorSets(device_, &ai, &cullDescSet_) != VK_SUCCESS) return false;

  VkPushConstantRange pcr{};
  pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  pcr.offset = 0;
  pcr.size = sizeof(uint32_t) * 4;

  VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  plci.setLayoutCount = 1;
  plci.pSetLayouts = &cullDescLayout_;
  plci.pushConstantRangeCount = 1;
  plci.pPushConstantRanges = &pcr;
  if (vkCreatePipelineLayout(device_, &plci, nullptr, &cullPipelineLayout_) != VK_SUCCESS)
    return false;

  VkShaderModule comp = loadShaderModule("assets/shaders/cull.comp.spv");
  if (!comp) return false;

  VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  stage.module = comp;
  stage.pName = "main";

  VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  ci.stage = stage;
  ci.layout = cullPipelineLayout_;
  const bool ok =
      vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr, &cullPipeline_) ==
      VK_SUCCESS;
  vkDestroyShaderModule(device_, comp, nullptr);
  if (!ok) return false;

  ensureCullBuffers(std::max(foliageCapacity_, 256u));
  return true;
}

namespace {
void extractFrustumPlanes(const glm::mat4& vp, glm::vec4 planes[6]) {
  // Gribb-Hartmann from clip matrix (column-major glm)
  const glm::mat4 m = glm::transpose(vp);
  planes[0] = m[3] + m[0]; // left
  planes[1] = m[3] - m[0]; // right
  planes[2] = m[3] + m[1]; // bottom
  planes[3] = m[3] - m[1]; // top
  planes[4] = m[3] + m[2]; // near
  planes[5] = m[3] - m[2]; // far
  for (int i = 0; i < 6; ++i) {
    const float len = glm::length(glm::vec3(planes[i]));
    if (len > 1e-6f) planes[i] /= len;
  }
}
} // namespace

void VulkanContext::dispatchGpuCull(VkCommandBuffer cmd, const FrameUBO& ubo,
                                    const SceneInstanceCounts& counts) {
  if (!cullPipeline_ || !cullDescSet_ || !indirectMapped_ || !cullParamMapped_) return;
  if (foliageCount_ == 0 || foliageInstanceBuf_.buffer == VK_NULL_HANDLE) return;
  ensureCullBuffers(std::max(foliageCapacity_, foliageCount_));
  if (!foliageCulledBuf_.buffer || !indirectCmdBuf_.buffer) return;

  struct CullParamsCPU {
    glm::mat4 viewProj;
    glm::vec4 camPos_params;
    glm::vec4 frustum[6];
    glm::vec4 lodParams;
  } params{};
  params.viewProj = ubo.viewProj;
  const float speedN = std::clamp(ubo.sprintScore_flags.z / 300.f, 0.f, 1.f);
  params.camPos_params = glm::vec4(ubo.cameraPos_time.x, ubo.cameraPos_time.y, ubo.cameraPos_time.z,
                                   speedN);
  extractFrustumPlanes(ubo.viewProj, params.frustum);
  params.lodParams = glm::vec4(220.f, 1.f, 0.25f, 0.f);
  std::memcpy(cullParamMapped_, &params, sizeof(params));

  struct Batch {
    uint32_t first, count, indexCount;
  };
  Batch batches[kIndirectBatches] = {};
  batches[0] = {counts.stalkFirst, counts.stalkCount, stalk_.indexCount};
  batches[1] = {counts.bushFirst, counts.bushCount, bush_.indexCount};
  for (int t = 0; t < kTreeTypes; ++t) {
    batches[2 + t] = {counts.treeFirst[t], counts.treeCount[t],
                      treeMeshes_[static_cast<size_t>(t)].indexCount};
  }
  batches[12] = {counts.detailFirst, counts.detailCount, detail_.indexCount};
  batches[13] = {counts.ruinFirst, counts.ruinCount, ruin_.indexCount};
  batches[14] = {counts.ruinArchFirst, counts.ruinArchCount, ruinArch_.indexCount};
  batches[15] = {counts.ruinObsFirst, counts.ruinObsCount, ruinObs_.indexCount};
  batches[16] = {counts.ruinTempleFirst, counts.ruinTempleCount, ruinTemple_.indexCount};

  // Pre-init 16 indirect cmds: 0..7 foliage meshes, 8..15 contact-shadow blobs
  auto* ind = static_cast<uint32_t*>(indirectMapped_);
  const uint32_t blobIdx = blob_.indexCount;
  for (uint32_t b = 0; b < kIndirectBatches; ++b) {
    // Foliage mesh draw
    ind[b * 5 + 0] = batches[b].indexCount;
    ind[b * 5 + 1] = 0; // instanceCount — compute atomics
    ind[b * 5 + 2] = 0;
    ind[b * 5 + 3] = 0;
    ind[b * 5 + 4] = batches[b].first;
    // Matching blob shadow draw (same firstInstance / surviving instances)
    const uint32_t bb = b + kIndirectBatches;
    ind[bb * 5 + 0] = blobIdx;
    ind[bb * 5 + 1] = 0;
    ind[bb * 5 + 2] = 0;
    ind[bb * 5 + 3] = 0;
    ind[bb * 5 + 4] = batches[b].first;
  }

  // Host wrote UBO + indirect zeros → compute must see them
  VkMemoryBarrier hostMb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  hostMb.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
  hostMb.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT |
                         VK_ACCESS_SHADER_WRITE_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                       &hostMb, 0, nullptr, 0, nullptr);

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullPipeline_);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullPipelineLayout_, 0, 1,
                          &cullDescSet_, 0, nullptr);

  for (uint32_t b = 0; b < kIndirectBatches; ++b) {
    if (batches[b].count == 0 || batches[b].indexCount == 0) continue;
    uint32_t push[4] = {b, batches[b].first, batches[b].count, batches[b].indexCount};
    vkCmdPushConstants(cmd, cullPipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push),
                       push);
    const uint32_t groups = (batches[b].count + 63u) / 64u;
    vkCmdDispatch(cmd, groups, 1, 1);
  }

  // Compute compact + instance counts → vertex SSBO + DrawIndirect
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0,
                       1, &mb, 0, nullptr, 0, nullptr);

  // Graphics draws this frame read compacted instances
  bindSsboBinding(frameIndex_, 1, foliageCulledBuf_);
}

bool VulkanContext::uploadCrystalLights(const std::vector<CrystalLightGPU>& lights) {
  if (device_ == VK_NULL_HANDLE || !crystalLightBuf_.buffer) return false;
  crystalLightCount_ = static_cast<uint32_t>(
      std::min(lights.size(), static_cast<size_t>(kMaxCrystalLights)));

  // std430: uvec4 header (count + pad) then CrystalLightGPU[]
  const VkDeviceSize headerBytes = sizeof(uint32_t) * 4;
  const VkDeviceSize bodyBytes = sizeof(CrystalLightGPU) * crystalLightCount_;
  const VkDeviceSize total = headerBytes + sizeof(CrystalLightGPU) * kMaxCrystalLights;
  std::vector<uint8_t> pack(static_cast<size_t>(total), 0);
  auto* hdr = reinterpret_cast<uint32_t*>(pack.data());
  hdr[0] = crystalLightCount_;
  if (crystalLightCount_ > 0) {
    std::memcpy(pack.data() + headerBytes, lights.data(), static_cast<size_t>(bodyBytes));
  }
  if (!copyToBuffer(crystalLightBuf_, pack.data(), total)) return false;
  for (int i = 0; i < kMaxFrames; ++i)
    bindSsboBinding(static_cast<uint32_t>(i), 18, crystalLightBuf_);
  return true;
}

bool VulkanContext::uploadParticles(const std::vector<ParticleGPU>& particles) {
  if (device_ == VK_NULL_HANDLE) return false;
  particleCount_ = static_cast<uint32_t>(particles.size());
  if (particleCount_ == 0) return true;
  if (particleCount_ > particleCapacity_) {
    vkDeviceWaitIdle(device_);
    destroyBuffer(particleBuf_);
    const uint32_t cap = std::max(particleCount_ * 2, 256u);
    createBuffer(sizeof(ParticleGPU) * cap, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 particleBuf_);
    particleCapacity_ = cap;
    for (int i = 0; i < kMaxFrames; ++i)
      bindSsboBinding(static_cast<uint32_t>(i), 14, particleBuf_);
  }
  copyToBuffer(particleBuf_, particles.data(), sizeof(ParticleGPU) * particleCount_);
  return true;
}

bool VulkanContext::uploadBlobMesh(const std::vector<VertexPC>& verts,
                                   const std::vector<uint32_t>& indices) {
  if (device_ == VK_NULL_HANDLE) return false;
  vkDeviceWaitIdle(device_);
  destroyBuffer(blob_.vertex);
  destroyBuffer(blob_.index);
  const VkDeviceSize vsize = sizeof(VertexPC) * verts.size();
  const VkDeviceSize isize = sizeof(uint32_t) * indices.size();
  if (!createBuffer(vsize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    blob_.vertex))
    return false;
  if (!createBuffer(isize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    blob_.index))
    return false;
  copyToBuffer(blob_.vertex, verts.data(), vsize);
  copyToBuffer(blob_.index, indices.data(), isize);
  blob_.vertexCount = static_cast<uint32_t>(verts.size());
  blob_.indexCount = static_cast<uint32_t>(indices.size());
  return true;
}

bool VulkanContext::uploadBoltMesh(const std::vector<VertexPC>& verts,
                                   const std::vector<uint32_t>& indices) {
  return uploadBoltPart(0, verts, indices);
}

bool VulkanContext::uploadBoltPart(int partIndex, const std::vector<VertexPC>& verts,
                                   const std::vector<uint32_t>& indices) {
  if (device_ == VK_NULL_HANDLE) return false;
  if (partIndex < 0 || partIndex >= kBoltPartCount) return false;
  if (verts.empty() || indices.empty()) return false;
  vkDeviceWaitIdle(device_);
  GpuMesh& mesh = boltParts_[partIndex];
  destroyBuffer(mesh.vertex);
  destroyBuffer(mesh.index);
  const VkDeviceSize vsize = sizeof(VertexPC) * verts.size();
  const VkDeviceSize isize = sizeof(uint32_t) * indices.size();
  if (!createBuffer(vsize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    mesh.vertex))
    return false;
  if (!createBuffer(isize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    mesh.index))
    return false;
  copyToBuffer(mesh.vertex, verts.data(), vsize);
  copyToBuffer(mesh.index, indices.data(), isize);
  mesh.vertexCount = static_cast<uint32_t>(verts.size());
  mesh.indexCount = static_cast<uint32_t>(indices.size());
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
  if (deferredReady_) {
    if (!createDeferredResources()) {
      deferredReady_ = false;
      logWarn("Deferred resources lost after resize");
    } else {
      updateDeferredDescriptors();
    }
  }
  updatePostDescriptors();
  framebufferResized_ = false;
  logInfo("Swapchain recreated " + std::to_string(swapExtent_.width) + "x" +
          std::to_string(swapExtent_.height));
  return true;
}

void VulkanContext::drawFrame(const FrameUBO& ubo, const SceneInstanceCounts& counts,
                              const ObjectPush* boltParts, int boltPartCount,
                              uint32_t particleCount) {
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

  // GPU frustum/distance cull → compact SSBO + DrawIndexedIndirect counts
  if (gpuCullEnabled_ && cullPipeline_ && foliageCount_ > 0) {
    ensureCullBuffers(std::max(foliageCapacity_, foliageCount_));
  }
  const bool useGpuCull = gpuCullEnabled_ && cullPipeline_ && foliageCount_ > 0 &&
                          foliageCulledBuf_.buffer != VK_NULL_HANDLE &&
                          indirectCmdBuf_.buffer != VK_NULL_HANDLE;
  if (useGpuCull) {
    dispatchGpuCull(cmd, ubo, counts);
  } else if (foliageInstanceBuf_.buffer) {
    bindSsboBinding(frameIndex_, 1, foliageInstanceBuf_);
  }

  // Sun shadow map (uses source instance buffer / culled after rebind)
  if (shadowReady_ && ubo.shadowParams.z > 0.5f) {
    // Shadows use packed instances (binding 1) — prefer source for stability
    if (foliageInstanceBuf_.buffer)
      bindSsboBinding(frameIndex_, 1, foliageInstanceBuf_);
    renderShadowMap(cmd, ubo, counts);
    if (useGpuCull && foliageCulledBuf_.buffer)
      bindSsboBinding(frameIndex_, 1, foliageCulledBuf_);
  }

  if (!sceneFramebuffer_) return;

  VkViewport vp{};
  vp.width = static_cast<float>(swapExtent_.width);
  vp.height = static_cast<float>(swapExtent_.height);
  vp.maxDepth = 1.f;
  VkRect2D sc{{0, 0}, swapExtent_};

  const bool useDeferred = deferredReady_ && gbufTerrainPipeline_ && deferredLightPipeline_ &&
                           gbufferFramebuffer_ && lightFramebuffer_ && forwardOverlayFb_;

  if (useDeferred) {
    // —— Pass 1: GBuffer (terrain + path) ——
    VkClearValue gclears[4]{};
    gclears[0].color = {{0.f, 0.f, 0.f, 0.f}};
    gclears[1].color = {{0.5f, 0.5f, 1.f, 1.f}};
    gclears[2].color = {{0.f, 0.f, 0.f, 0.f}};
    gclears[3].depthStencil = {1.f, 0};
    VkRenderPassBeginInfo grp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    grp.renderPass = gbufferRenderPass_;
    grp.framebuffer = gbufferFramebuffer_;
    grp.renderArea.extent = swapExtent_;
    grp.clearValueCount = 4;
    grp.pClearValues = gclears;
    vkCmdBeginRenderPass(cmd, &grp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1,
                            &descSets_[frameIndex_], 0, nullptr);
    if (terrain_.indexCount > 0) {
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gbufTerrainPipeline_);
      VkDeviceSize off = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &terrain_.vertex.buffer, &off);
      vkCmdBindIndexBuffer(cmd, terrain_.index.buffer, 0, VK_INDEX_TYPE_UINT32);
      vkCmdDrawIndexed(cmd, terrain_.indexCount, 1, 0, 0, 0);
    }
    if (pathRibbon_.indexCount > 0) {
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gbufTerrainPipeline_);
      VkDeviceSize off = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &pathRibbon_.vertex.buffer, &off);
      vkCmdBindIndexBuffer(cmd, pathRibbon_.index.buffer, 0, VK_INDEX_TYPE_UINT32);
      vkCmdDrawIndexed(cmd, pathRibbon_.indexCount, 1, 0, 0, 0);
    }
    vkCmdEndRenderPass(cmd);

    // —— Pass 2: deferred lighting → scene color (sky for empty depth) ——
    // Refresh light UBO binding for this frame
    {
      VkDescriptorBufferInfo uboInfo{uniformBuffers_[frameIndex_].buffer, 0, sizeof(FrameUBO)};
      VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      w.dstSet = deferredLightDescSet_;
      w.dstBinding = 0;
      w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      w.descriptorCount = 1;
      w.pBufferInfo = &uboInfo;
      vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
    }
    VkClearValue lclear{};
    lclear.color = {{0.02f, 0.03f, 0.08f, 1.f}};
    VkRenderPassBeginInfo lrp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    lrp.renderPass = lightRenderPass_;
    lrp.framebuffer = lightFramebuffer_;
    lrp.renderArea.extent = swapExtent_;
    lrp.clearValueCount = 1;
    lrp.pClearValues = &lclear;
    vkCmdBeginRenderPass(cmd, &lrp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, deferredLightPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, deferredLightLayout_, 0, 1,
                            &deferredLightDescSet_, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    // —— Pass 3: forward overlay (foliage, bolt, particles) ——
    VkRenderPassBeginInfo orp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    orp.renderPass = forwardOverlayPass_;
    orp.framebuffer = forwardOverlayFb_;
    orp.renderArea.extent = swapExtent_;
    orp.clearValueCount = 0;
    vkCmdBeginRenderPass(cmd, &orp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1,
                            &descSets_[frameIndex_], 0, nullptr);
  } else {
    // —— Classic forward path ——
    VkClearValue clears[2]{};
    clears[0].color = {{0.02f, 0.03f, 0.08f, 1.f}};
    clears[1].depthStencil = {1.f, 0};
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = renderPass_;
    rp.framebuffer = sceneFramebuffer_;
    rp.renderArea.extent = swapExtent_;
    rp.clearValueCount = 2;
    rp.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1,
                            &descSets_[frameIndex_], 0, nullptr);

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
    if (terrainPipeline_ && pathRibbon_.indexCount > 0) {
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipeline_);
      VkDeviceSize off = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &pathRibbon_.vertex.buffer, &off);
      vkCmdBindIndexBuffer(cmd, pathRibbon_.index.buffer, 0, VK_INDEX_TYPE_UINT32);
      vkCmdDrawIndexed(cmd, pathRibbon_.indexCount, 1, 0, 0, 0);
    }
  }

  const uint32_t totalInst =
      counts.stalkCount + counts.bushCount + counts.tallCount + counts.detailCount + counts.ruinCount +
      counts.ruinArchCount + counts.ruinObsCount + counts.ruinTempleCount;

  // Soft contact shadows + foliage: GPU indirect when cull is active
  auto drawBlobs = [&](uint32_t count, uint32_t first) {
    if (!blobPipeline_ || count == 0 || blob_.indexCount == 0) return;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blobPipeline_);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &blob_.vertex.buffer, &off);
    vkCmdBindIndexBuffer(cmd, blob_.index.buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, blob_.indexCount, count, 0, 0, first);
  };
  auto drawInstanced = [&](GpuMesh& mesh, uint32_t count, uint32_t first) {
    if (!foliagePipeline_ || count == 0 || mesh.indexCount == 0) return;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, foliagePipeline_);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertex.buffer, &off);
    vkCmdBindIndexBuffer(cmd, mesh.index.buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, mesh.indexCount, count, 0, 0, first);
  };
  auto drawIndirectMesh = [&](GpuMesh& mesh, uint32_t slot, uint32_t cpuCount, bool isBlob) {
    if (mesh.indexCount == 0 || cpuCount == 0 || !indirectCmdBuf_.buffer) return;
    if (isBlob) {
      if (!blobPipeline_) return;
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blobPipeline_);
    } else {
      if (!foliagePipeline_) return;
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, foliagePipeline_);
    }
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertex.buffer, &off);
    vkCmdBindIndexBuffer(cmd, mesh.index.buffer, 0, VK_INDEX_TYPE_UINT32);
    const VkDeviceSize cmdOff = sizeof(uint32_t) * 5 * slot;
    vkCmdDrawIndexedIndirect(cmd, indirectCmdBuf_.buffer, cmdOff, 1, sizeof(uint32_t) * 5);
  };

  if (useGpuCull) {
    // Mesh slots 0..16, blob slots 17..33 (mirrored instance counts)
    drawIndirectMesh(blob_, 17, counts.stalkCount, true);
    drawIndirectMesh(blob_, 18, counts.bushCount, true);
    for (int t = 0; t < kTreeTypes; ++t)
      drawIndirectMesh(blob_, 19 + t, counts.treeCount[t], true);
    drawIndirectMesh(blob_, 30, counts.ruinCount, true);
    drawIndirectMesh(blob_, 31, counts.ruinArchCount, true);
    drawIndirectMesh(blob_, 32, counts.ruinObsCount, true);
    drawIndirectMesh(blob_, 33, counts.ruinTempleCount, true);

    drawIndirectMesh(stalk_, 0, counts.stalkCount, false);
    drawIndirectMesh(bush_, 1, counts.bushCount, false);
    for (int t = 0; t < kTreeTypes; ++t)
      drawIndirectMesh(treeMeshes_[static_cast<size_t>(t)], 2 + t, counts.treeCount[t], false);
    drawIndirectMesh(detail_, 12, counts.detailCount, false);
    drawIndirectMesh(ruin_, 13, counts.ruinCount, false);
    drawIndirectMesh(ruinArch_, 14, counts.ruinArchCount, false);
    drawIndirectMesh(ruinObs_, 15, counts.ruinObsCount, false);
    drawIndirectMesh(ruinTemple_, 16, counts.ruinTempleCount, false);
  } else {
    drawBlobs(counts.stalkCount, counts.stalkFirst);
    drawBlobs(counts.bushCount, counts.bushFirst);
    for (int t = 0; t < kTreeTypes; ++t)
      drawBlobs(counts.treeCount[t], counts.treeFirst[t]);
    drawBlobs(counts.ruinCount, counts.ruinFirst);
    drawBlobs(counts.ruinArchCount, counts.ruinArchFirst);
    drawBlobs(counts.ruinObsCount, counts.ruinObsFirst);
    drawBlobs(counts.ruinTempleCount, counts.ruinTempleFirst);
    drawInstanced(stalk_, counts.stalkCount, counts.stalkFirst);
    drawInstanced(bush_, counts.bushCount, counts.bushFirst);
    for (int t = 0; t < kTreeTypes; ++t)
      drawInstanced(treeMeshes_[static_cast<size_t>(t)], counts.treeCount[t], counts.treeFirst[t]);
    drawInstanced(detail_, counts.detailCount, counts.detailFirst);
    drawInstanced(ruin_, counts.ruinCount, counts.ruinFirst);
    drawInstanced(ruinArch_, counts.ruinArchCount, counts.ruinArchFirst);
    drawInstanced(ruinObs_, counts.ruinObsCount, counts.ruinObsFirst);
    drawInstanced(ruinTemple_, counts.ruinTempleCount, counts.ruinTempleFirst);
  }
  (void)totalInst;

  // Multi-part Bolt GSD (body, legs, tail, aura)
  if (boltPipeline_ && boltParts && boltPartCount > 0) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, boltPipeline_);
    const int n = std::min(boltPartCount, kBoltPartCount);
    for (int i = 0; i < n; ++i) {
      if (boltParts_[i].indexCount == 0) continue;
      // Draw aura (part 6) last for better blending
      if (i == 6) continue;
      vkCmdPushConstants(cmd, pipelineLayout_,
                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                         sizeof(ObjectPush), &boltParts[i]);
      VkDeviceSize off = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &boltParts_[i].vertex.buffer, &off);
      vkCmdBindIndexBuffer(cmd, boltParts_[i].index.buffer, 0, VK_INDEX_TYPE_UINT32);
      vkCmdDrawIndexed(cmd, boltParts_[i].indexCount, 1, 0, 0, 0);
    }
    // Aura shell last
    if (n > 6 && boltParts_[6].indexCount > 0) {
      vkCmdPushConstants(cmd, pipelineLayout_,
                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                         sizeof(ObjectPush), &boltParts[6]);
      VkDeviceSize off = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &boltParts_[6].vertex.buffer, &off);
      vkCmdBindIndexBuffer(cmd, boltParts_[6].index.buffer, 0, VK_INDEX_TYPE_UINT32);
      vkCmdDrawIndexed(cmd, boltParts_[6].indexCount, 1, 0, 0, 0);
    }
  }

  // Dust / trail particles (binding 14 SSBO)
  if (particlePipeline_ && particleCount > 0 && blob_.indexCount > 0 && particleBuf_.buffer) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipeline_);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &blob_.vertex.buffer, &off);
    vkCmdBindIndexBuffer(cmd, blob_.index.buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, blob_.indexCount, particleCount, 0, 0, 0);
  }

  vkCmdEndRenderPass(cmd);

  // Post: TAA + motion blur + bloom + ACES (scene+depth+history → swapchain)
  if (postPipeline_ && postRenderPass_ && imageIndex < postFramebuffers_.size() &&
      postFramebuffers_[imageIndex] && postDescSet_) {
    if (postUboMapped_) {
      PostUBOData pubo{};
      const float invW =
          swapExtent_.width > 0 ? 1.f / static_cast<float>(swapExtent_.width) : 0.001f;
      const float invH =
          swapExtent_.height > 0 ? 1.f / static_cast<float>(swapExtent_.height) : 0.001f;
      pubo.res_time = glm::vec4(invW, invH, static_cast<float>(postTime_), postScore_);
      // Motion blur scales with camera speed (sprint); TAA history weight
      const float mb = std::clamp(postCamSpeed_ / 220.f, 0.f, 1.f);
      const float taaH = historyValid_ ? 0.88f : 0.0f;
      pubo.near_far_mb_taa = glm::vec4(0.12f, 520.f, mb, taaH);
      pubo.invViewProj = postInvViewProj_;
      pubo.prevViewProj = postPrevViewProj_;
      pubo.jitter = postJitter_;
      // God rays + score grade (set from Application via setSunScreen)
      pubo.sun_ray = glm::vec4(postSunUv_.x, postSunUv_.y, postGodRay_, postGrade_);
      std::memcpy(postUboMapped_, &pubo, sizeof(pubo));
    }
    VkRenderPassBeginInfo postRp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    postRp.renderPass = postRenderPass_;
    postRp.framebuffer = postFramebuffers_[imageIndex];
    postRp.renderArea.extent = swapExtent_;
    vkCmdBeginRenderPass(cmd, &postRp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipelineLayout_, 0, 1,
                            &postDescSet_, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    // Copy current scene → history for next frame TAA
    if (historyImage_ && sceneImage_) {
      VkImageMemoryBarrier barriers[2]{};
      for (int i = 0; i < 2; ++i) {
        barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barriers[i].subresourceRange.levelCount = 1;
        barriers[i].subresourceRange.layerCount = 1;
      }
      // scene: SHADER_READ → TRANSFER_SRC
      barriers[0].image = sceneImage_;
      barriers[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      barriers[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
      barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      // history: any → TRANSFER_DST
      barriers[1].image = historyImage_;
      barriers[1].oldLayout = historyValid_ ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                            : VK_IMAGE_LAYOUT_UNDEFINED;
      barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      barriers[1].srcAccessMask = historyValid_ ? VK_ACCESS_SHADER_READ_BIT : 0;
      barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0, 0, nullptr, 0, nullptr, 2, barriers);

      VkImageCopy region{};
      region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      region.srcSubresource.layerCount = 1;
      region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      region.dstSubresource.layerCount = 1;
      region.extent = {swapExtent_.width, swapExtent_.height, 1};
      vkCmdCopyImage(cmd, sceneImage_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, historyImage_,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

      // history → SHADER_READ; scene back to SHADER_READ for next descriptors
      barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      barriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           0, 0, nullptr, 0, nullptr, 2, barriers);
      historyValid_ = true;
    }
  }

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
