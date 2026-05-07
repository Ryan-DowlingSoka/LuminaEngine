#include "pch.h"
#include "TextureManager.h"
#include "RenderContext.h"
#include "RHIGlobals.h"
#include "Core/Assertions/Assert.h"


namespace Lumina::RHI
{
    FTextureManager::FTextureManager()
    {
    	FBindlessLayoutDesc Desc;
    	Desc.AddBinding(FBindingLayoutItem::Texture_SRV(0));
    	Desc.SetMaxCapacity(UINT16_MAX);
    	Desc.SetVisibility(ERHIShaderType::Fragment);
    	Layout = GRenderContext->CreateBindlessLayout(Desc);
    	
    	DescriptorTableManager = FDescriptorTableManager(GRenderContext, Layout);
    }
	
    void FTextureManager::AddTexture(FRHIImage* InTexture)
    {
    	FWriteScopeLock Lock(Mutex);
    	
		DEBUG_ASSERT(InTexture->GetTextureCacheIndex() == -1);
    	
    	FBindingSetItem Item = FBindingSetItem::TextureSRV(0, InTexture);
    	
    	int64 Index = DescriptorTableManager.CreateDescriptor(Item);
    	InTexture->SetTextureCacheIndex(static_cast<int32>(Index));
    }

    void FTextureManager::RemoveTexture(const FRHIImage* InTexture)
    {
    	FWriteScopeLock Lock(Mutex);

    	DescriptorTableManager.ReleaseDescriptor(InTexture->GetTextureCacheIndex());
    }
}
