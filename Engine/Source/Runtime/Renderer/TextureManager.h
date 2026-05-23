#pragma once

#include "DescriptorTableManager.h"
#include "Containers/Array.h"
#include "Platform/GenericPlatform.h"
#include "RenderResource.h"

namespace Lumina::RHI
{
    // Stock samplers stored at fixed slots in the bindless sampler array (binding 2).
    // Mirrored in shader headers as SAMPLER_* indices; keep this enum in lockstep
    // with Engine/Resources/Shaders/Includes/SceneGlobals.slang.
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

        // Register an SRV for a non-default subresource (e.g. a single mip) and
        // return its bindless index; pair with ReleaseSubresourceSRV. The
        // auto-registered ResourceID is the all-mips view, so tools that must
        // display a specific mip/slice (texture editor) go through this. Do NOT
        // pass AllSubresources here -- that aliases the texture's own ResourceID
        // and releasing it would free the live default view.
        NODISCARD int32 RegisterSubresourceSRV(FRHIImage* InTexture, const FTextureSubresourceSet& Subresources);
        void ReleaseSubresourceSRV(int32 Index);


        NODISCARD FRHIBindingLayout* GetLayout() const { return Layout; }
        NODISCARD FRHIDescriptorTable* GetDescriptorTable() const { return DescriptorTableManager.GetDescriptorTable(); }

        // Diagnostics: live bindless textures vs current table capacity.
        NODISCARD uint32 GetLiveDescriptorCount() const { return DescriptorTableManager.GetLiveDescriptorCount(); }
        NODISCARD uint32 GetDescriptorCapacity()  const { return DescriptorTableManager.GetDescriptorCapacity(); }

    private:

        void RegisterStockSamplers();

        FSharedMutex                              Mutex;
        FDescriptorTableManager                   DescriptorTableManager;
        FRHIBindingLayoutRef                      Layout;
        TFixedVector<FRHISamplerRef, (size_t)EBindlessSampler::Count> StockSamplers;
    };
}
