#include "pch.h"
#include "TextureManager.h"
#include "RenderContext.h"
#include "RHIGlobals.h"
#include "Core/Assertions/Assert.h"


namespace Lumina::RHI
{
    namespace
    {
        constexpr uint32 ImageBinding   = 0;
        constexpr uint32 SamplerBinding = 1;

        FRHISamplerRef MakeSampler(const FString& DebugName, bool bLinear, bool bMip, ESamplerAddressMode Address, float MaxAnisotropy = 1.0f, ESamplerReductionType Reduction = ESamplerReductionType::Standard)
        {
            FSamplerDesc Desc;
            Desc.DebugName = DebugName;
            Desc.SetAllFilters(bLinear);
            Desc.SetMipFilter(bMip);
            Desc.SetAllAddressModes(Address);
            Desc.SetMaxAnisotropy(MaxAnisotropy);
            Desc.SetReductionType(Reduction);
            Desc.BorderColor = FColor::White;
            return GRenderContext->CreateSampler(Desc);
        }
    }

    FTextureManager::FTextureManager()
    {
        FBindlessLayoutDesc Desc;
        Desc.AddBinding(FBindingLayoutItem::Image_Mutable(ImageBinding));
        Desc.AddBinding(FBindingLayoutItem::Sampler      (SamplerBinding));
        Desc.SetMaxCapacity(UINT16_MAX);
        Desc.SetVisibility(ERHIShaderType::Vertex);
        Desc.SetVisibility(ERHIShaderType::Fragment);
        Desc.SetVisibility(ERHIShaderType::Compute);
        Layout = GRenderContext->CreateBindlessLayout(Desc);

        DescriptorTableManager = FDescriptorTableManager(GRenderContext, Layout);

        RegisterStockSamplers();
    }

    void FTextureManager::RegisterStockSamplers()
    {
        StockSamplers.resize((size_t)EBindlessSampler::Count);

        StockSamplers[(size_t)EBindlessSampler::LinearWrap]        = MakeSampler("BindlessSampler_LinearWrap",       true,  true,  ESamplerAddressMode::Wrap);
        StockSamplers[(size_t)EBindlessSampler::LinearClamp]       = MakeSampler("BindlessSampler_LinearClamp",      true,  true,  ESamplerAddressMode::Clamp);
        StockSamplers[(size_t)EBindlessSampler::LinearMirror]      = MakeSampler("BindlessSampler_LinearMirror",     true,  true,  ESamplerAddressMode::Mirror);
        StockSamplers[(size_t)EBindlessSampler::PointWrap]         = MakeSampler("BindlessSampler_PointWrap",        false, false, ESamplerAddressMode::Wrap);
        StockSamplers[(size_t)EBindlessSampler::PointClamp]        = MakeSampler("BindlessSampler_PointClamp",       false, false, ESamplerAddressMode::Clamp);
        StockSamplers[(size_t)EBindlessSampler::AnisotropicWrap]   = MakeSampler("BindlessSampler_AnisoWrap",        true,  true,  ESamplerAddressMode::Wrap,  16.0f);
        StockSamplers[(size_t)EBindlessSampler::AnisotropicClamp]  = MakeSampler("BindlessSampler_AnisoClamp",       true,  true,  ESamplerAddressMode::Clamp, 16.0f);
        StockSamplers[(size_t)EBindlessSampler::ShadowCompare]     = MakeSampler("BindlessSampler_ShadowCompare",    true,  true,  ESamplerAddressMode::Clamp, 1.0f, ESamplerReductionType::Comparison);
        StockSamplers[(size_t)EBindlessSampler::MinReductionClamp] = MakeSampler("BindlessSampler_MinReduction",     true,  true,  ESamplerAddressMode::Clamp, 1.0f, ESamplerReductionType::Minimum);
        StockSamplers[(size_t)EBindlessSampler::MaxReductionClamp] = MakeSampler("BindlessSampler_MaxReduction",     true,  true,  ESamplerAddressMode::Clamp, 1.0f, ESamplerReductionType::Max);

        for (size_t i = 0; i < StockSamplers.size(); ++i)
        {
            FBindingSetItem Item = FBindingSetItem::Sampler(SamplerBinding, StockSamplers[i]);
            Item.Slot = static_cast<uint32>(i);
            GRenderContext->WriteDescriptorTable(DescriptorTableManager.GetDescriptorTable(), Item);
        }
    }

    void FTextureManager::AddTexture(FRHIImage* InTexture)
    {
        if (InTexture == nullptr || InTexture->GetResourceID() != -1)
        {
            return;
        }

        FWriteScopeLock Lock(Mutex);

        // All-mips SRV; per-mip UAVs use distinct array indices and don't clobber this slot.
        FBindingSetItem SRVItem = FBindingSetItem::TextureSRV(ImageBinding, InTexture);
        const int64 Index = DescriptorTableManager.CreateDescriptor(SRVItem);
        InTexture->SetResourceID(static_cast<int32>(Index));

        // Per-mip storage views for SPD-style passes that write each mip via the
        // bindless RW table without allocating a per-pass binding set.
        const FRHIImageDesc& Desc = InTexture->GetDescription();
        if (Desc.Flags.IsFlagSet(EImageCreateFlags::Storage) && Desc.NumMips > 1)
        {
            TVector<int32>& MipIndices = InTexture->GetMipUAVIndices();
            MipIndices.resize(Desc.NumMips);
            for (uint8 Mip = 0; Mip < Desc.NumMips; ++Mip)
            {
                FBindingSetItem UAVItem = FBindingSetItem::TextureUAV(
                    ImageBinding, InTexture, Desc.Format,
                    FTextureSubresourceSet(Mip, 1, 0, FTextureSubresourceSet::AllArraySlices));
                MipIndices[Mip] = static_cast<int32>(DescriptorTableManager.CreateDescriptor(UAVItem));
            }
        }
    }

    int32 FTextureManager::RegisterSubresourceSRV(FRHIImage* InTexture, const FTextureSubresourceSet& Subresources)
    {
        if (InTexture == nullptr)
        {
            return -1;
        }

        FWriteScopeLock Lock(Mutex);

        FBindingSetItem Item = FBindingSetItem::TextureSRV(
            ImageBinding, InTexture, nullptr, InTexture->GetDescription().Format, Subresources);
        return static_cast<int32>(DescriptorTableManager.CreateDescriptor(Item));
    }

    void FTextureManager::ReleaseSubresourceSRV(int32 Index)
    {
        if (Index < 0)
        {
            return;
        }

        FWriteScopeLock Lock(Mutex);
        DescriptorTableManager.ReleaseDescriptor(Index);
    }

    void FTextureManager::RemoveTexture(FRHIImage* InTexture)
    {
        if (InTexture == nullptr)
        {
            return;
        }

        FWriteScopeLock Lock(Mutex);

        TVector<int32>& MipIndices = InTexture->GetMipUAVIndices();
        for (int32 MipIndex : MipIndices)
        {
            if (MipIndex >= 0)
            {
                DescriptorTableManager.ReleaseDescriptor(MipIndex);
            }
        }
        MipIndices.clear();

        const int32 Index = InTexture->GetResourceID();
        if (Index == -1)
        {
            return;
        }

        DescriptorTableManager.ReleaseDescriptor(Index);
        InTexture->SetResourceID(-1);
    }
}
