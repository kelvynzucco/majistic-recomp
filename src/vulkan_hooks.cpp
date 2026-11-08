// Majin and the Forsaken Kingdom - Vulkan Presentation Pacing & Device Hooks
// Adapted from the Forza Horizon recompilation architecture.

#include "vulkan_hooks.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/ui/vulkan/provider.h>
#include <rex/ui/vulkan/device.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <typeinfo>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#endif

REXCVAR_DEFINE_BOOL(majin_vulkan_pacing, true, "Display",
    "Enable high-precision frame presentation pacing on Vulkan (eliminates GPU spikes and stalls)");

namespace majin {
namespace {

using rex::ui::vulkan::VulkanDevice;

const VulkanDevice* g_device = nullptr;
PFN_vkCreateSwapchainKHR g_create_swapchain = nullptr;
PFN_vkQueuePresentKHR g_queue_present = nullptr;

#if defined(_WIN32)
void PreciseWait(int64_t ns) {
  if (ns <= 0) return;
  static thread_local HANDLE timer = CreateWaitableTimerExW(
      nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  if (!timer) return;
  LARGE_INTEGER due;
  due.QuadPart = -(ns / 100);  // relative, in 100 ns units
  if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
    WaitForSingleObject(timer, INFINITE);
  }
}
#else
void PreciseWait(int64_t ns) {
  if (ns <= 0) return;
  std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
}
#endif

// Target frame interval for 60 FPS: 16,666,666 ns; for 30 FPS: 33,333,333 ns
std::atomic<VkPresentModeKHR> g_active_present_mode{VK_PRESENT_MODE_FIFO_KHR};
std::atomic<std::chrono::steady_clock::time_point> g_last_present_time{std::chrono::steady_clock::now()};

VKAPI_ATTR VkResult VKAPI_CALL CreateSwapchain(
    VkDevice device,
    const VkSwapchainCreateInfoKHR* create_info,
    const VkAllocationCallbacks* allocator,
    VkSwapchainKHR* swapchain) {
  g_active_present_mode.store(create_info->presentMode, std::memory_order_relaxed);
  REXLOG_INFO("[Vulkan Hooks] Swapchain created: {}x{}, format={}, presentMode={}",
      create_info->imageExtent.width, create_info->imageExtent.height,
      uint32_t(create_info->imageFormat), uint32_t(create_info->presentMode));
  return g_create_swapchain(device, create_info, allocator, swapchain);
}

VKAPI_ATTR VkResult VKAPI_CALL QueuePresent(
    VkQueue queue,
    const VkPresentInfoKHR* present_info) {
  // If presentation mode is FIFO (VSync active), presentation is hardware synchronized
  // to vertical blanking. Calling PreciseWait right before vkQueuePresentKHR in FIFO mode
  // causes missed VBlank deadlines and severe presentation stutter.
  // When running non-FIFO (immediate/mailbox) or if pacing is explicitly forced,
  // pace according to the active target framerate (30 vs 60 FPS).
  const auto present_mode = g_active_present_mode.load(std::memory_order_relaxed);
  const bool is_fifo = (present_mode == VK_PRESENT_MODE_FIFO_KHR ||
                        present_mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR);

  if (REXCVAR_GET(majin_vulkan_pacing) && !is_fifo) {
    const bool is_60fps = rex::cvar::Query<bool>("majin_60fps");
    const int64_t target_interval_ns = is_60fps ? 16'666'666 : 33'333'333;

    const auto now = std::chrono::steady_clock::now();
    const auto last = g_last_present_time.load(std::memory_order_relaxed);
    const int64_t elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - last).count();
    const int64_t delay_ns = target_interval_ns - elapsed_ns;

    if (delay_ns > 200'000) {  // only wait if more than 0.2 ms remains
      PreciseWait(delay_ns);
    }
    g_last_present_time.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
  }

  return g_queue_present(queue, present_info);
}

}  // namespace

bool InstallVulkanHooks(rex::system::IGraphicsSystem* graphics) {
  static std::mutex mutex;
  std::lock_guard lock(mutex);
  if (g_device) return true;

  rex::ui::GraphicsProvider* provider = graphics ? graphics->provider() : nullptr;
  if (!provider || !std::strstr(typeid(*provider).name(), "VulkanProvider")) {
    return false;
  }

  auto* vulkan_provider = static_cast<rex::ui::vulkan::VulkanProvider*>(provider);
  VulkanDevice* device = vulkan_provider->vulkan_device();
  if (!device) return false;

  auto& dfn = const_cast<VulkanDevice::Functions&>(device->functions());
  if (!dfn.vkCreateSwapchainKHR || !dfn.vkQueuePresentKHR) {
    return false;
  }

  g_device = device;
  g_create_swapchain = dfn.vkCreateSwapchainKHR;
  dfn.vkCreateSwapchainKHR = CreateSwapchain;
  g_queue_present = dfn.vkQueuePresentKHR;
  dfn.vkQueuePresentKHR = QueuePresent;

  REXLOG_INFO("[Vulkan Hooks] Successfully installed presentation pacing hooks in VulkanDevice table.");
  return true;
}

}  // namespace majin
