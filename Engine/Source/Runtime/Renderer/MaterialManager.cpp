#include "pch.h"
#include "MaterialManager.h"
#include "MaterialTypes.h"
#include "RenderContext.h"
#include "RenderThread.h"
#include "RHIGlobals.h"
#include "Assets/AssetTypes/Material/MaterialInterface.h"

namespace Lumina::RHI
{
    FMaterialManager::FMaterialManager()
    {
        constexpr auto MaxMaterialUniforms = 1024;
        
        FRHIBufferDesc Desc;
        Desc.Size   = sizeof(FMaterialUniforms) * MaxMaterialUniforms;
        Desc.Stride = sizeof(FMaterialUniforms);
        Desc.Usage.SetFlag(EBufferUsageFlags::StorageBuffer);
        Desc.DebugName = "Material Uniforms";
        Desc.InitialState = EResourceStates::ShaderResource;
        Desc.bKeepInitialState = true;
        
        MaterialBuffer = GRenderContext->CreateBuffer(Desc);

        for (int i = 0; i < MaxMaterialUniforms; ++i)
        {
            FreeList.push(i);
        }
    }

    void FMaterialManager::AddMaterial(CMaterialInterface* Material)
    {
        FWriteScopeLock Lock(Mutex);

        DEBUG_ASSERT(Material != nullptr);
        DEBUG_ASSERT(Material->GetMaterialIndex() == -1);
        DEBUG_ASSERT(!FreeList.empty());
        
        uint32 FreeIndex = FreeList.top();
        FreeList.pop();
        
        Material->SetMaterialIndex((int32)FreeIndex);
        UpdateMaterialUniforms(Material->GetMaterialUniforms(), FreeIndex);
    }

    void FMaterialManager::RemoveMaterial(CMaterialInterface* Material)
    {
        FWriteScopeLock Lock(Mutex);

        DEBUG_ASSERT(Material != nullptr);
        DEBUG_ASSERT(Material->GetMaterialIndex() != -1);

        int32 MaterialIndex = Material->GetMaterialIndex();
        
        UpdateMaterialUniforms(nullptr, MaterialIndex);

        Material->SetMaterialIndex(-1);
        
        FreeList.push(MaterialIndex);
    }

    void FMaterialManager::UpdateMaterialUniforms(const FMaterialUniforms* InUniforms, uint32 Index)
    {
        TOptional<FMaterialUniforms> MaybeCopy;
        
        if (InUniforms)
        {
            MaybeCopy = *InUniforms;
        }
        
        ENQUEUE_RENDER_COMMAND(UpdateMaterialUniforms)([this, Index, MaybeCopy]
        {
            FRHICommandListRef CommandList = GRenderContext->CreateCommandList(FCommandListInfo::Graphics());
            CommandList->Open();
            
            if (!MaybeCopy.has_value())
            {
                CommandList->FillBuffer(MaterialBuffer, 0u, sizeof(FMaterialUniforms), Index * sizeof(FMaterialUniforms));
            }
            else
            {
                CommandList->WriteBuffer(MaterialBuffer, &*MaybeCopy, sizeof(FMaterialUniforms), Index * sizeof(FMaterialUniforms));
            }
            CommandList->Close();
            
            GRenderContext->ExecuteCommandList(CommandList); 
        });
    }
}
