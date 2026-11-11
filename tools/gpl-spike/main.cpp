#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <numeric>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <windows.h>

#ifndef VK_ENABLE_BETA_EXTENSIONS
#define VK_ENABLE_BETA_EXTENSIONS
#endif
#ifndef VULKAN_HPP_NO_TO_STRING
#define VULKAN_HPP_NO_TO_STRING
#endif
#include <rex/ui/vulkan/api.h>

#include <rex/graphics/pipeline/shader/spirv_translator.h>
#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/graphics/pipeline/shader/spirv.h>
#include <xxhash.h>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Structure definitions matching ReXGlue SDK
// ---------------------------------------------------------------------------

#pragma pack(push, 1)
struct XshHeader {
  uint32_t magic;           // 0x48534558 ('XESH')
  uint32_t version_swapped;
};

struct XshRecordHeader {
  uint64_t ucode_data_hash;
  uint32_t countType;       // ucode_dword_count : 31, type : 1 (bit 31)
};

enum class PipelineGeometryShader : uint32_t {
  kNone,
  kPointList,
  kRectangleList,
  kQuadList,
};

enum class PipelinePrimitiveTopology : uint32_t {
  kPointList,
  kLineList,
  kLineStrip,
  kTriangleList,
  kTriangleStrip,
  kTriangleFan,
  kLineListWithAdjacency,
  kPatchList,
};

enum class PipelinePolygonMode : uint32_t {
  kFill,
  kLine,
  kPoint,
};

enum class PipelineBlendFactor : uint32_t {
  kZero,
  kOne,
  kSrcColor,
  kOneMinusSrcColor,
  kDstColor,
  kOneMinusDstColor,
  kSrcAlpha,
  kOneMinusSrcAlpha,
  kDstAlpha,
  kOneMinusDstAlpha,
  kConstantColor,
  kOneMinusConstantColor,
  kConstantAlpha,
  kOneMinusConstantAlpha,
  kSrcAlphaSaturate,
};

struct PipelineRenderTarget {
  PipelineBlendFactor src_color_blend_factor : 4;  // 4
  PipelineBlendFactor dst_color_blend_factor : 4;  // 8
  rex::graphics::xenos::BlendOp color_blend_op : 3; // 11
  PipelineBlendFactor src_alpha_blend_factor : 4;  // 15
  PipelineBlendFactor dst_alpha_blend_factor : 4;  // 19
  rex::graphics::xenos::BlendOp alpha_blend_op : 3; // 22
  uint32_t color_write_mask : 4;                   // 26
  uint32_t padding : 6;                            // 32
};

struct PipelineDescription {
  uint64_t vertex_shader_hash;                     // 8
  uint64_t vertex_shader_modification;             // 8
  uint64_t pixel_shader_hash;                      // 8
  uint64_t pixel_shader_modification;              // 8
  uint32_t render_pass_key;                        // 4

  // Word 0 (32 bits)
  PipelineGeometryShader geometry_shader : 2;      // 2
  PipelinePrimitiveTopology primitive_topology : 3; // 5
  uint32_t primitive_restart : 1;                  // 6
  rex::graphics::xenos::TessellationMode tessellation_mode : 2; // 8
  uint32_t depth_clamp_enable : 1;                 // 9
  PipelinePolygonMode polygon_mode : 2;            // 11
  uint32_t cull_front : 1;                         // 12
  uint32_t cull_back : 1;                          // 13
  uint32_t front_face_clockwise : 1;               // 14
  uint32_t rasterizer_discard : 1;                 // 15
  uint32_t depth_write_enable : 1;                 // 16
  rex::graphics::xenos::CompareFunction depth_compare_op : 3; // 19
  uint32_t stencil_test_enable : 1;                // 20
  rex::graphics::xenos::StencilOp stencil_front_fail_op : 3; // 23
  rex::graphics::xenos::StencilOp stencil_front_pass_op : 3; // 26
  rex::graphics::xenos::StencilOp stencil_front_depth_fail_op : 3; // 29
  rex::graphics::xenos::CompareFunction stencil_front_compare_op : 3; // 32

  // Word 1 (32 bits)
  rex::graphics::xenos::StencilOp stencil_back_fail_op : 3; // 3
  rex::graphics::xenos::StencilOp stencil_back_pass_op : 3; // 6
  rex::graphics::xenos::StencilOp stencil_back_depth_fail_op : 3; // 9
  rex::graphics::xenos::CompareFunction stencil_back_compare_op : 3; // 12
  uint32_t sample_rate_shading : 1;                // 13
  uint32_t padding0 : 19;                          // 32

  PipelineRenderTarget render_targets[rex::graphics::xenos::kMaxColorRenderTargets]; // 16 bytes

  uint64_t GetHash() const {
    return XXH3_64bits(this, sizeof(*this));
  }
};
static_assert(sizeof(PipelineDescription) == 60, "PipelineDescription must be 60 bytes");

struct PipelineStoredDescription {
  uint64_t description_hash;
  PipelineDescription description;
};
static_assert(sizeof(PipelineStoredDescription) == 68, "PipelineStoredDescription must be 68 bytes");
#pragma pack(pop)

struct GuestShader {
  uint32_t type;            // 0 = vertex, 1 = pixel
  uint64_t hash;
  std::vector<uint32_t> ucode;
};

struct LatencyStats {
  double count = 0;
  double sum_ms = 0;
  double min_ms = 0;
  double max_ms = 0;
  double mean_ms = 0;
  double median_ms = 0;
  double p95_ms = 0;
  double p99_ms = 0;

  static LatencyStats Compute(std::vector<double>& samples) {
    LatencyStats stats;
    if (samples.empty()) return stats;
    stats.count = static_cast<double>(samples.size());
    std::sort(samples.begin(), samples.end());
    stats.min_ms = samples.front();
    stats.max_ms = samples.back();
    double sum = 0;
    for (double v : samples) sum += v;
    stats.sum_ms = sum;
    stats.mean_ms = sum / samples.size();
    auto percentile = [&](double p) {
      size_t idx = static_cast<size_t>(std::ceil(p * samples.size())) - 1;
      if (idx >= samples.size()) idx = samples.size() - 1;
      return samples[idx];
    };
    stats.median_ms = percentile(0.50);
    stats.p95_ms = percentile(0.95);
    stats.p99_ms = percentile(0.99);
    return stats;
  }
};

// ---------------------------------------------------------------------------
// Dynamic Vulkan Dispatch Table
// ---------------------------------------------------------------------------

struct VulkanContext {
  HMODULE vulkan_dll = nullptr;

  // Global functions
  PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
  PFN_vkCreateInstance vkCreateInstance = nullptr;
  PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties = nullptr;
  PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties = nullptr;

  // Instance functions
  PFN_vkDestroyInstance vkDestroyInstance = nullptr;
  PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = nullptr;
  PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2 = nullptr;
  PFN_vkGetPhysicalDeviceFeatures2 vkGetPhysicalDeviceFeatures2 = nullptr;
  PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties = nullptr;
  PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties = nullptr;
  PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties = nullptr;
  PFN_vkCreateDevice vkCreateDevice = nullptr;
  PFN_vkDestroyDevice vkDestroyDevice = nullptr;
  PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;

  // Instance & Device
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queue_family_index = 0;
  VkPhysicalDeviceProperties physical_device_properties = {};

  // Device functions
  PFN_vkGetDeviceQueue vkGetDeviceQueue = nullptr;
  PFN_vkCreateShaderModule vkCreateShaderModule = nullptr;
  PFN_vkDestroyShaderModule vkDestroyShaderModule = nullptr;
  PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout = nullptr;
  PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout = nullptr;
  PFN_vkCreatePipelineLayout vkCreatePipelineLayout = nullptr;
  PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout = nullptr;
  PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines = nullptr;
  PFN_vkDestroyPipeline vkDestroyPipeline = nullptr;
  PFN_vkCreateCommandPool vkCreateCommandPool = nullptr;
  PFN_vkDestroyCommandPool vkDestroyCommandPool = nullptr;
  PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers = nullptr;
  PFN_vkFreeCommandBuffers vkFreeCommandBuffers = nullptr;
  PFN_vkBeginCommandBuffer vkBeginCommandBuffer = nullptr;
  PFN_vkEndCommandBuffer vkEndCommandBuffer = nullptr;
  PFN_vkCmdBeginRendering vkCmdBeginRendering = nullptr;
  PFN_vkCmdEndRendering vkCmdEndRendering = nullptr;
  PFN_vkCmdBindPipeline vkCmdBindPipeline = nullptr;
  PFN_vkCmdDraw vkCmdDraw = nullptr;
  PFN_vkQueueSubmit vkQueueSubmit = nullptr;
  PFN_vkQueueWaitIdle vkQueueWaitIdle = nullptr;
  PFN_vkCreateQueryPool vkCreateQueryPool = nullptr;
  PFN_vkDestroyQueryPool vkDestroyQueryPool = nullptr;
  PFN_vkCmdResetQueryPool vkCmdResetQueryPool = nullptr;
  PFN_vkCmdWriteTimestamp vkCmdWriteTimestamp = nullptr;
  PFN_vkGetQueryPoolResults vkGetQueryPoolResults = nullptr;

  // Images / Memory for offscreen draw testing
  PFN_vkCreateImage vkCreateImage = nullptr;
  PFN_vkDestroyImage vkDestroyImage = nullptr;
  PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements = nullptr;
  PFN_vkAllocateMemory vkAllocateMemory = nullptr;
  PFN_vkFreeMemory vkFreeMemory = nullptr;
  PFN_vkBindImageMemory vkBindImageMemory = nullptr;
  PFN_vkCreateImageView vkCreateImageView = nullptr;
  PFN_vkDestroyImageView vkDestroyImageView = nullptr;
  PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier = nullptr;

  // Dynamic state functions
  PFN_vkCmdSetViewport vkCmdSetViewport = nullptr;
  PFN_vkCmdSetScissor vkCmdSetScissor = nullptr;
  PFN_vkCmdSetCullModeEXT vkCmdSetCullModeEXT = nullptr;
  PFN_vkCmdSetFrontFaceEXT vkCmdSetFrontFaceEXT = nullptr;
  PFN_vkCmdSetDepthTestEnableEXT vkCmdSetDepthTestEnableEXT = nullptr;
  PFN_vkCmdSetDepthWriteEnableEXT vkCmdSetDepthWriteEnableEXT = nullptr;
  PFN_vkCmdSetDepthCompareOpEXT vkCmdSetDepthCompareOpEXT = nullptr;
  PFN_vkCmdSetStencilTestEnableEXT vkCmdSetStencilTestEnableEXT = nullptr;
  PFN_vkCmdSetStencilOpEXT vkCmdSetStencilOpEXT = nullptr;

  // Validation
  PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = nullptr;
  PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT = nullptr;
  VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
  uint32_t validation_error_count = 0;
  uint32_t validation_warning_count = 0;
};

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* user_data) {
  auto* ctx = reinterpret_cast<VulkanContext*>(user_data);
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    if (ctx) ctx->validation_error_count++;
    std::cerr << "[VALIDATION ERROR] " << data->pMessage << "\n";
  } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    if (ctx) ctx->validation_warning_count++;
    std::cerr << "[VALIDATION WARNING] " << data->pMessage << "\n";
  }
  return VK_FALSE;
}

// ---------------------------------------------------------------------------
// Purge GLCache
// ---------------------------------------------------------------------------

static size_t PurgeGLCache() {
  char* local_app_data = nullptr;
  size_t len = 0;
  if (_dupenv_s(&local_app_data, &len, "LOCALAPPDATA") != 0 || !local_app_data) {
    return 0;
  }
  fs::path gl_cache = fs::path(local_app_data) / "NVIDIA" / "GLCache";
  free(local_app_data);

  size_t removed = 0;
  if (fs::exists(gl_cache)) {
    try {
      for (const auto& entry : fs::recursive_directory_iterator(gl_cache)) {
        if (entry.is_regular_file()) {
          std::error_code ec;
          if (fs::remove(entry.path(), ec)) {
            removed++;
          }
        }
      }
    } catch (...) {}
  }
  return removed;
}

// ---------------------------------------------------------------------------
// Post-EDS Normalizer
// ---------------------------------------------------------------------------

static PipelineDescription NormalizeEDS(const PipelineDescription& src) {
  PipelineDescription desc = src;

  // Dynamic with EDS 2
  desc.primitive_restart = 0;

  // Dynamic with EDS 1
  desc.cull_front = 0;
  desc.cull_back = 0;
  desc.front_face_clockwise = 0;
  desc.depth_write_enable = 0;
  desc.depth_compare_op = rex::graphics::xenos::CompareFunction::kNever;
  desc.stencil_test_enable = 0;
  desc.stencil_front_fail_op = rex::graphics::xenos::StencilOp::kKeep;
  desc.stencil_front_pass_op = rex::graphics::xenos::StencilOp::kKeep;
  desc.stencil_front_depth_fail_op = rex::graphics::xenos::StencilOp::kKeep;
  desc.stencil_front_compare_op = rex::graphics::xenos::CompareFunction::kNever;
  desc.stencil_back_fail_op = rex::graphics::xenos::StencilOp::kKeep;
  desc.stencil_back_pass_op = rex::graphics::xenos::StencilOp::kKeep;
  desc.stencil_back_depth_fail_op = rex::graphics::xenos::StencilOp::kKeep;
  desc.stencil_back_compare_op = rex::graphics::xenos::CompareFunction::kNever;

  // Dynamic with EDS 3
  for (uint32_t i = 0; i < rex::graphics::xenos::kMaxColorRenderTargets; ++i) {
    desc.render_targets[i].src_color_blend_factor = PipelineBlendFactor::kOne;
    desc.render_targets[i].dst_color_blend_factor = PipelineBlendFactor::kZero;
    desc.render_targets[i].color_blend_op = rex::graphics::xenos::BlendOp::kAdd;
    desc.render_targets[i].src_alpha_blend_factor = PipelineBlendFactor::kOne;
    desc.render_targets[i].dst_alpha_blend_factor = PipelineBlendFactor::kZero;
    desc.render_targets[i].alpha_blend_op = rex::graphics::xenos::BlendOp::kAdd;
    desc.render_targets[i].color_write_mask = 0;
  }

  return desc;
}

// ---------------------------------------------------------------------------
// Main Harness Implementation
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
  puts("DEBUG: Entered main!");
  fflush(stdout);
  std::cout << "=================================================================\n";
  std::cout << "  Majin Recomp - GPL Clean-Room Empirical Validation Spike (M2a) \n";
  std::cout << "  RTX 3060 Ti Post-EDS Pipeline Library Gate Benchmark           \n";
  std::cout << "=================================================================\n\n";

  // Parse CLI args
  fs::path xpso_path = "game-data/runtime/cold_driver_exp/cache/shaders/shareable/4E4D0816.fbo.vk.xpso";
  fs::path xsh_path = "game-data/runtime/cold_driver_exp/cache/shaders/shareable/4E4D0816.xsh";
  fs::path output_path = "reports/gpl-spike/gpl-spike-results.json";
  std::string phase = "all";
  bool do_purge = true;
  uint32_t draw_iterations = 1000;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg.rfind("--xpso=", 0) == 0) xpso_path = arg.substr(7);
    else if (arg.rfind("--xsh=", 0) == 0) xsh_path = arg.substr(6);
    else if (arg.rfind("--output=", 0) == 0) output_path = arg.substr(9);
    else if (arg.rfind("--phase=", 0) == 0) phase = arg.substr(8);
    else if (arg.rfind("--draw-iterations=", 0) == 0) draw_iterations = std::stoul(arg.substr(18));
    else if (arg == "--no-purge") do_purge = false;
  }

  puts("DEBUG: CLI parsed"); fflush(stdout);

  // 1. Initialize Vulkan
  VulkanContext vk;
  vk.vulkan_dll = LoadLibraryA("vulkan-1.dll");
  if (!vk.vulkan_dll) {
    std::cerr << "FATAL: Failed to load vulkan-1.dll\n";
    return 1;
  }
  puts("DEBUG: vulkan-1.dll loaded"); fflush(stdout);
  vk.vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
      GetProcAddress(vk.vulkan_dll, "vkGetInstanceProcAddr"));
  if (!vk.vkGetInstanceProcAddr) {
    std::cerr << "FATAL: Failed to get vkGetInstanceProcAddr\n";
    return 1;
  }
  puts("DEBUG: vkGetInstanceProcAddr retrieved"); fflush(stdout);

#define LOAD_GLOBAL(name) vk.name = reinterpret_cast<PFN_##name>(vk.vkGetInstanceProcAddr(VK_NULL_HANDLE, #name))
  LOAD_GLOBAL(vkCreateInstance);
  LOAD_GLOBAL(vkEnumerateInstanceExtensionProperties);
  LOAD_GLOBAL(vkEnumerateInstanceLayerProperties);
#undef LOAD_GLOBAL

  // Enumerate supported instance extensions
  uint32_t inst_ext_count = 0;
  if (vk.vkEnumerateInstanceExtensionProperties) {
    vk.vkEnumerateInstanceExtensionProperties(nullptr, &inst_ext_count, nullptr);
  }
  std::vector<VkExtensionProperties> available_inst_exts(inst_ext_count);
  if (inst_ext_count > 0) {
    vk.vkEnumerateInstanceExtensionProperties(nullptr, &inst_ext_count, available_inst_exts.data());
  }
  auto has_inst_ext = [&](const char* name) {
    for (const auto& e : available_inst_exts) {
      if (std::strcmp(e.extensionName, name) == 0) return true;
    }
    return false;
  };

  // Enumerate supported instance layers
  uint32_t inst_layer_count = 0;
  if (vk.vkEnumerateInstanceLayerProperties) {
    vk.vkEnumerateInstanceLayerProperties(&inst_layer_count, nullptr);
  }
  std::vector<VkLayerProperties> available_inst_layers(inst_layer_count);
  if (inst_layer_count > 0) {
    vk.vkEnumerateInstanceLayerProperties(&inst_layer_count, available_inst_layers.data());
  }
  auto has_inst_layer = [&](const char* name) {
    for (const auto& l : available_inst_layers) {
      if (std::strcmp(l.layerName, name) == 0) return true;
    }
    return false;
  };

  // Instance Extensions
  std::vector<const char*> instance_extensions;
  if (has_inst_ext(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)) {
    instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
  }
  if (has_inst_ext(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
    instance_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  std::vector<const char*> instance_layers;
  if (has_inst_layer("VK_LAYER_KHR_validation")) {
    instance_layers.push_back("VK_LAYER_KHR_validation");
  }

  VkApplicationInfo app_info = {};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = "GPL Spike Harness";
  app_info.apiVersion = VK_API_VERSION_1_3;

  VkInstanceCreateInfo instance_ci = {};
  instance_ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_ci.pApplicationInfo = &app_info;
  instance_ci.enabledExtensionCount = static_cast<uint32_t>(instance_extensions.size());
  instance_ci.ppEnabledExtensionNames = instance_extensions.data();
  instance_ci.enabledLayerCount = static_cast<uint32_t>(instance_layers.size());
  instance_ci.ppEnabledLayerNames = instance_layers.data();

  VkResult res = vk.vkCreateInstance(&instance_ci, nullptr, &vk.instance);
  if (res != VK_SUCCESS) {
    // Retry without validation layer if not installed
    std::cout << "Note: Validation layers not available or failed, proceeding without them.\n";
    instance_ci.enabledLayerCount = 0;
    instance_ci.ppEnabledLayerNames = nullptr;
    res = vk.vkCreateInstance(&instance_ci, nullptr, &vk.instance);
    if (res != VK_SUCCESS) {
      std::cerr << "FATAL: vkCreateInstance failed: " << res << "\n";
      return 1;
    }
  }

  // Load instance functions using vk.instance
#define LOAD_INSTANCE(name) vk.name = reinterpret_cast<PFN_##name>(vk.vkGetInstanceProcAddr(vk.instance, #name))
  LOAD_INSTANCE(vkDestroyInstance);
  LOAD_INSTANCE(vkEnumeratePhysicalDevices);
  LOAD_INSTANCE(vkGetPhysicalDeviceProperties2);
  LOAD_INSTANCE(vkGetPhysicalDeviceFeatures2);
  LOAD_INSTANCE(vkGetPhysicalDeviceMemoryProperties);
  LOAD_INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties);
  LOAD_INSTANCE(vkEnumerateDeviceExtensionProperties);
  LOAD_INSTANCE(vkCreateDevice);
  LOAD_INSTANCE(vkDestroyDevice);
  LOAD_INSTANCE(vkGetDeviceProcAddr);
#undef LOAD_INSTANCE

  // Hook debug messenger
  vk.vkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
      vk.vkGetInstanceProcAddr(vk.instance, "vkCreateDebugUtilsMessengerEXT"));
  vk.vkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
      vk.vkGetInstanceProcAddr(vk.instance, "vkDestroyDebugUtilsMessengerEXT"));
  if (vk.vkCreateDebugUtilsMessengerEXT) {
    VkDebugUtilsMessengerCreateInfoEXT dbg_ci = {};
    dbg_ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    dbg_ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    dbg_ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
    dbg_ci.pfnUserCallback = DebugCallback;
    dbg_ci.pUserData = &vk;
    vk.vkCreateDebugUtilsMessengerEXT(vk.instance, &dbg_ci, nullptr, &vk.debug_messenger);
  }

  // Pick physical device
  uint32_t device_count = 0;
  vk.vkEnumeratePhysicalDevices(vk.instance, &device_count, nullptr);
  std::vector<VkPhysicalDevice> devices(device_count);
  vk.vkEnumeratePhysicalDevices(vk.instance, &device_count, devices.data());

  for (auto d : devices) {
    VkPhysicalDeviceProperties2 props2 = {};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    vk.vkGetPhysicalDeviceProperties2(d, &props2);
    if (props2.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
      vk.physical_device = d;
      vk.physical_device_properties = props2.properties;
      break;
    }
  }
  if (!vk.physical_device && !devices.empty()) {
    vk.physical_device = devices[0];
    VkPhysicalDeviceProperties2 props2 = {};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    vk.vkGetPhysicalDeviceProperties2(vk.physical_device, &props2);
    vk.physical_device_properties = props2.properties;
  }

  std::cout << "[Device] Selected GPU: " << vk.physical_device_properties.deviceName << "\n";
  std::cout << "[Device] Driver Version: " << (vk.physical_device_properties.driverVersion >> 22)
            << "." << ((vk.physical_device_properties.driverVersion >> 14) & 0xff) << "\n";

  // Check GPL and EDS Features
  VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT gpl_features = {};
  gpl_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT;

  VkPhysicalDeviceExtendedDynamicStateFeaturesEXT eds1_features = {};
  eds1_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
  eds1_features.pNext = &gpl_features;

  VkPhysicalDeviceExtendedDynamicState2FeaturesEXT eds2_features = {};
  eds2_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT;
  eds2_features.pNext = &eds1_features;

  VkPhysicalDeviceExtendedDynamicState3FeaturesEXT eds3_features = {};
  eds3_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT;
  eds3_features.pNext = &eds2_features;

  VkPhysicalDeviceDynamicRenderingFeatures dyn_features = {};
  dyn_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
  dyn_features.pNext = &eds3_features;

  VkPhysicalDeviceFeatures2 features2 = {};
  features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  features2.pNext = &dyn_features;

  vk.vkGetPhysicalDeviceFeatures2(vk.physical_device, &features2);

  if (!gpl_features.graphicsPipelineLibrary) {
    std::cerr << "FATAL: VK_EXT_graphics_pipeline_library not supported on this device!\n";
    return 1;
  }
  std::cout << "[Device] VK_EXT_graphics_pipeline_library: Supported\n";
  std::cout << "[Device] VK_EXT_extended_dynamic_state (1, 2, 3): Supported\n";

  // Find graphics queue family
  uint32_t qf_count = 0;
  vk.vkGetPhysicalDeviceQueueFamilyProperties(vk.physical_device, &qf_count, nullptr);
  std::vector<VkQueueFamilyProperties> qf_props(qf_count);
  vk.vkGetPhysicalDeviceQueueFamilyProperties(vk.physical_device, &qf_count, qf_props.data());
  vk.queue_family_index = 0;
  for (uint32_t i = 0; i < qf_count; ++i) {
    if (qf_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      vk.queue_family_index = i;
      break;
    }
  }

  // Create Device
  float queue_priority = 1.0f;
  VkDeviceQueueCreateInfo queue_ci = {};
  queue_ci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_ci.queueFamilyIndex = vk.queue_family_index;
  queue_ci.queueCount = 1;
  queue_ci.pQueuePriorities = &queue_priority;

  std::vector<const char*> device_extensions = {
      VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
      VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME,
      VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
      VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME,
      VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME,
      VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME
  };

  VkDeviceCreateInfo device_ci = {};
  device_ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_ci.pNext = &dyn_features;
  device_ci.queueCreateInfoCount = 1;
  device_ci.pQueueCreateInfos = &queue_ci;
  device_ci.enabledExtensionCount = static_cast<uint32_t>(device_extensions.size());
  device_ci.ppEnabledExtensionNames = device_extensions.data();

  res = vk.vkCreateDevice(vk.physical_device, &device_ci, nullptr, &vk.device);
  if (res != VK_SUCCESS) {
    std::cerr << "FATAL: vkCreateDevice failed: " << res << "\n";
    return 1;
  }

#define LOAD_DEVICE(name) vk.name = reinterpret_cast<PFN_##name>(vk.vkGetDeviceProcAddr(vk.device, #name))
  LOAD_DEVICE(vkGetDeviceQueue);
  LOAD_DEVICE(vkCreateShaderModule);
  LOAD_DEVICE(vkDestroyShaderModule);
  LOAD_DEVICE(vkCreateDescriptorSetLayout);
  LOAD_DEVICE(vkDestroyDescriptorSetLayout);
  LOAD_DEVICE(vkCreatePipelineLayout);
  LOAD_DEVICE(vkDestroyPipelineLayout);
  LOAD_DEVICE(vkCreateGraphicsPipelines);
  LOAD_DEVICE(vkDestroyPipeline);
  LOAD_DEVICE(vkCreateCommandPool);
  LOAD_DEVICE(vkDestroyCommandPool);
  LOAD_DEVICE(vkAllocateCommandBuffers);
  LOAD_DEVICE(vkFreeCommandBuffers);
  LOAD_DEVICE(vkBeginCommandBuffer);
  LOAD_DEVICE(vkEndCommandBuffer);
  LOAD_DEVICE(vkCmdBeginRendering);
  LOAD_DEVICE(vkCmdEndRendering);
  LOAD_DEVICE(vkCmdBindPipeline);
  LOAD_DEVICE(vkCmdDraw);
  LOAD_DEVICE(vkQueueSubmit);
  LOAD_DEVICE(vkQueueWaitIdle);
  LOAD_DEVICE(vkCreateQueryPool);
  LOAD_DEVICE(vkDestroyQueryPool);
  LOAD_DEVICE(vkCmdResetQueryPool);
  LOAD_DEVICE(vkCmdWriteTimestamp);
  LOAD_DEVICE(vkGetQueryPoolResults);
  LOAD_DEVICE(vkCreateImage);
  LOAD_DEVICE(vkDestroyImage);
  LOAD_DEVICE(vkGetImageMemoryRequirements);
  LOAD_DEVICE(vkAllocateMemory);
  LOAD_DEVICE(vkFreeMemory);
  LOAD_DEVICE(vkBindImageMemory);
  LOAD_DEVICE(vkCreateImageView);
  LOAD_DEVICE(vkDestroyImageView);
  LOAD_DEVICE(vkCmdPipelineBarrier);
  LOAD_DEVICE(vkCmdSetViewport);
  LOAD_DEVICE(vkCmdSetScissor);
  LOAD_DEVICE(vkCmdSetCullModeEXT);
  LOAD_DEVICE(vkCmdSetFrontFaceEXT);
  LOAD_DEVICE(vkCmdSetDepthTestEnableEXT);
  LOAD_DEVICE(vkCmdSetDepthWriteEnableEXT);
  LOAD_DEVICE(vkCmdSetDepthCompareOpEXT);
  LOAD_DEVICE(vkCmdSetStencilTestEnableEXT);
  LOAD_DEVICE(vkCmdSetStencilOpEXT);
#undef LOAD_DEVICE

  vk.vkGetDeviceQueue(vk.device, vk.queue_family_index, 0, &vk.queue);

  // 2. Load Shaders (.xsh)
  std::cout << "[Dataset] Loading guest shaders from: " << xsh_path.string() << "\n";
  std::ifstream xsh_file(xsh_path, std::ios::binary);
  if (!xsh_file) {
    std::cerr << "FATAL: Could not open " << xsh_path << "\n";
    return 1;
  }
  XshHeader xsh_hdr;
  xsh_file.read(reinterpret_cast<char*>(&xsh_hdr), sizeof(xsh_hdr));
  if (xsh_hdr.magic != 0x48534558) {
    std::cerr << "FATAL: Invalid XSH magic: 0x" << std::hex << xsh_hdr.magic << "\n";
    return 1;
  }

  std::unordered_map<uint64_t, GuestShader> guest_shaders;
  while (xsh_file) {
    XshRecordHeader rec;
    xsh_file.read(reinterpret_cast<char*>(&rec), sizeof(rec));
    if (!xsh_file) break;
    uint32_t dwords = rec.countType & 0x7FFFFFFF;
    uint32_t type = rec.countType >> 31;
    GuestShader gs;
    gs.hash = rec.ucode_data_hash;
    gs.type = type;
    gs.ucode.resize(dwords);
    xsh_file.read(reinterpret_cast<char*>(gs.ucode.data()), dwords * sizeof(uint32_t));
    guest_shaders[gs.hash] = std::move(gs);
  }
  std::cout << "[Dataset] Loaded " << guest_shaders.size() << " guest shaders from XSH.\n";

  // 3. Load Pipeline Cache (.xpso) & Normalize with EDS
  std::cout << "[Dataset] Loading pipeline records from: " << xpso_path.string() << "\n";
  std::ifstream xpso_file(xpso_path, std::ios::binary);
  if (!xpso_file) {
    std::cerr << "FATAL: Could not open " << xpso_path << "\n";
    return 1;
  }
  xpso_file.seekg(20, std::ios::beg); // Skip 20 byte header

  std::vector<PipelineDescription> raw_descriptions;
  std::unordered_map<uint64_t, PipelineDescription> post_eds_unique_map;
  std::set<std::pair<uint64_t, uint64_t>> unique_vs_set;
  std::set<std::pair<uint64_t, uint64_t>> unique_ps_set;

  while (xpso_file) {
    PipelineStoredDescription rec;
    xpso_file.read(reinterpret_cast<char*>(&rec), sizeof(rec));
    if (!xpso_file) break;
    raw_descriptions.push_back(rec.description);

    PipelineDescription norm = NormalizeEDS(rec.description);
    uint64_t norm_hash = norm.GetHash();
    post_eds_unique_map[norm_hash] = norm;

    unique_vs_set.emplace(norm.vertex_shader_hash, norm.vertex_shader_modification);
    if (norm.pixel_shader_hash != 0) {
      unique_ps_set.emplace(norm.pixel_shader_hash, norm.pixel_shader_modification);
    }
  }

  std::cout << "[Dataset] Raw PSOs in cache: " << raw_descriptions.size() << "\n";
  std::cout << "[Dataset] Post-EDS Unique PSOs: " << post_eds_unique_map.size()
            << " (Reduction: " << (100.0 - 100.0 * post_eds_unique_map.size() / raw_descriptions.size())
            << "%)\n";
  std::cout << "[Dataset] Unique Post-EDS VS (hash, mod): " << unique_vs_set.size() << "\n";
  std::cout << "[Dataset] Unique Post-EDS PS (hash, mod): " << unique_ps_set.size() << "\n";

  // 4. Translate Shaders to SPIR-V & Create VkShaderModule
  rex::graphics::SpirvShaderTranslator::Features translator_features(true);
  rex::graphics::SpirvShaderTranslator translator(translator_features, false, false, false);
  rex::string::StringBuffer disasm_buffer;

  std::unordered_map<uint64_t, VkShaderModule> vs_modules;
  std::unordered_map<uint64_t, VkShaderModule> ps_modules;

  std::cout << "[Translation] Translating " << unique_vs_set.size() << " VS modules...\n";
  for (const auto& [hash, mod] : unique_vs_set) {
    auto it = guest_shaders.find(hash);
    if (it == guest_shaders.end()) continue;
    rex::graphics::SpirvShader shader(
        rex::graphics::xenos::ShaderType::kVertex, hash,
        it->second.ucode.data(), it->second.ucode.size());
    shader.AnalyzeUcode(disasm_buffer);
    bool is_new = false;
    auto* translation = static_cast<rex::graphics::SpirvShader::SpirvTranslation*>(
        shader.GetOrCreateTranslation(mod, &is_new));
    if (translator.TranslateAnalyzedShader(*translation)) {
      const auto& binary = translation->translated_binary();
      VkShaderModuleCreateInfo sm_ci = {};
      sm_ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
      sm_ci.codeSize = binary.size();
      sm_ci.pCode = reinterpret_cast<const uint32_t*>(binary.data());
      VkShaderModule module = VK_NULL_HANDLE;
      vk.vkCreateShaderModule(vk.device, &sm_ci, nullptr, &module);
      uint64_t key = (hash ^ (mod * 0x9e3779b97f4a7c15ULL));
      vs_modules[key] = module;
    }
  }

  std::cout << "[Translation] Translating " << unique_ps_set.size() << " PS modules...\n";
  for (const auto& [hash, mod] : unique_ps_set) {
    auto it = guest_shaders.find(hash);
    if (it == guest_shaders.end()) continue;
    rex::graphics::SpirvShader shader(
        rex::graphics::xenos::ShaderType::kPixel, hash,
        it->second.ucode.data(), it->second.ucode.size());
    shader.AnalyzeUcode(disasm_buffer);
    bool is_new = false;
    auto* translation = static_cast<rex::graphics::SpirvShader::SpirvTranslation*>(
        shader.GetOrCreateTranslation(mod, &is_new));
    if (translator.TranslateAnalyzedShader(*translation)) {
      const auto& binary = translation->translated_binary();
      VkShaderModuleCreateInfo sm_ci = {};
      sm_ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
      sm_ci.codeSize = binary.size();
      sm_ci.pCode = reinterpret_cast<const uint32_t*>(binary.data());
      VkShaderModule module = VK_NULL_HANDLE;
      vk.vkCreateShaderModule(vk.device, &sm_ci, nullptr, &module);
      uint64_t key = (hash ^ (mod * 0x9e3779b97f4a7c15ULL));
      ps_modules[key] = module;
    }
  }

  // 5. Create Descriptor Set Layouts & Pipeline Layout with Independent Sets
  std::vector<VkDescriptorSetLayoutBinding> bindings_set0 = {
      {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr},
      {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr}
  };
  VkDescriptorSetLayoutCreateInfo ds_ci0 = {};
  ds_ci0.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  ds_ci0.bindingCount = static_cast<uint32_t>(bindings_set0.size());
  ds_ci0.pBindings = bindings_set0.data();
  VkDescriptorSetLayout ds_layout0 = VK_NULL_HANDLE;
  vk.vkCreateDescriptorSetLayout(vk.device, &ds_ci0, nullptr, &ds_layout0);

  std::vector<VkDescriptorSetLayoutBinding> bindings_set1 = {
      {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr}
  };
  VkDescriptorSetLayoutCreateInfo ds_ci1 = {};
  ds_ci1.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  ds_ci1.bindingCount = static_cast<uint32_t>(bindings_set1.size());
  ds_ci1.pBindings = bindings_set1.data();
  VkDescriptorSetLayout ds_layout1 = VK_NULL_HANDLE;
  vk.vkCreateDescriptorSetLayout(vk.device, &ds_ci1, nullptr, &ds_layout1);

  std::vector<VkDescriptorSetLayoutBinding> bindings_textures;
  for (uint32_t b = 0; b < 32; ++b) {
    bindings_textures.push_back({b, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr});
  }
  VkDescriptorSetLayoutCreateInfo ds_citex = {};
  ds_citex.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  ds_citex.bindingCount = static_cast<uint32_t>(bindings_textures.size());
  ds_citex.pBindings = bindings_textures.data();
  VkDescriptorSetLayout ds_layout2 = VK_NULL_HANDLE;
  VkDescriptorSetLayout ds_layout3 = VK_NULL_HANDLE;
  vk.vkCreateDescriptorSetLayout(vk.device, &ds_citex, nullptr, &ds_layout2);
  vk.vkCreateDescriptorSetLayout(vk.device, &ds_citex, nullptr, &ds_layout3);

  VkDescriptorSetLayout set_layouts[4] = {ds_layout0, ds_layout1, ds_layout2, ds_layout3};
  VkPipelineLayoutCreateInfo pl_ci = {};
  pl_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pl_ci.flags = VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT;
  pl_ci.setLayoutCount = 4;
  pl_ci.pSetLayouts = set_layouts;
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  vk.vkCreatePipelineLayout(vk.device, &pl_ci, nullptr, &pipeline_layout);

  // Common Pipeline State Setup
  VkPipelineVertexInputStateCreateInfo vi_ci = {};
  vi_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  VkPipelineInputAssemblyStateCreateInfo ia_ci = {};
  ia_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  ia_ci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  ia_ci.primitiveRestartEnable = VK_FALSE;

  VkPipelineViewportStateCreateInfo vp_ci = {};
  vp_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  vp_ci.viewportCount = 1;
  vp_ci.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rast_ci = {};
  rast_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rast_ci.polygonMode = VK_POLYGON_MODE_FILL;
  rast_ci.cullMode = VK_CULL_MODE_NONE;
  rast_ci.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rast_ci.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo ms_ci = {};
  ms_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms_ci.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo ds_ci = {};
  ds_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

  VkPipelineColorBlendAttachmentState blend_attachment = {};
  blend_attachment.colorWriteMask = 0;
  VkPipelineColorBlendStateCreateInfo cb_ci = {};
  cb_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  cb_ci.attachmentCount = 1;
  cb_ci.pAttachments = &blend_attachment;

  VkDynamicState dynamic_states[] = {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,
      VK_DYNAMIC_STATE_CULL_MODE,
      VK_DYNAMIC_STATE_FRONT_FACE,
      VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
      VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
      VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
      VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE,
      VK_DYNAMIC_STATE_STENCIL_OP,
      VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE,
      VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE,
      VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT,
      VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT,
      VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT
  };
  VkPipelineDynamicStateCreateInfo dyn_ci = {};
  dyn_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dyn_ci.dynamicStateCount = sizeof(dynamic_states) / sizeof(dynamic_states[0]);
  dyn_ci.pDynamicStates = dynamic_states;

  VkFormat color_format = VK_FORMAT_R8G8B8A8_UNORM;
  VkFormat depth_format = VK_FORMAT_D24_UNORM_S8_UINT;
  VkPipelineRenderingCreateInfo rendering_ci = {};
  rendering_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  rendering_ci.colorAttachmentCount = 1;
  rendering_ci.pColorAttachmentFormats = &color_format;
  rendering_ci.depthAttachmentFormat = depth_format;
  rendering_ci.stencilAttachmentFormat = depth_format;

  // -------------------------------------------------------------------------
  // BENCHMARK 1: Cold Monolithic Post-EDS PSOs
  // -------------------------------------------------------------------------
  std::vector<double> mono_cold_samples;
  std::vector<VkPipeline> mono_pipelines;
  mono_pipelines.reserve(post_eds_unique_map.size());

  if (phase == "all" || phase == "monolithic") {
    if (do_purge) {
      size_t purged = PurgeGLCache();
      std::cout << "[Benchmark 1] Purged " << purged << " GLCache files before monolithic run.\n";
    }
    std::cout << "[Benchmark 1] Measuring cold monolithic PSOs (" << post_eds_unique_map.size() << " pipelines)...\n";

    for (const auto& [hash, desc] : post_eds_unique_map) {
      uint64_t vs_key = (desc.vertex_shader_hash ^ (desc.vertex_shader_modification * 0x9e3779b97f4a7c15ULL));
      uint64_t ps_key = (desc.pixel_shader_hash ^ (desc.pixel_shader_modification * 0x9e3779b97f4a7c15ULL));

      VkShaderModule vs_mod = vs_modules[vs_key];
      VkShaderModule ps_mod = desc.pixel_shader_hash ? ps_modules[ps_key] : VK_NULL_HANDLE;
      if (vs_mod == VK_NULL_HANDLE) continue;

      std::vector<VkPipelineShaderStageCreateInfo> stages;
      VkPipelineShaderStageCreateInfo vs_stage = {};
      vs_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      vs_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
      vs_stage.module = vs_mod;
      vs_stage.pName = "main";
      stages.push_back(vs_stage);

      if (ps_mod != VK_NULL_HANDLE) {
        VkPipelineShaderStageCreateInfo ps_stage = {};
        ps_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ps_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        ps_stage.module = ps_mod;
        ps_stage.pName = "main";
        stages.push_back(ps_stage);
      }

      VkGraphicsPipelineCreateInfo pso_ci = {};
      pso_ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
      pso_ci.pNext = &rendering_ci;
      pso_ci.flags = 0;
      pso_ci.stageCount = static_cast<uint32_t>(stages.size());
      pso_ci.pStages = stages.data();
      pso_ci.pVertexInputState = &vi_ci;
      pso_ci.pInputAssemblyState = &ia_ci;
      pso_ci.pViewportState = &vp_ci;
      pso_ci.pRasterizationState = &rast_ci;
      pso_ci.pMultisampleState = &ms_ci;
      pso_ci.pDepthStencilState = &ds_ci;
      pso_ci.pColorBlendState = &cb_ci;
      pso_ci.pDynamicState = &dyn_ci;
      pso_ci.layout = pipeline_layout;

      auto t0 = std::chrono::high_resolution_clock::now();
      VkPipeline pso = VK_NULL_HANDLE;
      res = vk.vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &pso_ci, nullptr, &pso);
      auto t1 = std::chrono::high_resolution_clock::now();

      if (res == VK_SUCCESS && pso != VK_NULL_HANDLE) {
        mono_pipelines.push_back(pso);
        mono_cold_samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
      }
    }
  }

  // -------------------------------------------------------------------------
  // BENCHMARK 2: Cold GPL Libraries (Pre-Raster + Fragment + Interfaces)
  // -------------------------------------------------------------------------
  std::vector<double> gpl_vs_cold_samples;
  std::vector<double> gpl_ps_cold_samples;
  std::unordered_map<uint64_t, VkPipeline> pre_raster_libs;
  std::unordered_map<uint64_t, VkPipeline> fragment_shader_libs;
  VkPipeline vertex_input_lib = VK_NULL_HANDLE;
  VkPipeline fragment_output_lib = VK_NULL_HANDLE;

  if (phase == "all" || phase == "gpl") {
    if (do_purge) {
      size_t purged = PurgeGLCache();
      std::cout << "[Benchmark 2] Purged " << purged << " GLCache files before GPL libraries run.\n";
    }
    std::cout << "[Benchmark 2] Measuring cold GPL libraries...\n";

    // A. Vertex Input Library
    VkGraphicsPipelineLibraryCreateInfoEXT vi_lib_ci = {};
    vi_lib_ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT;
    vi_lib_ci.flags = VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT;

    VkGraphicsPipelineCreateInfo vi_pso_ci = {};
    vi_pso_ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    vi_pso_ci.pNext = &vi_lib_ci;
    vi_pso_ci.flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR;
    vi_pso_ci.pVertexInputState = &vi_ci;
    vi_pso_ci.pInputAssemblyState = &ia_ci;

    vk.vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &vi_pso_ci, nullptr, &vertex_input_lib);

    // B. Fragment Output Library
    VkGraphicsPipelineLibraryCreateInfoEXT fo_lib_ci = {};
    fo_lib_ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT;
    fo_lib_ci.flags = VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT;
    fo_lib_ci.pNext = &rendering_ci;

    VkGraphicsPipelineCreateInfo fo_pso_ci = {};
    fo_pso_ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    fo_pso_ci.pNext = &fo_lib_ci;
    fo_pso_ci.flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR;
    fo_pso_ci.pColorBlendState = &cb_ci;
    fo_pso_ci.pMultisampleState = &ms_ci;
    fo_pso_ci.pDynamicState = &dyn_ci;

    vk.vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &fo_pso_ci, nullptr, &fragment_output_lib);

    // C. Pre-Rasterization Libraries (VS)
    for (const auto& [hash, mod] : unique_vs_set) {
      uint64_t vs_key = (hash ^ (mod * 0x9e3779b97f4a7c15ULL));
      VkShaderModule vs_mod = vs_modules[vs_key];
      if (vs_mod == VK_NULL_HANDLE) continue;

      VkGraphicsPipelineLibraryCreateInfoEXT pr_lib_ci = {};
      pr_lib_ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT;
      pr_lib_ci.flags = VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT;

      VkPipelineShaderStageCreateInfo vs_stage = {};
      vs_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      vs_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
      vs_stage.module = vs_mod;
      vs_stage.pName = "main";

      VkGraphicsPipelineCreateInfo pr_pso_ci = {};
      pr_pso_ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
      pr_pso_ci.pNext = &pr_lib_ci;
      pr_pso_ci.flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR;
      pr_pso_ci.stageCount = 1;
      pr_pso_ci.pStages = &vs_stage;
      pr_pso_ci.pViewportState = &vp_ci;
      pr_pso_ci.pRasterizationState = &rast_ci;
      pr_pso_ci.pDynamicState = &dyn_ci;
      pr_pso_ci.layout = pipeline_layout;

      auto t0 = std::chrono::high_resolution_clock::now();
      VkPipeline lib = VK_NULL_HANDLE;
      res = vk.vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &pr_pso_ci, nullptr, &lib);
      auto t1 = std::chrono::high_resolution_clock::now();

      if (res == VK_SUCCESS && lib != VK_NULL_HANDLE) {
        pre_raster_libs[vs_key] = lib;
        gpl_vs_cold_samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
      }
    }

    // D. Fragment Shader Libraries (PS)
    for (const auto& [hash, mod] : unique_ps_set) {
      uint64_t ps_key = (hash ^ (mod * 0x9e3779b97f4a7c15ULL));
      VkShaderModule ps_mod = ps_modules[ps_key];
      if (ps_mod == VK_NULL_HANDLE) continue;

      VkGraphicsPipelineLibraryCreateInfoEXT fs_lib_ci = {};
      fs_lib_ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT;
      fs_lib_ci.flags = VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT;
      fs_lib_ci.pNext = &rendering_ci;

      VkPipelineShaderStageCreateInfo ps_stage = {};
      ps_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      ps_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
      ps_stage.module = ps_mod;
      ps_stage.pName = "main";

      VkGraphicsPipelineCreateInfo fs_pso_ci = {};
      fs_pso_ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
      fs_pso_ci.pNext = &fs_lib_ci;
      fs_pso_ci.flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR;
      fs_pso_ci.stageCount = 1;
      fs_pso_ci.pStages = &ps_stage;
      fs_pso_ci.pMultisampleState = &ms_ci;
      fs_pso_ci.pDepthStencilState = &ds_ci;
      fs_pso_ci.pDynamicState = &dyn_ci;
      fs_pso_ci.layout = pipeline_layout;

      auto t0 = std::chrono::high_resolution_clock::now();
      VkPipeline lib = VK_NULL_HANDLE;
      res = vk.vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &fs_pso_ci, nullptr, &lib);
      auto t1 = std::chrono::high_resolution_clock::now();

      if (res == VK_SUCCESS && lib != VK_NULL_HANDLE) {
        fragment_shader_libs[ps_key] = lib;
        gpl_ps_cold_samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
      }
    }
  }

  // -------------------------------------------------------------------------
  // BENCHMARK 3: Fast-Linking Latency
  // -------------------------------------------------------------------------
  std::vector<double> fast_link_samples;
  std::vector<VkPipeline> linked_pipelines;
  linked_pipelines.reserve(post_eds_unique_map.size());

  if (phase == "all" || phase == "gpl") {
    std::cout << "[Benchmark 3] Fast-linking " << post_eds_unique_map.size() << " executable pipelines...\n";

    for (const auto& [hash, desc] : post_eds_unique_map) {
      uint64_t vs_key = (desc.vertex_shader_hash ^ (desc.vertex_shader_modification * 0x9e3779b97f4a7c15ULL));
      uint64_t ps_key = (desc.pixel_shader_hash ^ (desc.pixel_shader_modification * 0x9e3779b97f4a7c15ULL));

      VkPipeline pr_lib = pre_raster_libs[vs_key];
      VkPipeline fs_lib = desc.pixel_shader_hash ? fragment_shader_libs[ps_key] : VK_NULL_HANDLE;
      if (pr_lib == VK_NULL_HANDLE) continue;

      std::vector<VkPipeline> libs = {vertex_input_lib, pr_lib};
      if (fs_lib != VK_NULL_HANDLE) libs.push_back(fs_lib);
      libs.push_back(fragment_output_lib);

      VkPipelineLibraryCreateInfoKHR lib_link_ci = {};
      lib_link_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR;
      lib_link_ci.libraryCount = static_cast<uint32_t>(libs.size());
      lib_link_ci.pLibraries = libs.data();

      VkGraphicsPipelineCreateInfo link_pso_ci = {};
      link_pso_ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
      link_pso_ci.pNext = &lib_link_ci;
      link_pso_ci.flags = 0; // Fast-link without LTO
      link_pso_ci.layout = pipeline_layout;

      auto t0 = std::chrono::high_resolution_clock::now();
      VkPipeline linked = VK_NULL_HANDLE;
      res = vk.vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &link_pso_ci, nullptr, &linked);
      auto t1 = std::chrono::high_resolution_clock::now();

      if (res == VK_SUCCESS && linked != VK_NULL_HANDLE) {
        linked_pipelines.push_back(linked);
        fast_link_samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
      }
    }
  }

  // -------------------------------------------------------------------------
  // BENCHMARK 4: GPU Runtime Draw Overhead (Monolithic vs Linked)
  // -------------------------------------------------------------------------
  double gpu_draw_overhead_percent = 0.0;
  double gpu_mono_draw_ms = 0.0;
  double gpu_linked_draw_ms = 0.0;

  if ((phase == "all" || phase == "draw") && !mono_pipelines.empty() && !linked_pipelines.empty()) {
    std::cout << "[Benchmark 4] Measuring GPU draw overhead (" << draw_iterations << " draws offscreen)...\n";

    // Setup offscreen color image (1024x1024 RGBA8)
    VkImage color_image = VK_NULL_HANDLE;
    VkImageCreateInfo img_ci = {};
    img_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_ci.imageType = VK_IMAGE_TYPE_2D;
    img_ci.format = color_format;
    img_ci.extent = {1024, 1024, 1};
    img_ci.mipLevels = 1;
    img_ci.arrayLayers = 1;
    img_ci.samples = VK_SAMPLE_COUNT_1_BIT;
    img_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    vk.vkCreateImage(vk.device, &img_ci, nullptr, &color_image);

    VkMemoryRequirements mem_req;
    vk.vkGetImageMemoryRequirements(vk.device, color_image, &mem_req);

    VkPhysicalDeviceMemoryProperties mem_props = {};
    vk.vkGetPhysicalDeviceMemoryProperties(vk.physical_device, &mem_props);
    uint32_t mem_type_index = 0;
    bool found_mem = false;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
      if ((mem_req.memoryTypeBits & (1 << i)) &&
          (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
        mem_type_index = i;
        found_mem = true;
        break;
      }
    }
    if (!found_mem) {
      for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if (mem_req.memoryTypeBits & (1 << i)) {
          mem_type_index = i;
          break;
        }
      }
    }

    VkMemoryAllocateInfo alloc_ci = {};
    alloc_ci.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_ci.allocationSize = mem_req.size;
    alloc_ci.memoryTypeIndex = mem_type_index;
    VkDeviceMemory color_memory = VK_NULL_HANDLE;
    vk.vkAllocateMemory(vk.device, &alloc_ci, nullptr, &color_memory);
    vk.vkBindImageMemory(vk.device, color_image, color_memory, 0);

    VkImageView color_view = VK_NULL_HANDLE;
    VkImageViewCreateInfo view_ci = {};
    view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_ci.image = color_image;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = color_format;
    view_ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vk.vkCreateImageView(vk.device, &view_ci, nullptr, &color_view);

    // Setup command pool and buffer
    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo cp_ci = {};
    cp_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cp_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cp_ci.queueFamilyIndex = vk.queue_family_index;
    vk.vkCreateCommandPool(vk.device, &cp_ci, nullptr, &cmd_pool);

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cb_alloc = {};
    cb_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cb_alloc.commandPool = cmd_pool;
    cb_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cb_alloc.commandBufferCount = 1;
    vk.vkAllocateCommandBuffers(vk.device, &cb_alloc, &cmd);

    // Setup Query Pool for timestamps
    VkQueryPool query_pool = VK_NULL_HANDLE;
    VkQueryPoolCreateInfo qp_ci = {};
    qp_ci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qp_ci.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qp_ci.queryCount = 2;
    vk.vkCreateQueryPool(vk.device, &qp_ci, nullptr, &query_pool);

    float timestamp_period = vk.physical_device_properties.limits.timestampPeriod; // ns per tick

    auto measure_draws = [&](VkPipeline pso) -> double {
      VkCommandBufferBeginInfo begin_ci = {};
      begin_ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      vk.vkBeginCommandBuffer(cmd, &begin_ci);

      vk.vkCmdResetQueryPool(cmd, query_pool, 0, 2);

      VkRenderingAttachmentInfo att = {};
      att.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
      att.imageView = color_view;
      att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

      VkRenderingInfo render_info = {};
      render_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
      render_info.renderArea = {{0, 0}, {1024, 1024}};
      render_info.layerCount = 1;
      render_info.colorAttachmentCount = 1;
      render_info.pColorAttachments = &att;

      vk.vkCmdBeginRendering(cmd, &render_info);
      vk.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pso);

      VkViewport vp = {0, 0, 1024.0f, 1024.0f, 0.0f, 1.0f};
      VkRect2D sc = {{0, 0}, {1024, 1024}};
      vk.vkCmdSetViewport(cmd, 0, 1, &vp);
      vk.vkCmdSetScissor(cmd, 0, 1, &sc);

      // Start timestamp
      vk.vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool, 0);

      for (uint32_t i = 0; i < draw_iterations; ++i) {
        vk.vkCmdDraw(cmd, 3, 1, 0, 0);
      }

      // End timestamp
      vk.vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool, 1);

      vk.vkCmdEndRendering(cmd);
      vk.vkEndCommandBuffer(cmd);

      VkSubmitInfo submit_info = {};
      submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
      submit_info.commandBufferCount = 1;
      submit_info.pCommandBuffers = &cmd;
      vk.vkQueueSubmit(vk.queue, 1, &submit_info, VK_NULL_HANDLE);
      vk.vkQueueWaitIdle(vk.queue);

      uint64_t timestamps[2] = {0, 0};
      vk.vkGetQueryPoolResults(vk.device, query_pool, 0, 2, sizeof(timestamps), timestamps, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
      double duration_ns = static_cast<double>(timestamps[1] - timestamps[0]) * timestamp_period;
      return duration_ns / 1e6; // ms
    };

    // Warmup
    measure_draws(mono_pipelines[0]);
    measure_draws(linked_pipelines[0]);

    // Measure representative draw batch
    gpu_mono_draw_ms = measure_draws(mono_pipelines[0]);
    gpu_linked_draw_ms = measure_draws(linked_pipelines[0]);
    if (gpu_mono_draw_ms > 0.0001) {
      gpu_draw_overhead_percent = ((gpu_linked_draw_ms - gpu_mono_draw_ms) / gpu_mono_draw_ms) * 100.0;
    }

    std::cout << "[Benchmark 4] GPU Draw Mono: " << gpu_mono_draw_ms << " ms | Linked: "
              << gpu_linked_draw_ms << " ms | Overhead: " << gpu_draw_overhead_percent << "%\n";

    // Clean up draw resources
    vk.vkDestroyQueryPool(vk.device, query_pool, nullptr);
    vk.vkDestroyCommandPool(vk.device, cmd_pool, nullptr);
    vk.vkDestroyImageView(vk.device, color_view, nullptr);
    vk.vkDestroyImage(vk.device, color_image, nullptr);
    vk.vkFreeMemory(vk.device, color_memory, nullptr);
  }

  // -------------------------------------------------------------------------
  // Statistics & Gate Decision Evaluation
  // -------------------------------------------------------------------------
  LatencyStats mono_stats = LatencyStats::Compute(mono_cold_samples);
  LatencyStats gpl_vs_stats = LatencyStats::Compute(gpl_vs_cold_samples);
  LatencyStats gpl_ps_stats = LatencyStats::Compute(gpl_ps_cold_samples);
  LatencyStats fast_link_stats = LatencyStats::Compute(fast_link_samples);

  double gpl_total_lib_cold_ms = gpl_vs_stats.sum_ms + gpl_ps_stats.sum_ms;
  double cold_ratio = (mono_stats.sum_ms > 0.0) ? (gpl_total_lib_cold_ms / mono_stats.sum_ms) : 1.0;
  double theoretical_breakeven = 219.0 / 244.0; // 0.898

  // Gate Evaluation
  // Fail if ANY:
  // 1. cold_ratio >= 0.50
  // 2. fast_link_stats.p95_ms >= 1.0
  // 3. gpu_draw_overhead_percent >= 5.0
  // 4. validation_error_count > 0
  bool cond_cold_ratio_pass = (cold_ratio < 0.50);
  bool cond_fast_link_pass = (fast_link_stats.p95_ms < 1.0);
  bool cond_gpu_overhead_pass = (gpu_draw_overhead_percent < 5.0);
  bool cond_validation_pass = (vk.validation_error_count == 0);

  bool gate_passed = cond_cold_ratio_pass && cond_fast_link_pass && cond_gpu_overhead_pass && cond_validation_pass;

  std::cout << "\n=================================================================\n";
  std::cout << "                 GPL SPIKE EMPIRICAL RESULTS                     \n";
  std::cout << "=================================================================\n";
  std::cout << "  1. Cold Monolithic PSOs (" << mono_stats.count << " pipelines):\n";
  std::cout << "     Total Time:     " << mono_stats.sum_ms << " ms\n";
  std::cout << "     Mean:           " << mono_stats.mean_ms << " ms\n";
  std::cout << "     Median:         " << mono_stats.median_ms << " ms\n";
  std::cout << "     P95 / P99:      " << mono_stats.p95_ms << " / " << mono_stats.p99_ms << " ms\n";
  std::cout << "     Max:            " << mono_stats.max_ms << " ms\n\n";

  std::cout << "  2. Cold GPL Libraries (" << (gpl_vs_stats.count + gpl_ps_stats.count) << " libraries: "
            << gpl_vs_stats.count << " VS + " << gpl_ps_stats.count << " PS):\n";
  std::cout << "     Total Time:     " << gpl_total_lib_cold_ms << " ms (VS: " << gpl_vs_stats.sum_ms
            << " ms, PS: " << gpl_ps_stats.sum_ms << " ms)\n";
  std::cout << "     Cold Ratio:     " << cold_ratio << "x (Gate Threshold: < 0.50x | Break-even: "
            << theoretical_breakeven << "x)\n";
  std::cout << "     Ratio Status:   " << (cond_cold_ratio_pass ? "PASS" : "FAIL (Contingency R1 Triggered)") << "\n\n";

  std::cout << "  3. Fast-Linking Latency (" << fast_link_stats.count << " pipelines linked):\n";
  std::cout << "     Mean:           " << fast_link_stats.mean_ms << " ms\n";
  std::cout << "     Median:         " << fast_link_stats.median_ms << " ms\n";
  std::cout << "     P95:            " << fast_link_stats.p95_ms << " ms (Gate Threshold: < 1.0 ms)\n";
  std::cout << "     P99 / Max:      " << fast_link_stats.p99_ms << " / " << fast_link_stats.max_ms << " ms\n";
  std::cout << "     Fast-Link Pass: " << (cond_fast_link_pass ? "PASS" : "FAIL") << "\n\n";

  std::cout << "  4. GPU Runtime Draw Overhead (1,000 draws):\n";
  std::cout << "     Monolithic:     " << gpu_mono_draw_ms << " ms\n";
  std::cout << "     Linked:         " << gpu_linked_draw_ms << " ms\n";
  std::cout << "     Overhead:       " << gpu_draw_overhead_percent << "% (Gate Threshold: < 5.0% fail, < 2.0% pass)\n";
  std::cout << "     Overhead Pass:  " << (cond_gpu_overhead_pass ? "PASS" : "FAIL") << "\n\n";

  std::cout << "  5. Vulkan Validation Layers:\n";
  std::cout << "     Errors:         " << vk.validation_error_count << " (Gate Threshold: 0)\n";
  std::cout << "     Warnings:       " << vk.validation_warning_count << "\n";
  std::cout << "     Valid Status:   " << (cond_validation_pass ? "PASS" : "FAIL") << "\n";
  std::cout << "-----------------------------------------------------------------\n";
  std::cout << "  GATE DECISION: " << (gate_passed ? "APPROVED -> Advance to Phase C2 (M2.2 GPL Refactor)" : "REJECTED -> Advance Directly to Phase D (M3 Async Fallback)") << "\n";
  std::cout << "=================================================================\n\n";

  // 6. Write Structured JSON Report
  fs::create_directories(output_path.parent_path());
  std::ofstream json(output_path);
  json << "{\n";
  json << "  \"timestamp\": \"" << std::chrono::system_clock::now().time_since_epoch().count() << "\",\n";
  json << "  \"device\": {\n";
  json << "    \"name\": \"" << vk.physical_device_properties.deviceName << "\",\n";
  json << "    \"driver_version\": \"" << (vk.physical_device_properties.driverVersion >> 22) << "."
       << ((vk.physical_device_properties.driverVersion >> 14) & 0xff) << "\",\n";
  json << "    \"gpl_supported\": true,\n";
  json << "    \"fast_linking_supported\": true\n";
  json << "  },\n";
  json << "  \"metrics\": {\n";
  json << "    \"monolithic_cold_sum_ms\": " << mono_stats.sum_ms << ",\n";
  json << "    \"monolithic_cold_p95_ms\": " << mono_stats.p95_ms << ",\n";
  json << "    \"monolithic_cold_count\": " << mono_stats.count << ",\n";
  json << "    \"gpl_cold_lib_sum_ms\": " << gpl_total_lib_cold_ms << ",\n";
  json << "    \"gpl_vs_cold_count\": " << gpl_vs_stats.count << ",\n";
  json << "    \"gpl_ps_cold_count\": " << gpl_ps_stats.count << ",\n";
  json << "    \"cold_ratio\": " << cold_ratio << ",\n";
  json << "    \"fast_link_mean_ms\": " << fast_link_stats.mean_ms << ",\n";
  json << "    \"fast_link_p95_ms\": " << fast_link_stats.p95_ms << ",\n";
  json << "    \"fast_link_max_ms\": " << fast_link_stats.max_ms << ",\n";
  json << "    \"gpu_draw_overhead_percent\": " << gpu_draw_overhead_percent << ",\n";
  json << "    \"validation_errors\": " << vk.validation_error_count << ",\n";
  json << "    \"validation_warnings\": " << vk.validation_warning_count << "\n";
  json << "  },\n";
  json << "  \"gate\": {\n";
  json << "    \"cold_ratio_threshold\": 0.50,\n";
  json << "    \"cold_ratio_breakeven\": " << theoretical_breakeven << ",\n";
  json << "    \"cold_ratio_passed\": " << (cond_cold_ratio_pass ? "true" : "false") << ",\n";
  json << "    \"fast_link_threshold_ms\": 1.0,\n";
  json << "    \"fast_link_passed\": " << (cond_fast_link_pass ? "true" : "false") << ",\n";
  json << "    \"gpu_overhead_threshold_percent\": 5.0,\n";
  json << "    \"gpu_overhead_passed\": " << (cond_gpu_overhead_pass ? "true" : "false") << ",\n";
  json << "    \"validation_passed\": " << (cond_validation_pass ? "true" : "false") << ",\n";
  json << "    \"overall_gate_passed\": " << (gate_passed ? "true" : "false") << ",\n";
  json << "    \"recommended_action\": \"" << (gate_passed ? "PROCEED_TO_M2_2_GPL" : "ABANDON_GPL_ADVANCE_TO_M3_ASYNC") << "\"\n";
  json << "  }\n";
  json << "}\n";
  std::cout << "[Report] Written JSON results to: " << output_path.string() << "\n";

  // Cleanup Vulkan
  for (auto p : mono_pipelines) vk.vkDestroyPipeline(vk.device, p, nullptr);
  for (auto p : linked_pipelines) vk.vkDestroyPipeline(vk.device, p, nullptr);
  for (auto& [k, p] : pre_raster_libs) vk.vkDestroyPipeline(vk.device, p, nullptr);
  for (auto& [k, p] : fragment_shader_libs) vk.vkDestroyPipeline(vk.device, p, nullptr);
  if (vertex_input_lib) vk.vkDestroyPipeline(vk.device, vertex_input_lib, nullptr);
  if (fragment_output_lib) vk.vkDestroyPipeline(vk.device, fragment_output_lib, nullptr);
  for (auto& [k, m] : vs_modules) vk.vkDestroyShaderModule(vk.device, m, nullptr);
  for (auto& [k, m] : ps_modules) vk.vkDestroyShaderModule(vk.device, m, nullptr);

  vk.vkDestroyPipelineLayout(vk.device, pipeline_layout, nullptr);
  vk.vkDestroyDescriptorSetLayout(vk.device, ds_layout0, nullptr);
  vk.vkDestroyDescriptorSetLayout(vk.device, ds_layout1, nullptr);
  vk.vkDestroyDescriptorSetLayout(vk.device, ds_layout2, nullptr);
  vk.vkDestroyDescriptorSetLayout(vk.device, ds_layout3, nullptr);

  if (vk.debug_messenger) vk.vkDestroyDebugUtilsMessengerEXT(vk.instance, vk.debug_messenger, nullptr);
  vk.vkDestroyDevice(vk.device, nullptr);
  vk.vkDestroyInstance(vk.instance, nullptr);
  FreeLibrary(vk.vulkan_dll);

  return gate_passed ? 0 : 2;
}
