#include "pch.h"
#include "Mesh.h"
#include "Assets/AssetTypes/Material/Material.h"
#include "assets/assettypes/material/materialinstance.h"
#include "Core/Object/Cast.h"
#include "Memory/MemoryTracking.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RHIGlobals.h"
#include "Renderer/Vertex.h"
#include "Tools/Import/ImportHelpers.h"


namespace Lumina
{
    void CMesh::Serialize(FArchive& Ar)
    {
        LUMINA_MEMORY_SCOPE("Meshes");

        Super::Serialize(Ar);

        if (!MeshResources)
        {
            MeshResources = MakeUnique<FMeshResource>();
        }

        Ar << *MeshResources;
    }

    void CMesh::PostLoad()
    {
        LUMINA_MEMORY_SCOPE("Meshes");

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
            for (const FVector3& P : MeshResources->Positions)
            {
                BoundingBox.Min = Math::Min(BoundingBox.Min, P);
                BoundingBox.Max = Math::Max(BoundingBox.Max, P);
            }
            return;
        }
        
        if (MeshResources && !MeshResources->MeshletData.IsEmpty())
        {
            const FMeshletData& MD = MeshResources->MeshletData;

            if (!MD.MeshletBounds.empty())
            {
                for (const FMeshletBounds& B : MD.MeshletBounds)
                {
                    BoundingBox.Min = Math::Min(BoundingBox.Min, B.Center - FVector3(B.Radius));
                    BoundingBox.Max = Math::Max(BoundingBox.Max, B.Center + FVector3(B.Radius));
                }
            }
            else
            {
                // Fallback (no bounds stored): conservative per-meshlet grid extent.
                for (const FMeshlet& M : MD.Meshlets)
                {
                    const FVector3 Origin   = MD.MeshOrigin[M.LODIndex];
                    const FVector3 GridStep = MD.MeshGridStep[M.LODIndex];
                    const FVector3 Lo = Origin + FVector3(M.LoInt) * GridStep;
                    BoundingBox.Min = Math::Min(BoundingBox.Min, Lo);
                    BoundingBox.Max = Math::Max(BoundingBox.Max, Lo + FVector3(1023.0f) * GridStep);
                }
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
            for (uint32 i = 0; i < MAX_MESH_LODS; ++i)
            {
                Header.MeshOrigin[i]   = FVector4(MData.MeshOrigin[i],   0.0f);
                Header.MeshGridStep[i] = FVector4(MData.MeshGridStep[i], 0.0f);
            }

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
