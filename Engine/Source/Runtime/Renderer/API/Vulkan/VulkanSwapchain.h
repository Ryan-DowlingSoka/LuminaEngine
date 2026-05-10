#pragma once

#include "VulkanResources.h"
#include "Containers/Array.h"
#include <volk/volk.h>

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
        
        
        void CreateSwapchain(VkInstance Instance, FVulkanRenderContext* InContext, FWindow* Window, glm::uvec2 Extent, bool bFromResize = false);

        void RecreateSwapchain(const glm::uvec2& Extent);
        void SetPresentMode(VkPresentModeKHR NewMode);

        FORCEINLINE const VkSurfaceFormatKHR& GetSurfaceFormat() const { return SurfaceFormat; }
        FORCEINLINE uint32 GetNumFramesInFlight() const { return (uint32)FramesInFlight.size(); }
        FORCEINLINE uint32 GetCurrentImageIndex() const { return CurrentImageIndex; }
        FORCEINLINE uint32 GetImageCount() const { return (uint32)SwapchainImages.size(); }
        FORCEINLINE VkPresentModeKHR GetPresentMode() const { return CurrentPresentMode; }
        FORCEINLINE VkFormat GetSwapchainFormat() const { return Format; }
        FORCEINLINE const glm::uvec2& GetSwapchainExtent() const { return SwapchainExtent; }
        
        TRefCountPtr<FVulkanImage> GetCurrentImage() const;

        bool AcquireNextImage();
        bool Present();
        
    private:

        FVulkanRenderContext*                   Context = nullptr;
        VkSurfaceKHR                            Surface = VK_NULL_HANDLE;
        VkSwapchainKHR                          Swapchain = VK_NULL_HANDLE;
        uint64                                  AcquireSemaphoreIndex = 0;
        
        TVector<TRefCountPtr<FVulkanImage>>     SwapchainImages;
        TVector<VkSemaphore>                    PresentSemaphores;
        TVector<VkSemaphore>                    AcquireSemaphores;
        TQueue<FRHIEventQueryRef>               FramesInFlight;
        TFixedVector<FRHIEventQueryRef, 4>      QueryPool;
        
        glm::uvec2                              SwapchainExtent = {};
        
        VkSurfaceFormatKHR                      SurfaceFormat = {};
        
        VkFormat                                Format = VK_FORMAT_MAX_ENUM;
        VkPresentModeKHR                        CurrentPresentMode = VK_PRESENT_MODE_FIFO_KHR;
        uint32                                  CurrentImageIndex = 0;
        
        bool                                    bNeedsResize = false;
    };
    
}
