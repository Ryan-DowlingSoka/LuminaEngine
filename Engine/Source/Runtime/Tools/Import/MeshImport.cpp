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

        // 1) Vertex dedup + index remap. meshoptimizer expects this as step one:
        // identical-attribute vertices collapse, which directly reduces VPC /
        // vertex-shader invocations for every subsequent pass.
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

        // 2) Per-surface vertex-cache + overdraw reorder. Each surface owns a
        // disjoint slice [StartIndex, StartIndex+IndexCount) of the index
        // buffer and meshoptimizer's reorder is in-place on that slice, so
        // surfaces can run on different threads without aliasing. The vertex
        // buffer is read-only here; only indices are mutated.
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

                // 1.05 ACMR threshold: up to 5% worse cache ratio for overdraw wins.
                constexpr float Threshold = 1.05f;
                meshopt_optimizeOverdraw(
                    &MeshResource.Indices[Section.StartIndex],
                    &MeshResource.Indices[Section.StartIndex],
                    Section.IndexCount,
                    VertexPositions,
                    NumVertices, VertexSize, Threshold);
            });
        }

        // 3) Global vertex-fetch reorder. Must be last because it depends on the
        // final index order; reorders the vertex buffer so the GPU prefetcher
        // streams sequentially as the IA walks the index buffer.
        meshopt_optimizeVertexFetch(
            MeshResource.GetVertexData(),
            MeshResource.Indices.data(), NumIndices,
            MeshResource.GetVertexData(),
            NumVertices, VertexSize);
    }

    void GenerateShadowBuffers(FMeshResource& MeshResource)
    {
        const size_t NumIndices  = MeshResource.Indices.size();
        const size_t NumVertices = MeshResource.GetNumVertices();
        if (NumIndices == 0 || NumVertices == 0)
        {
            MeshResource.ShadowIndices.clear();
            return;
        }

        MeshResource.ShadowIndices.resize(NumIndices);

        // Position-only dedup: shadow passes write only depth so two vertices
        // with identical positions can share the same index even if they
        // differ in normal / UV / color / skinning. Hashing only the first
        // sizeof(vec3) bytes collapses the maximum number of vertices and
        // gives the shadow pass a smaller VPC working set.
        meshopt_generateShadowIndexBuffer(
            MeshResource.ShadowIndices.data(),
            MeshResource.Indices.data(), NumIndices,
            MeshResource.GetVertexData(),
            NumVertices,
            sizeof(glm::vec3),
            MeshResource.GetVertexTypeSize());

        // Per-surface cache optimization on the shadow indices. Surface slices
        // are disjoint so the in-place reorder parallelizes across threads.
        const uint32 NumSurfaces = (uint32)MeshResource.GeometrySurfaces.size();
        if (NumSurfaces > 0)
        {
            Task::ParallelFor(NumSurfaces, [&](uint32 SurfaceIdx)
            {
                const FGeometrySurface& Section = MeshResource.GeometrySurfaces[SurfaceIdx];
                if (Section.IndexCount == 0)
                {
                    return;
                }
                meshopt_optimizeVertexCache(
                    &MeshResource.ShadowIndices[Section.StartIndex],
                    &MeshResource.ShadowIndices[Section.StartIndex],
                    Section.IndexCount, NumVertices);
            });
        }
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

        // FVertex places Position in its first 12 bytes (static_assert guards
        // this in Vertex.h) so the vertex pointer doubles as the position
        // pointer meshoptimizer expects.
        const float* VertexPositions = static_cast<const float*>(MeshResource.GetVertexData());

        constexpr size_t MaxVertices  = MESHLET_MAX_VERTICES;
        constexpr size_t MaxTriangles = MESHLET_MAX_TRIANGLES;
        // 0.25 is the cone_weight used by meshoptimizer's own demo; trades a
        // small amount of cluster-size uniformity for better backface cone
        // culling efficiency. Pure size packing would use 0.
        constexpr float  ConeWeight   = 0.25f;

        const uint32 NumSurfaces = (uint32)MeshResource.GeometrySurfaces.size();

        // Phase 1: build per-surface meshlets in parallel. Each surface owns a
        // disjoint slice of MeshResource.Indices and writes only into its own
        // entry of Results, so there is no synchronization needed inside the
        // parallel loop. Bounds + per-meshlet optimize are batched here while
        // the surface's local arrays are still hot.
        struct FSurfaceMeshletResult
        {
            TVector<uint32>             Vertices;       // local meshlet vertex indices
            TVector<uint8>              Triangles;      // local micro-indices
            TVector<FMeshlet>           OutMeshlets;    // per-meshlet header (local offsets)
            TVector<FMeshletBounds>     Bounds;
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

            // Trim the flat arrays to exactly what buildMeshlets produced.
            // The triangle buffer is padded to a multiple of 4 per meshlet
            // (documented meshopt invariant) so we align the tail the same way.
            const meshopt_Meshlet& Last = LocalMeshlets.back();
            Result.Vertices.resize(Last.vertex_offset + Last.vertex_count);
            Result.Triangles.resize(Last.triangle_offset + ((Last.triangle_count * 3 + 3) & ~3u));

            // Reorder inside each meshlet for rasterizer locality.
            for (const meshopt_Meshlet& M : LocalMeshlets)
            {
                meshopt_optimizeMeshlet(
                    &Result.Vertices[M.vertex_offset],
                    &Result.Triangles[M.triangle_offset],
                    M.triangle_count, M.vertex_count);
            }

            Result.OutMeshlets.reserve(MeshletCount);
            Result.Bounds.reserve(MeshletCount);

            for (const meshopt_Meshlet& M : LocalMeshlets)
            {
                FMeshlet Out{};
                // Local offsets — phase 2 rebases them onto the global arrays.
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
            }
        });

        // Phase 2: serial merge. Order-preserving append into the mesh's flat
        // arrays, rebasing the per-surface local offsets onto the global ones.
        size_t TotalMeshlets  = 0;
        size_t TotalVertices  = 0;
        size_t TotalTriangles = 0;
        for (const FSurfaceMeshletResult& R : Results)
        {
            TotalMeshlets  += R.OutMeshlets.size();
            TotalVertices  += R.Vertices.size();
            TotalTriangles += R.Triangles.size();
        }

        MeshResource.MeshletData.Meshlets.reserve(TotalMeshlets);
        MeshResource.MeshletData.MeshletBounds.reserve(TotalMeshlets);
        MeshResource.MeshletData.MeshletVertices.reserve(TotalVertices);
        MeshResource.MeshletData.MeshletTriangles.reserve(TotalTriangles);

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

            const uint32 GlobalVertexBase   = (uint32)MeshResource.MeshletData.MeshletVertices.size();
            const uint32 GlobalTriangleBase = (uint32)MeshResource.MeshletData.MeshletTriangles.size();

            MeshResource.MeshletData.MeshletVertices.insert(
                MeshResource.MeshletData.MeshletVertices.end(),
                Result.Vertices.begin(), Result.Vertices.end());

            MeshResource.MeshletData.MeshletTriangles.insert(
                MeshResource.MeshletData.MeshletTriangles.end(),
                Result.Triangles.begin(), Result.Triangles.end());

            for (FMeshlet Out : Result.OutMeshlets)
            {
                Out.VertexOffset   += GlobalVertexBase;
                Out.TriangleOffset += GlobalTriangleBase;
                MeshResource.MeshletData.Meshlets.push_back(Out);
            }

            MeshResource.MeshletData.MeshletBounds.insert(
                MeshResource.MeshletData.MeshletBounds.end(),
                Result.Bounds.begin(), Result.Bounds.end());
        }
    }

    void AnalyzeMeshStatistics(FMeshResource& MeshResource, FMeshStatistics& OutMeshStats)
    {
        OutMeshStats.VertexFetchStatics.emplace_back(meshopt_analyzeVertexFetch(MeshResource.Indices.data(), MeshResource.Indices.size(), MeshResource.GetNumVertices(), MeshResource.GetVertexTypeSize()));
        OutMeshStats.OverdrawStatics.emplace_back(meshopt_analyzeOverdraw(MeshResource.Indices.data(), MeshResource.Indices.size(), static_cast<float*>(MeshResource.GetVertexData()), MeshResource.GetNumVertices(), MeshResource.GetVertexTypeSize()));
    }

    namespace
    {
        // Concatenate Src into Dst. Indices are rebased by Dst's current vertex
        // count, surface StartIndex by Dst's current index count. Material
        // indices are folded onto a unique-slot remap so primitives sharing
        // the same source material end up on the same slot in the merged asset.
        void MergeResourceInto(FMeshResource& Src, FMeshResource& Dst, THashMap<int16, int16>& MaterialRemap)
        {
            const uint32 BaseVert = (uint32)Dst.GetNumVertices();
            const uint32 BaseIdx  = (uint32)Dst.Indices.size();

            if (Src.bSkinnedMesh)
            {
                auto& DstVec = eastl::get<TVector<FSkinnedVertex>>(Dst.Vertices);
                auto& SrcVec = eastl::get<TVector<FSkinnedVertex>>(Src.Vertices);
                DstVec.insert(DstVec.end(), SrcVec.begin(), SrcVec.end());
            }
            else
            {
                auto& DstVec = eastl::get<TVector<FVertex>>(Dst.Vertices);
                auto& SrcVec = eastl::get<TVector<FVertex>>(Src.Vertices);
                DstVec.insert(DstVec.end(), SrcVec.begin(), SrcVec.end());
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

        // 1) Per-mesh transform application. Walking each FMeshResource in
        // parallel is safe: each resource owns its own vertex buffer.
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

        // 2) Skeletons. The bind matrix translation column scales like the
        // vertex positions: scaling the joint world transform by S changes its
        // inverse's translation column by the same S (the rotation half is
        // unaffected because a uniform scale commutes with R^T).
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

        // 3) Animation translations. Rotation/scale keys are unitless so
        // user scale only touches Translation channels.
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

        // 4) Optional merge into a single static + skinned pair. Done before
        // optimize/meshlets so the heavy passes run on the merged geometry.
        if (Options.bMergeMeshes && Data.Resources.size() > 1)
        {
            TUniquePtr<FMeshResource> MergedStatic = MakeUnique<FMeshResource>();
            MergedStatic->Vertices = TVector<FVertex>();

            TUniquePtr<FMeshResource> MergedSkinned = MakeUnique<FMeshResource>();
            MergedSkinned->Vertices = TVector<FSkinnedVertex>();
            MergedSkinned->bSkinnedMesh = true;

            // Inherit a sensible name from the first matching source resource.
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

        // 5) Heavy CPU finalize. Stats are appended to a shared vector so the
        // parallel pass only runs the resource-local optimize/shadow/meshlet
        // work; AnalyzeMeshStatistics runs serially afterwards. Clear any
        // stats from the preview parse first so the indices line up with
        // the (possibly merged) resource list.
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
            GenerateShadowBuffers(M);
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
