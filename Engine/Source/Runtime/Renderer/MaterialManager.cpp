#include "pch.h"
#include "MaterialManager.h"
#include "MaterialTypes.h"
#include "RenderContext.h"
#include "RHIGlobals.h"
#include "Assets/AssetTypes/Material/MaterialInterface.h"
#include "Memory/Memcpy.h"

namespace Lumina::RHI
{
    FMaterialManager::FMaterialManager()
    {
        FRHIBufferDesc Desc;
        Desc.Size   = sizeof(FMaterialUniforms) * 2000;
        Desc.Stride = sizeof(FMaterialUniforms);
        Desc.Usage.SetFlag(EBufferUsageFlags::StorageBuffer);
        Desc.DebugName = "Material Uniforms";
        Desc.InitialState = EResourceStates::UnorderedAccess;
        Desc.bKeepInitialState = true;
        
        MaterialBuffer = GRenderContext->CreateBuffer(Desc);
    }

    void FMaterialManager::AddMaterial(CMaterialInterface* Material)
    {
        FWriteScopeLock Lock(Mutex);

        DEBUG_ASSERT(Material != nullptr);
        DEBUG_ASSERT(Material->GetMaterialIndex() == -1);
        
        Material->SetMaterialIndex(MaterialUniforms.size());
        MaterialMap.emplace(Material->GetMaterialIndex(), Material);
        MaterialUniforms.emplace_back(*Material->GetMaterialUniforms());
        UpdateMaterialUniforms(Material);
    }

    void FMaterialManager::RemoveMaterial(CMaterialInterface* Material)
    {
        FWriteScopeLock Lock(Mutex);

        DEBUG_ASSERT(Material != nullptr);
        DEBUG_ASSERT(Material->GetMaterialIndex() != -1);

        int32 MaterialIndex = Material->GetMaterialIndex();
        int32 LastMaterialIndex = MaterialUniforms.size() - 1;

        MaterialMap.erase(MaterialIndex);

        if (MaterialIndex != LastMaterialIndex)
        {
            CMaterialInterface* MovedMaterial = MaterialMap[LastMaterialIndex];

            MaterialUniforms.erase_unsorted(MaterialUniforms.begin() + MaterialIndex);

            MaterialMap.erase(LastMaterialIndex);
            MaterialMap.insert_or_assign(MaterialIndex, MovedMaterial);

            MovedMaterial->SetMaterialIndex(MaterialIndex);

            UpdateMaterialUniforms(MovedMaterial);
        }
        else
        {
            MaterialUniforms.pop_back();
        }

        Material->SetMaterialIndex(-1);
    }

    void FMaterialManager::UpdateMaterialUniforms(CMaterialInterface* Material)
    {
        const int32 Index = Material->GetMaterialIndex();
        FMaterialUniforms& Uniforms = MaterialUniforms[Index];
        Memory::Memcpy(&Uniforms, Material->GetMaterialUniforms(), sizeof(FMaterialUniforms));

        FRHICommandListRef CommandList = GRenderContext->CreateCommandList(FCommandListInfo::Graphics());
        CommandList->Open();
        CommandList->WriteBuffer(MaterialBuffer, &Uniforms, sizeof(FMaterialUniforms), Index * sizeof(FMaterialUniforms));
        CommandList->Close();

        GRenderContext->ExecuteCommandList(CommandList);
    }
}
