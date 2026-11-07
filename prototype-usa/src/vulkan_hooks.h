// Majin and the Forsaken Kingdom - Vulkan Presentation Pacing & Device Hooks
// Inspired by the Forza Horizon recompilation architecture.

#pragma once

namespace rex::system {
class IGraphicsSystem;
}

namespace majin {

// Installs Vulkan device function hooks (vkQueuePresentKHR, vkCreateSwapchainKHR)
// into VulkanDevice::Functions table. Ensures rock-solid 60 FPS presentation pacing
// and eliminates GPU queue spikes and stalls.
bool InstallVulkanHooks(rex::system::IGraphicsSystem* graphics);

}  // namespace majin
