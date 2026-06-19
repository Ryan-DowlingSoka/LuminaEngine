#include "pch.h"
#include "DynamicMeshComponent.h"

#include "Assets/AssetTypes/Material/MaterialInterface.h"
#include "Core/Object/ObjectCore.h"
#include "Core/Object/Package/Package.h"
#include "Renderer/MeshData.h"
#include "Renderer/Vertex.h"
#include "Tools/Import/ImportHelpers.h"

namespace Lumina
{
    // One material-tagged slice of the index buffer.
    struct FDynamicMeshSection
    {
        int32 MaterialSlot = 0;
        int32 StartIndex   = 0;
        int32 IndexCount   = 0;
    };

    // CPU-side staging filled by the setters; consumed and cleared by Commit().
    struct FDynamicMeshBuildData
    {
        TVector<FVector3>          Positions;
        TVector<FVector3>          Normals;   // unpacked; octahedral-packed at Commit
        TVector<FVector2>          UVs;
        TVector<uint32>             Colors;    // RGBA8 packed
        TVector<uint32>             Indices;
        TVector<FDynamicMeshSection> Sections;
    };

    namespace
    {
        // Area-weighted smooth normals, used when the caller didn't supply any (common for procedural/voxel
        // geometry where the engine can derive them).
        void ComputeSmoothNormals(const TVector<FVector3>& Positions, const TVector<uint32>& Indices, TVector<FVector3>& OutNormals)
        {
            OutNormals.assign(Positions.size(), FVector3(0.0f));

            for (size_t i = 0; i + 2 < Indices.size(); i += 3)
            {
                const uint32 I0 = Indices[i];
                const uint32 I1 = Indices[i + 1];
                const uint32 I2 = Indices[i + 2];
                if (I0 >= Positions.size() || I1 >= Positions.size() || I2 >= Positions.size())
                {
                    continue;
                }

                const FVector3 Edge1 = Positions[I1] - Positions[I0];
                const FVector3 Edge2 = Positions[I2] - Positions[I0];
                const FVector3 FaceNormal = Math::Cross(Edge1, Edge2); // length encodes 2x triangle area

                OutNormals[I0] += FaceNormal;
                OutNormals[I1] += FaceNormal;
                OutNormals[I2] += FaceNormal;
            }

            for (FVector3& N : OutNormals)
            {
                const float Len = Math::Length(N);
                N = (Len > 1e-8f) ? (N / Len) : FVector3(0.0f, 1.0f, 0.0f);
            }
        }
    }

    FDynamicMeshBuildData& SDynamicMeshComponent::EnsureBuildData()
    {
        if (!BuildData)
        {
            BuildData = MakeShared<FDynamicMeshBuildData>();
        }
        return *BuildData;
    }

    CMaterialInterface* SDynamicMeshComponent::GetMaterialForSlot(size_t Slot) const
    {
        if (Slot < MaterialOverrides.size())
        {
            if (CMaterialInterface* Interface = MaterialOverrides[Slot])
            {
                return Interface;
            }
        }

        if (DynamicMesh.IsValid())
        {
            return DynamicMesh->GetMaterialAtSlot(Slot);
        }

        return nullptr;
    }

    FAABB SDynamicMeshComponent::GetAABB() const
    {
        return DynamicMesh.IsValid() ? DynamicMesh->GetAABB() : FAABB();
    }

    void SDynamicMeshComponent::AddSection(int32 MaterialSlot, int32 StartIndex, int32 IndexCount)
    {
        FDynamicMeshSection& Section = EnsureBuildData().Sections.emplace_back();
        Section.MaterialSlot = Math::Max(0, MaterialSlot);
        Section.StartIndex   = Math::Max(0, StartIndex);
        Section.IndexCount   = Math::Max(0, IndexCount);
    }

    void SDynamicMeshComponent::ClearMesh()
    {
        BuildData.reset();
        DynamicMesh = nullptr;
        CommittedVertexCount   = 0;
        CommittedTriangleCount = 0;
    }

    bool SDynamicMeshComponent::IsBuilt() const
    {
        return DynamicMesh.IsValid();
    }

    int32 SDynamicMeshComponent::GetVertexCount() const
    {
        return BuildData ? (int32)BuildData->Positions.size() : CommittedVertexCount;
    }

    int32 SDynamicMeshComponent::GetTriangleCount() const
    {
        return BuildData ? (int32)(BuildData->Indices.size() / 3) : CommittedTriangleCount;
    }

    void SDynamicMeshComponent::SetPositionsData(const float* Data, int32 FloatCount)
    {
        FDynamicMeshBuildData& BD = EnsureBuildData();
        const int32 Count = FloatCount / 3;
        BD.Positions.resize((size_t)Count);
        for (int32 i = 0; i < Count; ++i)
        {
            BD.Positions[i] = FVector3(Data[i * 3 + 0], Data[i * 3 + 1], Data[i * 3 + 2]);
        }
    }

    void SDynamicMeshComponent::SetNormalsData(const float* Data, int32 FloatCount)
    {
        FDynamicMeshBuildData& BD = EnsureBuildData();
        const int32 Count = FloatCount / 3;
        BD.Normals.resize((size_t)Count);
        for (int32 i = 0; i < Count; ++i)
        {
            BD.Normals[i] = FVector3(Data[i * 3 + 0], Data[i * 3 + 1], Data[i * 3 + 2]);
        }
    }

    void SDynamicMeshComponent::SetUVsData(const float* Data, int32 FloatCount)
    {
        FDynamicMeshBuildData& BD = EnsureBuildData();
        const int32 Count = FloatCount / 2;
        BD.UVs.resize((size_t)Count);
        for (int32 i = 0; i < Count; ++i)
        {
            BD.UVs[i] = FVector2(Data[i * 2 + 0], Data[i * 2 + 1]);
        }
    }

    void SDynamicMeshComponent::SetColorsFloatData(const float* Data, int32 FloatCount)
    {
        FDynamicMeshBuildData& BD = EnsureBuildData();
        const int32 Count = FloatCount / 4;
        BD.Colors.resize((size_t)Count);
        for (int32 i = 0; i < Count; ++i)
        {
            BD.Colors[i] = PackColor(FVector4(Data[i * 4 + 0], Data[i * 4 + 1], Data[i * 4 + 2], Data[i * 4 + 3]));
        }
    }

    void SDynamicMeshComponent::SetColorsPackedData(const uint32* Data, int32 Count)
    {
        FDynamicMeshBuildData& BD = EnsureBuildData();
        BD.Colors.assign(Data, Data + Math::Max(0, Count));
    }

    void SDynamicMeshComponent::SetIndicesData(const uint32* Data, int32 Count)
    {
        FDynamicMeshBuildData& BD = EnsureBuildData();
        BD.Indices.assign(Data, Data + Math::Max(0, Count));
    }

    bool SDynamicMeshComponent::Commit()
    {
        if (!BuildData || BuildData->Positions.empty() || BuildData->Indices.empty())
        {
            return false;
        }

        FDynamicMeshBuildData& BD = *BuildData;
        const size_t VertexCount = BD.Positions.size();

        TUniquePtr<FMeshResource> Resource = MakeUnique<FMeshResource>();
        Resource->bSkinnedMesh = false;

        // Positions.
        Resource->Positions = BD.Positions;

        // Normals (derive when absent), packed octahedral.
        const TVector<FVector3>* SourceNormals = &BD.Normals;
        TVector<FVector3> DerivedNormals;
        if (BD.Normals.size() != VertexCount)
        {
            ComputeSmoothNormals(BD.Positions, BD.Indices, DerivedNormals);
            SourceNormals = &DerivedNormals;
        }
        Resource->Normals.resize(VertexCount);
        for (size_t i = 0; i < VertexCount; ++i)
        {
            Resource->Normals[i] = PackNormal((*SourceNormals)[i]);
        }

        // Tangents are generated by MikkTSpace inside GenerateMeshlets; start them zeroed.
        Resource->Tangents.assign(VertexCount, 0u);

        // UVs (zeroed when absent), packed half2.
        Resource->UVs.resize(VertexCount);
        const bool bHasUVs = BD.UVs.size() == VertexCount;
        for (size_t i = 0; i < VertexCount; ++i)
        {
            Resource->UVs[i] = Math::PackHalf2x16(bHasUVs ? BD.UVs[i] : FVector2(0.0f));
        }

        // Colors (white when absent).
        Resource->Colors.resize(VertexCount);
        const bool bHasColors = BD.Colors.size() == VertexCount;
        for (size_t i = 0; i < VertexCount; ++i)
        {
            Resource->Colors[i] = bHasColors ? BD.Colors[i] : 0xFFFFFFFFu;
        }

        Resource->Indices = BD.Indices;

        // Sections -> geometry surfaces. With none declared, one section covers the whole index buffer.
        int32 MaterialSlotCount = 1;
        if (BD.Sections.empty())
        {
            FGeometrySurface& Surface = Resource->GeometrySurfaces.emplace_back();
            Surface.ID            = "Section0";
            Surface.StartIndex    = 0;
            Surface.IndexCount    = (uint32)Resource->Indices.size();
            Surface.MaterialIndex = 0;
        }
        else
        {
            uint32 SurfaceIndex = 0;
            for (const FDynamicMeshSection& Section : BD.Sections)
            {
                FGeometrySurface& Surface = Resource->GeometrySurfaces.emplace_back();
                Surface.ID            = FName(eastl::string("Section") + eastl::to_string(SurfaceIndex++));
                Surface.StartIndex    = (uint32)Section.StartIndex;
                Surface.IndexCount    = (uint32)Section.IndexCount;
                Surface.MaterialIndex = (int16)Section.MaterialSlot;
                MaterialSlotCount     = Math::Max(MaterialSlotCount, Section.MaterialSlot + 1);
            }
        }

        if (!DynamicMesh.IsValid())
        {
            // NAME_None auto-generates a unique name; identity is GUID-based, so per-entity meshes never collide.
            DynamicMesh = NewObject<CStaticMesh>(CPackage::GetTransientPackage());
        }

        // Keep the material array sized to the slots the sections reference; the render path falls back to the
        // engine default material for any null slot, and per-component MaterialOverrides win regardless.
        if ((int32)DynamicMesh->Materials.size() < MaterialSlotCount)
        {
            DynamicMesh->Materials.resize((size_t)MaterialSlotCount);
        }

        CommittedVertexCount   = (int32)VertexCount;
        CommittedTriangleCount = (int32)(Resource->Indices.size() / 3);

        // SetMeshResource runs GenerateMeshlets (tangents + LODs), uploads the GPU buffers and rebuilds the
        // bounding box; it also drops the CPU scratch streams.
        DynamicMesh->SetMeshResource(eastl::move(Resource));

        // Staging is consumed; the next edit re-stages from scratch.
        BuildData.reset();
        return true;
    }
}
