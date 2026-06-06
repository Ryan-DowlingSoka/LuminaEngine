#pragma once

#include "DescriptorTableManager.h"
#include "Containers/Array.h"
#include "Platform/GenericPlatform.h"
#include "RenderResource.h"

namespace Lumina::RHI
{
    // Stock samplers at fixed slots in the bindless sampler array (binding 2). Keep in lockstep
    // with the SAMPLER_* indices in Engine/Resources/Shaders/Includes/SceneGlobals.slang.
    enum class EBindlessSampler : uint32
    {
        LinearWrap          = 0,
        LinearClamp         = 1,
        LinearMirror        = 2,
        PointWrap           = 3,
        PointClamp          = 4,
        AnisotropicWrap     = 5,
        AnisotropicClamp    = 6,
        ShadowCompare       = 7,
        MinReductionClamp   = 8,
        MaxReductionClamp   = 9,

        Count
    };

    class FTextureManager
    {
    public:

        FTextureManager();

        void AddTexture(FRHIImage* InTexture);
        void RemoveTexture(FRHIImage* InTexture);

        // Register an SRV for a non-default subresource (e.g. one mip), pair with ReleaseSubresourceSRV.
        // Do NOT pass AllSubresources -- it aliases the texture's ResourceID, so releasing frees the live view.
        NODISCARD int32 RegisterSubresourceSRV(FRHIImage* InTexture, const FTextureSubresourceSet& Subresources);
        void ReleaseSubresourceSRV(int32 Index);

        // Retire bindless slots whose deferral window has elapsed. Call once per rendered frame
        // from a GPU-safe point (after the frame fence). Reclaimed slots become reusable.
        void Tick();


        NODISCARD FRHIBindingLayout* GetLayout() const { return Layout; }
        NODISCARD FRHIDescriptorTable* GetDescriptorTable() const { return DescriptorTableManager.GetDescriptorTable(); }
        NODISCARD const FDescriptorTableManager& GetDescriptorManager() const { return DescriptorTableManager; }

        // Diagnostics: live bindless textures vs current table capacity.
        NODISCARD uint32 GetLiveDescriptorCount() const { return DescriptorTableManager.GetLiveDescriptorCount(); }
        NODISCARD uint32 GetDescriptorCapacity()  const { return DescriptorTableManager.GetDescriptorCapacity(); }

    private:

        void RegisterStockSamplers();
        void CreateDefaultImage();

        // Repoint a bindless slot at the always-live 1x1 placeholder. Done before releasing a
        // texture's slot so the slot never references the texture's about-to-be-destroyed view.
        void WriteSlotToPlaceholder(int32 Slot);

        FSharedMutex                              Mutex;
        FDescriptorTableManager                   DescriptorTableManager;
        FRHIBindingLayoutRef                      Layout;
        FRHIImageRef                              DefaultImage;
        TFixedVector<FRHISamplerRef, (size_t)EBindlessSampler::Count> StockSamplers;
    };
}
