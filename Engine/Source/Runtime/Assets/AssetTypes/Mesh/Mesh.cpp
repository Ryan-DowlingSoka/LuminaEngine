#include "pch.h"
#include "Mesh.h"
#include "Assets/AssetTypes/Material/Material.h"
#include "assets/assettypes/material/materialinstance.h"
#include "Core/Object/Cast.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RHIGlobals.h"
#include "Renderer/Vertex.h"
#include "Tools/Import/ImportHelpers.h"


namespace Lumina
{
    void CMesh::Serialize(FArchive& Ar)
    {
        Super::Serialize(Ar);

        if (!MeshResources)
        {
            MeshResources = MakeUnique<FMeshResource>();
        }
        
        Ar << *MeshResources;
    }

    void CMesh::PostLoad()
    {
        GenerateBoundingBox();

        if (MeshResources && MeshResources->ShadowIndices.empty())
        {
            Import::Mesh::GenerateShadowBuffers(*MeshResources);
        }

        if (MeshResources && MeshResources->MeshletData.IsEmpty())
        {
            Import::Mesh::GenerateMeshlets(*MeshResources);
        }
        GenerateGPUBuffers();
    }

    CMaterialInterface* CMesh::GetMaterialAtSlot(size_t Slot) const
    {
        if (Materials.size() <= Slot)
        {
            return nullptr;
        }
        
        return Materials.empty() ? nullptr : Materials[Slot].Get();
    }

    void CMesh::SetMaterialAtSlot(size_t Slot, CMaterialInterface* NewMaterial)
    {
        if (Materials.size() <= Slot)
        {
            Materials.push_back(NewMaterial);
        }
        else
        {
            Materials[Slot] = NewMaterial;
        }  
    }

    void CMesh::SetMeshResource(TUniquePtr<FMeshResource>&& NewResource)
    {
        MeshResources = eastl::move(NewResource);
        GenerateBoundingBox();
        if (MeshResources && MeshResources->ShadowIndices.empty())
        {
            Import::Mesh::GenerateShadowBuffers(*MeshResources);
        }
        if (MeshResources && MeshResources->MeshletData.IsEmpty())
        {
            Import::Mesh::GenerateMeshlets(*MeshResources);
        }
        GenerateGPUBuffers();
    }

    bool CMesh::IsReadyForRender() const
    {
        LUMINA_PROFILE_SCOPE();

        if (HasAnyFlag(OF_NeedsLoad))
        {
            return false;
        }

        for (CMaterialInterface* Material : Materials)
        {
            if (Material == nullptr)
            {
                return false;
            }

            if (Material->IsReadyForRender() == false)
            {
                return false;
            }
        }

        return !Materials.empty();
    }

    void CMesh::GenerateBoundingBox()
    {
        BoundingBox.Min = { FLT_MAX, FLT_MAX, FLT_MAX };
        BoundingBox.Max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
        
        for (size_t i = 0; i < MeshResources->GetNumVertices(); ++i)
        {
            eastl::visit([&](auto& Vertex)
            {
                MeshResources->ExpandBounds(Vertex[i], BoundingBox);
            }, MeshResources->Vertices);
        }
    }

    void CMesh::GenerateGPUBuffers()
    {
        FRHICommandListRef CommandList = GRenderContext->CreateCommandList(FCommandListInfo::Graphics());
        CommandList->Open();
        
        uint64 VertexSize = MeshResources->GetVertexTypeSize() * MeshResources->GetNumVertices();

        FRHIBufferDesc VertexBufferDesc;
        VertexBufferDesc.Size = VertexSize;
        VertexBufferDesc.Usage.SetFlag(BUF_VertexBuffer);
        VertexBufferDesc.DebugName = GetName().ToString() + " Vertex Buffer";
        MeshResources->MeshBuffers.VertexBuffer = GRenderContext->CreateBuffer(VertexBufferDesc);
        
        CommandList->BeginTrackingBufferState(MeshResources->MeshBuffers.VertexBuffer, EResourceStates::CopyDest);
        CommandList->WriteBuffer(MeshResources->MeshBuffers.VertexBuffer, MeshResources->GetVertexData(), VertexBufferDesc.Size);
        CommandList->SetPermanentBufferState(MeshResources->MeshBuffers.VertexBuffer, EResourceStates::VertexBuffer);

        uint64 IndexSize = sizeof(uint32) * MeshResources->Indices.size();
        
        FRHIBufferDesc IndexBufferDesc;
        IndexBufferDesc.Size = IndexSize;
        IndexBufferDesc.Usage.SetFlag(BUF_IndexBuffer);
        IndexBufferDesc.DebugName = GetName().ToString() + " Index Buffer";
        MeshResources->MeshBuffers.IndexBuffer = GRenderContext->CreateBuffer(IndexBufferDesc);
        
        CommandList->BeginTrackingBufferState(MeshResources->MeshBuffers.IndexBuffer, EResourceStates::CopyDest);
        CommandList->WriteBuffer(MeshResources->MeshBuffers.IndexBuffer, MeshResources->Indices.data(), IndexBufferDesc.Size);
        CommandList->SetPermanentBufferState(MeshResources->MeshBuffers.IndexBuffer, EResourceStates::IndexBuffer);

        // Position-only shadow index buffer: shadow passes pull through this
        // buffer via vertex pulling, so depth-only draws hit fewer unique
        // vertex-shader invocations (see MeshImport::GenerateShadowBuffers).
        // Legacy assets imported before shadow indices were generated fall
        // back to aliasing the regular index buffer so rendering stays
        // correct without a reimport.
        if (!MeshResources->ShadowIndices.empty())
        {
            const uint64 ShadowIndexSize = sizeof(uint32) * MeshResources->ShadowIndices.size();

            FRHIBufferDesc ShadowIndexBufferDesc;
            ShadowIndexBufferDesc.Size      = ShadowIndexSize;
            ShadowIndexBufferDesc.Usage.SetFlag(BUF_IndexBuffer);
            ShadowIndexBufferDesc.DebugName = GetName().ToString() + " Shadow Index Buffer";
            MeshResources->MeshBuffers.ShadowIndexBuffer = GRenderContext->CreateBuffer(ShadowIndexBufferDesc);

            CommandList->BeginTrackingBufferState(MeshResources->MeshBuffers.ShadowIndexBuffer, EResourceStates::CopyDest);
            CommandList->WriteBuffer(MeshResources->MeshBuffers.ShadowIndexBuffer, MeshResources->ShadowIndices.data(), ShadowIndexSize);
            CommandList->SetPermanentBufferState(MeshResources->MeshBuffers.ShadowIndexBuffer, EResourceStates::IndexBuffer);
        }
        else
        {
            MeshResources->MeshBuffers.ShadowIndexBuffer = MeshResources->MeshBuffers.IndexBuffer;
        }


        CommandList->Close();
        GRenderContext->ExecuteCommandList(CommandList, ECommandQueue::Graphics);
        
        
        if (!MeshResources->bSkinnedMesh)
        {
            FVertexAttributeDesc VertexDesc[4];
            // Pos
            VertexDesc[0].SetElementStride(sizeof(FVertex));
            VertexDesc[0].SetOffset(offsetof(FVertex, Position));
            VertexDesc[0].Format = EFormat::RGB32_FLOAT;
        
            // Normal
            VertexDesc[1].SetElementStride(sizeof(FVertex));
            VertexDesc[1].SetOffset(offsetof(FVertex, Normal));
            VertexDesc[1].Format = EFormat::R32_UINT;
        
            // UV
            VertexDesc[2].SetElementStride(sizeof(FVertex));
            VertexDesc[2].SetOffset(offsetof(FVertex, UV));
            VertexDesc[2].Format = EFormat::RG16_UINT;
            
            // Color
            VertexDesc[3].SetElementStride(sizeof(FVertex));
            VertexDesc[3].SetOffset(offsetof(FVertex, Color));
            VertexDesc[3].Format = EFormat::RGBA8_UNORM;
            
            MeshResources->VertexLayout = GRenderContext->CreateInputLayout(VertexDesc, std::size(VertexDesc));
        }
        else
        {
            FVertexAttributeDesc VertexDesc[6];
            // Pos
            VertexDesc[0].SetElementStride(sizeof(FSkinnedVertex));
            VertexDesc[0].SetOffset(offsetof(FSkinnedVertex, Position));
            VertexDesc[0].Format = EFormat::RGB32_FLOAT;
        
            // Normal
            VertexDesc[1].SetElementStride(sizeof(FSkinnedVertex));
            VertexDesc[1].SetOffset(offsetof(FSkinnedVertex, Normal));
            VertexDesc[1].Format = EFormat::R32_UINT;
        
            // UV
            VertexDesc[2].SetElementStride(sizeof(FSkinnedVertex));
            VertexDesc[2].SetOffset(offsetof(FSkinnedVertex, UV));
            VertexDesc[2].Format = EFormat::RG16_UINT;
            
            // Color
            VertexDesc[3].SetElementStride(sizeof(FSkinnedVertex));
            VertexDesc[3].SetOffset(offsetof(FSkinnedVertex, Color));
            VertexDesc[3].Format = EFormat::RGBA8_UNORM;
            
            // Joint Indices
            VertexDesc[4].SetElementStride(sizeof(FSkinnedVertex));
            VertexDesc[4].SetOffset(offsetof(FSkinnedVertex, JointIndices));
            VertexDesc[4].Format = EFormat::RGBA8_UINT;
            
            // Joint Weights
            VertexDesc[5].SetElementStride(sizeof(FSkinnedVertex));
            VertexDesc[5].SetOffset(offsetof(FSkinnedVertex, JointWeights));
            VertexDesc[5].Format = EFormat::RGBA8_UINT;
            
            MeshResources->VertexLayout = GRenderContext->CreateInputLayout(VertexDesc, std::size(VertexDesc));
        }
    }
}
