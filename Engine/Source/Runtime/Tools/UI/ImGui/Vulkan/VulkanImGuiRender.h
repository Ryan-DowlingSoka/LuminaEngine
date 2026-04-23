#pragma once

#include <volk/volk.h>
#include <vk_mem_alloc.h>
#include "Memory/SmartPtr.h"
#include "Tools/UI/ImGui/ImGuiRenderer.h"

namespace Lumina
{
    class FVulkanRenderContext;
    class FUpdateContext;

    class FVulkanImGuiRender: public IImGuiRenderer
    {
    public:
        
        enum class ETextureState : uint8
        {
            Empty,
            Loading,
            Ready,
        };
        
        struct FEntry
        {
            FName Name;
            TAtomic<uint64> LastUseFrame{0};
            TAtomic<ETextureState> State = ETextureState::Empty;
            FRHIImageRef RHIImage;
            ImTextureRef ImTexture;
            // Cached view used to create the descriptor set. If the image's view
            // for this subresource ever changes, we rebuild the entry.
            VkImageView CachedView = VK_NULL_HANDLE;
        };
        
        void Initialize() override;
        void Deinitialize() override;
        
        void OnStartFrame(const FUpdateContext& UpdateContext) override;
        void OnEndFrame(const FUpdateContext& UpdateContext, ICommandList& CmdList) override;

        void DrawRenderDebugInformationWindow(bool* bOpen, const FUpdateContext& InContext) override;

        /** An ImTextureID in this context is castable to a VkDescriptorSet. */
        RUNTIME_API ImTextureID GetOrCreateImTexture(FStringView Path) override;
        RUNTIME_API ImTextureID GetOrCreateImTexture(FRHIImage* Image, const FTextureSubresourceSet& Subresources = AllSubresources) override;
        void DestroyImTexture(uint64 Hash) override;
    
    private:

        void DrawOverviewTab(const VkPhysicalDeviceProperties& props, const VkPhysicalDeviceMemoryProperties& memProps, VmaAllocator Allocator);
        void DrawMemoryTab(const VkPhysicalDeviceMemoryProperties& memProps, VmaAllocator Allocator);
        void DrawResourcesTab();
        void DrawDeviceInfoTab(const VkPhysicalDeviceProperties& props, const VkPhysicalDeviceFeatures& Features);
        void DrawDeviceProperties(const VkPhysicalDeviceProperties& props);
        void DrawDeviceFeatures(const VkPhysicalDeviceFeatures& Features);
        void DrawDeviceLimits(const VkPhysicalDeviceProperties& props);
        void DrawGeneralLimits(const VkPhysicalDeviceLimits& limits);
        void DrawBufferImageLimits(const VkPhysicalDeviceLimits& limits);
        void DrawComputeLimits(const VkPhysicalDeviceLimits& limits);
        void DrawDescriptorLimits(const VkPhysicalDeviceLimits& limits);
        void DrawRenderingLimits(const VkPhysicalDeviceLimits& limits);
        
    
    private:

        FRHIShaderRef VertexShader;
        FRHIShaderRef PixelShader;
        FRHIInputLayoutRef ShaderAttribLayout;

        FRHIImageRef FontTexture;
        FRHISamplerRef FontSampler;

        FRHIBufferRef VertexBuffer;
        FRHIBufferRef IndexBuffer;

        FRHIBindingLayoutRef BindingLayout;
        FGraphicsPipelineDesc BasePSODesc;

        FRHIGraphicsPipelineRef Pipeline;

        TVector<ImDrawVert> VTXBuffer;
        TVector<ImDrawIdx> IDXBuffer;


        mutable FRecursiveMutex                 Mutex;

        THashMap<uint64, TUniquePtr<FEntry>>    Images;

        // Frame of the last stale-entry sweep; the sweep is amortized so we
        // don't walk the entire map every frame.
        uint64                                  LastCleanupFrame = 0;

        TPair<FName, FEntry*>                   SquareWhiteTexture;
        
        VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
        FVulkanRenderContext* VulkanRenderContext = nullptr;

        TFixedVector<float, 300> VRAMHistory;
        TFixedVector<FRHIImageRef, 10> ReferencedImages;
    };
}
