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

        // Fallback for procedurally-generated meshes that bypass the import finalize pass.
        if (MeshResources && MeshResources->MeshletData.IsEmpty() && !MeshResources->Indices.empty())
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

        // ThumbnailManager's primitive meshes arrive without baked meshlets.
        if (MeshResources && MeshResources->MeshletData.IsEmpty() && !MeshResources->Indices.empty())
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

        if (MeshResources && MeshResources->GetNumVertices() > 0)
        {
            for (const glm::vec3& P : MeshResources->Positions)
            {
                BoundingBox.Min = glm::min(BoundingBox.Min, P);
                BoundingBox.Max = glm::max(BoundingBox.Max, P);
            }
            return;
        }

        // Loaded asset: Vertices is transient, derive the AABB from per-meshlet LoInt against the grid.
        if (MeshResources && !MeshResources->MeshletData.IsEmpty())
        {
            const FMeshletData& MD       = MeshResources->MeshletData;
            const glm::vec3     Origin   = MD.MeshOrigin;
            const glm::vec3     GridStep = MD.MeshGridStep;

            for (const FMeshlet& M : MD.Meshlets)
            {
                const glm::vec3 Lo = Origin + glm::vec3(M.LoInt) * GridStep;
                const glm::vec3 Hi = Lo + glm::vec3(1023.0f) * GridStep;
                BoundingBox.Min = glm::min(BoundingBox.Min, Lo);
                BoundingBox.Max = glm::max(BoundingBox.Max, Hi);
            }
        }
    }

    void CMesh::GenerateGPUBuffers()
    {
        FRHICommandListRef CommandList = GRenderContext->CreateCommandList(FCommandListInfo::Graphics());
        CommandList->Open();

        if (!MeshResources->MeshletData.IsEmpty())
        {
            const FMeshletData& MData = MeshResources->MeshletData;
            const bool bSkinned       = MeshResources->bSkinnedMesh;

            const uint64 MeshletsSize = sizeof(FMeshlet) * MData.Meshlets.size();
            FRHIBufferDesc MeshletsDesc;
            MeshletsDesc.Size       = MeshletsSize;
            MeshletsDesc.Stride     = sizeof(FMeshlet);
            MeshletsDesc.Usage.SetFlag(BUF_StorageBuffer);
            MeshletsDesc.DebugName  = GetName().ToString() + " Meshlets";
            MeshResources->MeshBuffers.MeshletBuffer = GRenderContext->CreateBuffer(MeshletsDesc);

            CommandList->BeginTrackingBufferState(MeshResources->MeshBuffers.MeshletBuffer, EResourceStates::CopyDest);
            CommandList->WriteBuffer(MeshResources->MeshBuffers.MeshletBuffer, MData.Meshlets.data(), MeshletsSize);
            CommandList->SetPermanentBufferState(MeshResources->MeshBuffers.MeshletBuffer, EResourceStates::ShaderResource);

            const uint64 BoundsSize = sizeof(FMeshletBounds) * MData.MeshletBounds.size();
            FRHIBufferDesc BoundsDesc;
            BoundsDesc.Size       = BoundsSize;
            BoundsDesc.Stride     = sizeof(FMeshletBounds);
            BoundsDesc.Usage.SetFlag(BUF_StorageBuffer);
            BoundsDesc.DebugName  = GetName().ToString() + " Meshlet Bounds";
            MeshResources->MeshBuffers.MeshletBoundsBuffer = GRenderContext->CreateBuffer(BoundsDesc);

            CommandList->BeginTrackingBufferState(MeshResources->MeshBuffers.MeshletBoundsBuffer, EResourceStates::CopyDest);
            CommandList->WriteBuffer(MeshResources->MeshBuffers.MeshletBoundsBuffer, MData.MeshletBounds.data(), BoundsSize);
            CommandList->SetPermanentBufferState(MeshResources->MeshBuffers.MeshletBoundsBuffer, EResourceStates::ShaderResource);

            const void*  VertSrc    = bSkinned ? (const void*)MData.MeshletSkinnedVertices.data() : (const void*)MData.MeshletVertices.data();
            const uint64 VertStride = bSkinned ? sizeof(FMeshletSkinnedVertex) : sizeof(FMeshletVertex);
            const uint64 VertCount  = bSkinned ? MData.MeshletSkinnedVertices.size() : MData.MeshletVertices.size();
            const uint64 VertsSize  = VertCount * VertStride;
            FRHIBufferDesc VertsDesc;
            VertsDesc.Size       = VertsSize;
            VertsDesc.Stride     = VertStride;
            VertsDesc.Usage.SetFlag(BUF_StorageBuffer);
            VertsDesc.DebugName  = GetName().ToString() + " Meshlet Vertices";
            MeshResources->MeshBuffers.MeshletVertexBuffer = GRenderContext->CreateBuffer(VertsDesc);

            CommandList->BeginTrackingBufferState(MeshResources->MeshBuffers.MeshletVertexBuffer, EResourceStates::CopyDest);
            CommandList->WriteBuffer(MeshResources->MeshBuffers.MeshletVertexBuffer, VertSrc, VertsSize);
            CommandList->SetPermanentBufferState(MeshResources->MeshBuffers.MeshletVertexBuffer, EResourceStates::ShaderResource);

            const uint64 TrisSize = sizeof(uint32) * MData.MeshletTriangles.size();
            FRHIBufferDesc TrisDesc;
            TrisDesc.Size       = TrisSize;
            TrisDesc.Stride     = sizeof(uint32);
            TrisDesc.Usage.SetFlag(BUF_StorageBuffer);
            TrisDesc.DebugName  = GetName().ToString() + " Meshlet Triangles";
            MeshResources->MeshBuffers.MeshletTriangleBuffer = GRenderContext->CreateBuffer(TrisDesc);

            CommandList->BeginTrackingBufferState(MeshResources->MeshBuffers.MeshletTriangleBuffer, EResourceStates::CopyDest);
            CommandList->WriteBuffer(MeshResources->MeshBuffers.MeshletTriangleBuffer, MData.MeshletTriangles.data(), TrisSize);
            CommandList->SetPermanentBufferState(MeshResources->MeshBuffers.MeshletTriangleBuffer, EResourceStates::ShaderResource);

            FMeshletHeaderGPU Header;
            Header.MeshletsAddress    = MeshResources->MeshBuffers.MeshletBuffer->GetAddress();
            Header.BoundsAddress      = MeshResources->MeshBuffers.MeshletBoundsBuffer->GetAddress();
            Header.VerticesAddress    = MeshResources->MeshBuffers.MeshletVertexBuffer->GetAddress();
            Header.TrianglesAddress   = MeshResources->MeshBuffers.MeshletTriangleBuffer->GetAddress();
            Header.MeshOriginAndPad   = glm::vec4(MData.MeshOrigin,   0.0f);
            Header.MeshGridStepAndPad = glm::vec4(MData.MeshGridStep, 0.0f);

            FRHIBufferDesc HeaderDesc;
            HeaderDesc.Size       = sizeof(FMeshletHeaderGPU);
            HeaderDesc.Stride     = sizeof(FMeshletHeaderGPU);
            HeaderDesc.Usage.SetFlag(BUF_StorageBuffer);
            HeaderDesc.DebugName  = GetName().ToString() + " Meshlet Header";
            MeshResources->MeshBuffers.MeshletHeaderBuffer = GRenderContext->CreateBuffer(HeaderDesc);

            CommandList->BeginTrackingBufferState(MeshResources->MeshBuffers.MeshletHeaderBuffer, EResourceStates::CopyDest);
            CommandList->WriteBuffer(MeshResources->MeshBuffers.MeshletHeaderBuffer, &Header, sizeof(FMeshletHeaderGPU));
            CommandList->SetPermanentBufferState(MeshResources->MeshBuffers.MeshletHeaderBuffer, EResourceStates::ShaderResource);
        }

        CommandList->Close();
        GRenderContext->ExecuteCommandList(CommandList, ECommandQueue::Graphics);

        // Drop import-time scratch.
        MeshResources->ClearVertices();
        MeshResources->Indices.clear();
        MeshResources->Indices.shrink_to_fit();
    }
}
