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
