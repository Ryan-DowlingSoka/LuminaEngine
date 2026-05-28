#pragma once

#include "VulkanResources.h"
#include "Containers/Array.h"
#include <volk/volk.h>

struct GLFWwindow;

namespace Lumina
{
    class FVulkanRenderContext;
    class FWindow;
}

namespace Lumina
{
    class FVulkanSwapchain
    {
    public:
        
        FVulkanSwapchain() = default;
        ~FVulkanSwapchain();
        
        FVulkanSwapchain(const FVulkanSwapchain&) = delete;
        FVulkanSwapchain& operator=(const FVulkanSwapchain&) = delete;
        FVulkanSwapchain(FVulkanSwapchain&&) = delete;
        FVulkanSwapchain& operator=(FVulkanSwapchain&&) = delete;
        
        
        // Window is a raw GLFWwindow* so secondary ImGui viewport windows (which
        // aren't FWindows) can drive their own swapchain. bPrimary gates the
        // GRenderManager->SwapchainResized broadcast to the one real swapchain.
        void CreateSwapchain(VkInstance Instance, FVulkanRenderContext* InContext, GLFWwindow* Window, FUIntVector2 Extent, bool bFromResize = false, bool bPrimary = true);

        void RecreateSwapchain(const FUIntVector2& Extent);
        void SetPresentMode(VkPresentModeKHR NewMode);

        FORCEINLINE const VkSurfaceFormatKHR& GetSurfaceFormat() const { return SurfaceFormat; }
        FORCEINLINE uint32 GetNumFramesInFlight() const { return (uint32)FramesInFlight.size(); }
        FORCEINLINE uint32 GetCurrentImageIndex() const { return CurrentImageIndex; }
        FORCEINLINE uint32 GetImageCount() const { return (uint32)SwapchainImages.size(); }
        FORCEINLINE VkPresentModeKHR GetPresentMode() const { return CurrentPresentMode; }
        FORCEINLINE VkFormat GetSwapchainFormat() const { return Format; }
        FORCEINLINE const FUIntVector2& GetSwapchainExtent() const { return SwapchainExtent; }
        // The last requested extent (may differ from the actual clamped SwapchainExtent).
        FORCEINLINE const FUIntVector2& GetDesiredExtent() const { return DesiredExtent; }
        
        TRefCountPtr<FVulkanImage> GetCurrentImage() const;
        FORCEINLINE VkSemaphore GetCurrentPresentSemaphore() const { return PresentSemaphores[CurrentImageIndex]; }
        // Signaled by the last successful acquire (null if it failed); passed straight to the swapchain submit.
        FORCEINLINE VkSemaphore GetCurrentAcquireSemaphore() const { return CurrentAcquireSemaphore; }

        bool AcquireNextImage();
        bool Present();
        void WaitForFramePace();
        
    private:

        FVulkanRenderContext*                   Context = nullptr;
        GLFWwindow*                             WindowHandle = nullptr;
        bool                                    bIsPrimarySwapchain = true;
        // Last extent we (re)created at. Secondary swapchains recreate to this on
        // OUT_OF_DATE instead of querying GLFW (not thread-safe off the main thread).
        FUIntVector2                              DesiredExtent = {};
        VkSurfaceKHR                            Surface = VK_NULL_HANDLE;
        VkSwapchainKHR                          Swapchain = VK_NULL_HANDLE;
        uint64                                  AcquireSemaphoreIndex = 0;
        VkSemaphore                             CurrentAcquireSemaphore = VK_NULL_HANDLE;
        
        TVector<TRefCountPtr<FVulkanImage>>     SwapchainImages;
        TVector<VkSemaphore>                    PresentSemaphores;
        TVector<VkSemaphore>                    AcquireSemaphores;
        // Fixed node-pool FIFO: holds at most FRAMES_IN_FLIGHT queries (drained in
        // WaitForFramePace before Present pushes), so it never touches the heap.
        // Was a deque-backed TQueue whose push/pop churned subarrays every frame.
        TFixedList<FRHIEventQueryRef, FRAMES_IN_FLIGHT + 1>  FramesInFlight;
        TFixedVector<FRHIEventQueryRef, 4>      QueryPool;
        
        FUIntVector2                              SwapchainExtent = {};
        
        VkSurfaceFormatKHR                      SurfaceFormat = {};
        
        VkFormat                                Format = VK_FORMAT_MAX_ENUM;
        VkPresentModeKHR                        CurrentPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        uint32                                  CurrentImageIndex = 0;
        
        bool                                    bNeedsResize = false;
    };
    
}
