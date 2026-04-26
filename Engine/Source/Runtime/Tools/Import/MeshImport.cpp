#include "PCH.h"
#include "ImportHelpers.h"
#include "Assets/AssetTypes/Mesh/Animation/Animation.h"
#include "Core/Templates/AsBytes.h"
#include "Renderer/MeshData.h"
#include "Renderer/Vertex.h"
#include "TaskSystem/TaskSystem.h"

namespace Lumina::Import::Mesh
{
    void OptimizeNewlyImportedMesh(FMeshResource& MeshResource)
    {
        size_t NumVertices = MeshResource.GetNumVertices();
        size_t NumIndices  = MeshResource.Indices.size();
        const size_t VertexSize = MeshResource.GetVertexTypeSize();

        if (NumVertices == 0 || NumIndices == 0)
        {
            return;
        }

        // Vertex dedup + index remap.
        {
            TVector<uint32> Remap(NumVertices);
            const size_t UniqueVerts = meshopt_generateVertexRemap(
                Remap.data(),
                MeshResource.Indices.data(), NumIndices,
                MeshResource.GetVertexData(), NumVertices, VertexSize);

            if (UniqueVerts < NumVertices)
            {
                meshopt_remapIndexBuffer(
                    MeshResource.Indices.data(),
                    MeshResource.Indices.data(), NumIndices,
                    Remap.data());

                meshopt_remapVertexBuffer(
                    MeshResource.GetVertexData(),
                    MeshResource.GetVertexData(), NumVertices, VertexSize,
                    Remap.data());

                eastl::visit([&](auto& Vector) { Vector.resize(UniqueVerts); }, MeshResource.Vertices);
                NumVertices = UniqueVerts;
            }
        }

        // Per-surface vertex-cache + overdraw reorder. Surfaces own disjoint
        // index slices, so the in-place reorder is thread-safe.
        const uint32 NumSurfaces = (uint32)MeshResource.GeometrySurfaces.size();
        if (NumSurfaces > 0)
        {
            const float* VertexPositions = static_cast<const float*>(MeshResource.GetVertexData());

            Task::ParallelFor(NumSurfaces, [&](uint32 SurfaceIdx)
            {
                FGeometrySurface& Section = MeshResource.GeometrySurfaces[SurfaceIdx];
                if (Section.IndexCount == 0)
                {
                    return;
                }

                meshopt_optimizeVertexCache(
                    &MeshResource.Indices[Section.StartIndex],
                    &MeshResource.Indices[Section.StartIndex],
                    Section.IndexCount, NumVertices);

                // 5% ACMR slack for overdraw wins.
                constexpr float Threshold = 1.05f;
                meshopt_optimizeOverdraw(
                    &MeshResource.Indices[Section.StartIndex],
                    &MeshResource.Indices[Section.StartIndex],
                    Section.IndexCount,
                    VertexPositions,
                    NumVertices, VertexSize, Threshold);
            });
        }

        // Vertex-fetch reorder must be last (depends on final index order).
        meshopt_optimizeVertexFetch(
            MeshResource.GetVertexData(),
            MeshResource.Indices.data(), NumIndices,
            MeshResource.GetVertexData(),
            NumVertices, VertexSize);
    }

    void GenerateMeshlets(FMeshResource& MeshResource)
    {
        MeshResource.MeshletData.Clear();

        const size_t NumVertices = MeshResource.GetNumVertices();
        const size_t NumIndices  = MeshResource.Indices.size();
        const size_t VertexSize  = MeshResource.GetVertexTypeSize();

        if (NumVertices == 0 || NumIndices == 0)
        {
            for (FGeometrySurface& Section : MeshResource.GeometrySurfaces)
            {
                Section.MeshletOffset = 0;
                Section.MeshletCount  = 0;
            }
            return;
        }

        // FVertex / FSkinnedVertex both place Position at offset 0
        // (static_assert in Vertex.h), so the vertex pointer doubles as
        // meshopt's position pointer.
        const float* VertexPositions = static_cast<const float*>(MeshResource.GetVertexData());

        // Typed position fetch by global vertex index.
        auto ReadPosition = [&](uint32 GlobalIdx) -> glm::vec3
        {
            return eastl::visit([&](const auto& Vec) -> glm::vec3
            {
                return Vec[GlobalIdx].Position;
            }, MeshResource.Vertices);
        };

        constexpr size_t MaxVertices  = MESHLET_MAX_VERTICES;
        constexpr size_t MaxTriangles = MESHLET_MAX_TRIANGLES;
        // meshopt's recommended cone_weight; trades cluster uniformity for
        // tighter backface cones.
        constexpr float  ConeWeight   = 0.25f;

        const uint32 NumSurfaces = (uint32)MeshResource.GeometrySurfaces.size();

        // Phase 1: per-surface meshlet build in parallel. Per-meshlet AABB
        // is computed here (vertices are already hot in cache from the
        // bounds call) so phase 2 can derive LoInt closed-form.
        struct FSurfaceMeshletResult
        {
            TVector<uint32>          Vertices;       // local meshlet vertex indices
            TVector<uint8>           Triangles;      // local micro-indices
            TVector<FMeshlet>        OutMeshlets;    // local offsets
            TVector<FMeshletBounds>  Bounds;
            TVector<glm::vec3>       MeshletLo;      // per-meshlet world-space min P
            glm::vec3                MaxExtent = glm::vec3(0.0f);
        };

        TVector<FSurfaceMeshletResult> Results(NumSurfaces);

        Task::ParallelFor(NumSurfaces, [&](uint32 SurfaceIdx)
        {
            const FGeometrySurface& Section = MeshResource.GeometrySurfaces[SurfaceIdx];
            FSurfaceMeshletResult&  Result  = Results[SurfaceIdx];

            if (Section.IndexCount == 0)
            {
                return;
            }

            const uint32* SurfaceIndices = &MeshResource.Indices[Section.StartIndex];
            const size_t  MaxMeshlets    = meshopt_buildMeshletsBound(Section.IndexCount, MaxVertices, MaxTriangles);

            TVector<meshopt_Meshlet> LocalMeshlets(MaxMeshlets);
            Result.Vertices.resize(MaxMeshlets * MaxVertices);
            Result.Triangles.resize(MaxMeshlets * MaxTriangles * 3);

            const size_t MeshletCount = meshopt_buildMeshlets(
                LocalMeshlets.data(),
                Result.Vertices.data(),
                Result.Triangles.data(),
                SurfaceIndices, Section.IndexCount,
                VertexPositions, NumVertices, VertexSize,
                MaxVertices, MaxTriangles, ConeWeight);

            if (MeshletCount == 0)
            {
                Result.Vertices.clear();
                Result.Triangles.clear();
                return;
            }

            LocalMeshlets.resize(MeshletCount);

            // Trim flat arrays; meshopt pads triangles to a multiple of 4.
            const meshopt_Meshlet& Last = LocalMeshlets.back();
            Result.Vertices.resize(Last.vertex_offset + Last.vertex_count);
            Result.Triangles.resize(Last.triangle_offset + ((Last.triangle_count * 3 + 3) & ~3u));

            // In-meshlet reorder for rasterizer locality.
            for (const meshopt_Meshlet& M : LocalMeshlets)
            {
                meshopt_optimizeMeshlet(
                    &Result.Vertices[M.vertex_offset],
                    &Result.Triangles[M.triangle_offset],
                    M.triangle_count, M.vertex_count);
            }

            Result.OutMeshlets.reserve(MeshletCount);
            Result.Bounds.reserve(MeshletCount);
            Result.MeshletLo.reserve(MeshletCount);

            for (const meshopt_Meshlet& M : LocalMeshlets)
            {
                FMeshlet Out{};
                Out.VertexOffset   = M.vertex_offset;
                Out.TriangleOffset = M.triangle_offset;
                Out.VertexCount    = M.vertex_count;
                Out.TriangleCount  = M.triangle_count;
                Result.OutMeshlets.push_back(Out);

                const meshopt_Bounds B = meshopt_computeMeshletBounds(
                    &Result.Vertices[M.vertex_offset],
                    &Result.Triangles[M.triangle_offset],
                    M.triangle_count,
                    VertexPositions, NumVertices, VertexSize);

                FMeshletBounds Bounds{};
                Bounds.Center     = glm::vec3(B.center[0],    B.center[1],    B.center[2]);
                Bounds.Radius     = B.radius;
                Bounds.ConeApex   = glm::vec3(B.cone_apex[0], B.cone_apex[1], B.cone_apex[2]);
                Bounds.ConeAxis   = glm::vec3(B.cone_axis[0], B.cone_axis[1], B.cone_axis[2]);
                Bounds.ConeCutoff = B.cone_cutoff;
                Result.Bounds.push_back(Bounds);

                glm::vec3 Lo( FLT_MAX);
                glm::vec3 Hi(-FLT_MAX);
                for (uint32 i = 0; i < M.vertex_count; ++i)
                {
                    const glm::vec3 P = ReadPosition(Result.Vertices[M.vertex_offset + i]);
                    Lo = glm::min(Lo, P);
                    Hi = glm::max(Hi, P);
                }
                Result.MeshletLo.push_back(Lo);
                Result.MaxExtent = glm::max(Result.MaxExtent, Hi - Lo);
            }
        });

        // Mesh AABB + per-axis max meshlet extent (reduce across surfaces).
        glm::vec3 MeshLo( FLT_MAX);
        glm::vec3 MeshHi(-FLT_MAX);
        eastl::visit([&](const auto& Vec)
        {
            for (const auto& V : Vec)
            {
                MeshLo = glm::min(MeshLo, V.Position);
                MeshHi = glm::max(MeshHi, V.Position);
            }
        }, MeshResource.Vertices);

        glm::vec3 MaxMeshletExtent(0.0f);
        for (const FSurfaceMeshletResult& R : Results)
        {
            MaxMeshletExtent = glm::max(MaxMeshletExtent, R.MaxExtent);
        }

        // GridStep / 1022 (not 1023): round() can introduce a +1 fudge in
        // the integer diff, so 1022 keeps q in [0, 1023].
        glm::vec3 GridStep(0.0f);
        glm::vec3 InvGridStep(0.0f);
        if (MaxMeshletExtent.x > 0.0f) { GridStep.x = MaxMeshletExtent.x / 1022.0f; InvGridStep.x = 1.0f / GridStep.x; }
        if (MaxMeshletExtent.y > 0.0f) { GridStep.y = MaxMeshletExtent.y / 1022.0f; InvGridStep.y = 1.0f / GridStep.y; }
        if (MaxMeshletExtent.z > 0.0f) { GridStep.z = MaxMeshletExtent.z / 1022.0f; InvGridStep.z = 1.0f / GridStep.z; }

        MeshResource.MeshletData.MeshOrigin   = MeshLo;
        MeshResource.MeshletData.MeshGridStep = GridStep;

        auto GridIndex = [&](glm::vec3 P) -> glm::ivec3
        {
            return glm::ivec3(glm::round((P - MeshLo) * InvGridStep));
        };

        size_t TotalMeshlets  = 0;
        size_t TotalVertices  = 0;
        size_t TotalTriangles = 0;
        for (const FSurfaceMeshletResult& R : Results)
        {
            TotalMeshlets  += R.OutMeshlets.size();
            TotalVertices  += R.Vertices.size();
            for (const FMeshlet& M : R.OutMeshlets)
            {
                TotalTriangles += M.TriangleCount;
            }
        }

        MeshResource.MeshletData.Meshlets.reserve(TotalMeshlets);
        MeshResource.MeshletData.MeshletBounds.reserve(TotalMeshlets);
        MeshResource.MeshletData.MeshletTriangles.reserve(TotalTriangles);
        if (MeshResource.bSkinnedMesh)
        {
            MeshResource.MeshletData.MeshletSkinnedVertices.reserve(TotalVertices);
        }
        else
        {
            MeshResource.MeshletData.MeshletVertices.reserve(TotalVertices);
        }

        for (uint32 SurfaceIdx = 0; SurfaceIdx < NumSurfaces; ++SurfaceIdx)
        {
            FGeometrySurface&       Section = MeshResource.GeometrySurfaces[SurfaceIdx];
            FSurfaceMeshletResult&  Result  = Results[SurfaceIdx];

            Section.MeshletOffset = (uint32)MeshResource.MeshletData.Meshlets.size();
            Section.MeshletCount  = (uint32)Result.OutMeshlets.size();

            if (Result.OutMeshlets.empty())
            {
                continue;
            }

            for (size_t MeshletIdx = 0; MeshletIdx < Result.OutMeshlets.size(); ++MeshletIdx)
            {
                FMeshlet Out = Result.OutMeshlets[MeshletIdx];

                // round() of the meshlet's min P equals the per-vertex Q
                // floor across the meshlet (round is monotonic).
                Out.LoInt = GridIndex(Result.MeshletLo[MeshletIdx]);

                const uint32 PackedVertexStart = MeshResource.bSkinnedMesh
                    ? (uint32)MeshResource.MeshletData.MeshletSkinnedVertices.size()
                    : (uint32)MeshResource.MeshletData.MeshletVertices.size();

                if (MeshResource.bSkinnedMesh)
                {
                    const auto& Verts = eastl::get<TVector<FSkinnedVertex>>(MeshResource.Vertices);
                    for (uint32 i = 0; i < Out.VertexCount; ++i)
                    {
                        const FSkinnedVertex& V = Verts[Result.Vertices[Out.VertexOffset + i]];

                        FMeshletSkinnedVertex Packed;
                        Packed.Position = PackMeshletPosition(GridIndex(V.Position) - Out.LoInt);
                        Packed.Normal   = V.Normal;
                        Packed.UV       = V.UV;
                        Packed.Color    = V.Color;
                        // glm::u8vec4 is x..w in bytes 0..3; on little-endian
                        // its in-memory layout is already the packed uint32.
                        memcpy(&Packed.JointIndices, &V.JointIndices, sizeof(uint32));
                        memcpy(&Packed.JointWeights, &V.JointWeights, sizeof(uint32));
                        MeshResource.MeshletData.MeshletSkinnedVertices.push_back(Packed);
                    }
                }
                else
                {
                    const auto& Verts = eastl::get<TVector<FVertex>>(MeshResource.Vertices);
                    for (uint32 i = 0; i < Out.VertexCount; ++i)
                    {
                        const FVertex& V = Verts[Result.Vertices[Out.VertexOffset + i]];

                        FMeshletVertex Packed;
                        Packed.Position = PackMeshletPosition(GridIndex(V.Position) - Out.LoInt);
                        Packed.Normal   = V.Normal;
                        Packed.UV       = V.UV;
                        Packed.Color    = V.Color;
                        MeshResource.MeshletData.MeshletVertices.push_back(Packed);
                    }
                }

                // Pack triangle micro-indices, 3 per dword.
                const uint32 PackedDwordStart = (uint32)MeshResource.MeshletData.MeshletTriangles.size();
                const uint8* TriSrc           = Result.Triangles.data() + Out.TriangleOffset;
                for (uint32 t = 0; t < Out.TriangleCount; ++t)
                {
                    const uint32 Packed =
                          (uint32)TriSrc[t * 3 + 0]
                        | ((uint32)TriSrc[t * 3 + 1] << 8)
                        | ((uint32)TriSrc[t * 3 + 2] << 16);
                    MeshResource.MeshletData.MeshletTriangles.push_back(Packed);
                }

                Out.VertexOffset   = PackedVertexStart;
                Out.TriangleOffset = PackedDwordStart;
                MeshResource.MeshletData.Meshlets.push_back(Out);
                MeshResource.MeshletData.MeshletBounds.push_back(Result.Bounds[MeshletIdx]);
            }
        }

        // Avoid a zero-byte SSBO upload.
        if (MeshResource.MeshletData.MeshletTriangles.empty())
        {
            MeshResource.MeshletData.MeshletTriangles.push_back(0u);
        }
    }

    void AnalyzeMeshStatistics(FMeshResource& MeshResource, FMeshStatistics& OutMeshStats)
    {
        OutMeshStats.VertexFetchStatics.emplace_back(meshopt_analyzeVertexFetch(MeshResource.Indices.data(), MeshResource.Indices.size(), MeshResource.GetNumVertices(), MeshResource.GetVertexTypeSize()));
        OutMeshStats.OverdrawStatics.emplace_back(meshopt_analyzeOverdraw(MeshResource.Indices.data(), MeshResource.Indices.size(), static_cast<float*>(MeshResource.GetVertexData()), MeshResource.GetNumVertices(), MeshResource.GetVertexTypeSize()));
    }

    namespace
    {
        // Concatenate Src into Dst with index/material rebase. Src.ImportTransform
        // is baked into positions/normals so merged geometry keeps its authored
        // world placement.
        void MergeResourceInto(FMeshResource& Src, FMeshResource& Dst, THashMap<int16, int16>& MaterialRemap)
        {
            const uint32 BaseVert = (uint32)Dst.GetNumVertices();
            const uint32 BaseIdx  = (uint32)Dst.Indices.size();

            const glm::mat4 PosMatrix    = Src.ImportTransform;
            const glm::mat3 NormalMatrix = glm::transpose(glm::inverse(glm::mat3(PosMatrix)));
            const bool bIdentity         = PosMatrix == glm::mat4(1.0f);

            auto TransformVertex = [&](FVertex& V)
            {
                V.Position = glm::vec3(PosMatrix * glm::vec4(V.Position, 1.0f));
                glm::vec3 N = glm::normalize(NormalMatrix * UnpackNormal(V.Normal));
                V.Normal = PackNormal(N);
            };

            if (Src.bSkinnedMesh)
            {
                auto& DstVec = eastl::get<TVector<FSkinnedVertex>>(Dst.Vertices);
                auto& SrcVec = eastl::get<TVector<FSkinnedVertex>>(Src.Vertices);
                const size_t Start = DstVec.size();
                DstVec.insert(DstVec.end(), SrcVec.begin(), SrcVec.end());
                if (!bIdentity)
                {
                    for (size_t i = Start; i < DstVec.size(); ++i)
                    {
                        TransformVertex(DstVec[i]);
                    }
                }
            }
            else
            {
                auto& DstVec = eastl::get<TVector<FVertex>>(Dst.Vertices);
                auto& SrcVec = eastl::get<TVector<FVertex>>(Src.Vertices);
                const size_t Start = DstVec.size();
                DstVec.insert(DstVec.end(), SrcVec.begin(), SrcVec.end());
                if (!bIdentity)
                {
                    for (size_t i = Start; i < DstVec.size(); ++i)
                    {
                        TransformVertex(DstVec[i]);
                    }
                }
            }

            Dst.Indices.reserve(Dst.Indices.size() + Src.Indices.size());
            for (uint32 Idx : Src.Indices)
            {
                Dst.Indices.push_back(Idx + BaseVert);
            }

            Dst.GeometrySurfaces.reserve(Dst.GeometrySurfaces.size() + Src.GeometrySurfaces.size());
            for (FGeometrySurface S : Src.GeometrySurfaces)
            {
                S.StartIndex += BaseIdx;
                if (S.MaterialIndex >= 0)
                {
                    auto It = MaterialRemap.find(S.MaterialIndex);
                    if (It == MaterialRemap.end())
                    {
                        const int16 NewSlot = (int16)MaterialRemap.size();
                        MaterialRemap.emplace(S.MaterialIndex, NewSlot);
                        S.MaterialIndex = NewSlot;
                    }
                    else
                    {
                        S.MaterialIndex = It->second;
                    }
                }
                Dst.GeometrySurfaces.push_back(S);
            }
        }
    }

    void FinalizeMeshImportData(FMeshImportData& Data, const FMeshImportOptions& Options)
    {
        const float Scale          = Options.Scale;
        const bool  bScaleEnabled  = (Scale != 1.0f);
        const bool  bFlipUVs       = Options.bFlipUVs;
        const bool  bFlipNormals   = Options.bFlipNormals;

        // Per-mesh transforms (each resource owns its own vertex buffer).
        if (bScaleEnabled || bFlipUVs || bFlipNormals)
        {
            Task::ParallelFor((uint32)Data.Resources.size(), [&](uint32 ResIdx)
            {
                TUniquePtr<FMeshResource>& MeshPtr = Data.Resources[ResIdx];
                if (!MeshPtr)
                {
                    return;
                }

                FMeshResource& M = *MeshPtr;
                const size_t NumVerts = M.GetNumVertices();

                for (size_t i = 0; i < NumVerts; ++i)
                {
                    if (bScaleEnabled)
                    {
                        M.SetPositionAt(i, M.GetPositionAt(i) * Scale);
                    }
                    if (bFlipUVs)
                    {
                        glm::vec2 UV = M.GetUVAt(i);
                        UV.y = 1.0f - UV.y;
                        M.SetUVAt(i, UV);
                    }
                    if (bFlipNormals)
                    {
                        glm::vec3 N = UnpackNormal(M.GetNormalAt(i));
                        M.SetNormalAt(i, PackNormal(-N));
                    }
                }
            });
        }

        // Skeleton bind/local translations scale like positions; rotation
        // is untouched because uniform scale commutes with R^T.
        if (bScaleEnabled)
        {
            for (TUniquePtr<FSkeletonResource>& SkelPtr : Data.Skeletons)
            {
                if (!SkelPtr)
                {
                    continue;
                }
                for (FSkeletonResource::FBoneInfo& B : SkelPtr->Bones)
                {
                    B.LocalTransform[3][0] *= Scale;
                    B.LocalTransform[3][1] *= Scale;
                    B.LocalTransform[3][2] *= Scale;
                    B.InvBindMatrix[3][0]  *= Scale;
                    B.InvBindMatrix[3][1]  *= Scale;
                    B.InvBindMatrix[3][2]  *= Scale;
                }
            }
        }

        // Animation translation channels (rotation/scale are unitless).
        if (bScaleEnabled)
        {
            for (TUniquePtr<FAnimationResource>& AnimPtr : Data.Animations)
            {
                if (!AnimPtr)
                {
                    continue;
                }
                for (FAnimationChannel& Ch : AnimPtr->Channels)
                {
                    if (Ch.TargetPath == FAnimationChannel::ETargetPath::Translation)
                    {
                        for (glm::vec3& T : Ch.Translations)
                        {
                            T *= Scale;
                        }
                    }
                }
            }
        }

        // Merge into static + skinned pair before optimize/meshlets so the
        // heavy passes run on the merged geometry.
        if (Options.bMergeMeshes && Data.Resources.size() > 1)
        {
            TUniquePtr<FMeshResource> MergedStatic = MakeUnique<FMeshResource>();
            MergedStatic->Vertices = TVector<FVertex>();

            TUniquePtr<FMeshResource> MergedSkinned = MakeUnique<FMeshResource>();
            MergedSkinned->Vertices = TVector<FSkinnedVertex>();
            MergedSkinned->bSkinnedMesh = true;

            // Inherit name from first matching source.
            for (const TUniquePtr<FMeshResource>& Res : Data.Resources)
            {
                if (!Res) continue;
                if (!Res->bSkinnedMesh && MergedStatic->Name.IsNone())
                {
                    MergedStatic->Name = Res->Name;
                }
                if (Res->bSkinnedMesh && MergedSkinned->Name.IsNone())
                {
                    MergedSkinned->Name = Res->Name;
                }
            }

            THashMap<int16, int16> StaticMatRemap;
            THashMap<int16, int16> SkinnedMatRemap;

            for (TUniquePtr<FMeshResource>& Res : Data.Resources)
            {
                if (!Res)
                {
                    continue;
                }
                if (Res->bSkinnedMesh)
                {
                    MergeResourceInto(*Res, *MergedSkinned, SkinnedMatRemap);
                }
                else
                {
                    MergeResourceInto(*Res, *MergedStatic, StaticMatRemap);
                }
            }

            TVector<TUniquePtr<FMeshResource>> NewResources;
            if (MergedStatic->GetNumVertices() > 0)
            {
                NewResources.push_back(eastl::move(MergedStatic));
            }
            if (MergedSkinned->GetNumVertices() > 0)
            {
                NewResources.push_back(eastl::move(MergedSkinned));
            }
            Data.Resources = eastl::move(NewResources);
        }

        // Heavy CPU finalize. Stats run serially afterwards so the parallel
        // pass writes only resource-local data.
        Data.MeshStatistics.OverdrawStatics.clear();
        Data.MeshStatistics.VertexFetchStatics.clear();

        Task::ParallelFor((uint32)Data.Resources.size(), [&](uint32 ResIdx)
        {
            TUniquePtr<FMeshResource>& MeshPtr = Data.Resources[ResIdx];
            if (!MeshPtr)
            {
                return;
            }
            FMeshResource& M = *MeshPtr;
            if (Options.bOptimize)
            {
                OptimizeNewlyImportedMesh(M);
            }
            GenerateMeshlets(M);
        });

        for (TUniquePtr<FMeshResource>& MeshPtr : Data.Resources)
        {
            if (!MeshPtr)
            {
                continue;
            }
            AnalyzeMeshStatistics(*MeshPtr, Data.MeshStatistics);
        }
    }
}
