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

        // Mesh-global quantization basis. Shared across every meshlet so
        // vertices duplicated at meshlet seams map to the same dequantized
        // world position; per-meshlet AABBs would T-junction along seams.
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

        // Degenerate axis (extent==0) gets a zero scale; the dequant then
        // returns MeshAABBMin exactly for any quantized value on that axis.
        const glm::vec3 MeshExtent = MeshHi - MeshLo;
        glm::vec3 MeshScale(0.0f);
        if (MeshExtent.x > 0.0f) MeshScale.x = MeshExtent.x / 65535.0f;
        if (MeshExtent.y > 0.0f) MeshScale.y = MeshExtent.y / 65535.0f;
        if (MeshExtent.z > 0.0f) MeshScale.z = MeshExtent.z / 65535.0f;

        MeshResource.MeshletData.MeshAABBMin   = MeshLo;
        MeshResource.MeshletData.MeshAABBScale = MeshScale;

        // Phase 2: emit a packed FMeshlet(Skinned)Vertex slice for each
        // meshlet with positions quantized against the mesh-global AABB and
        // normals octahedral-encoded, then repack triangle micro-indices
        // into one dword per triangle.
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
                FMeshlet         Out    = Result.OutMeshlets[MeshletIdx];
                FMeshletBounds   Bounds = Result.Bounds[MeshletIdx];

                const uint32 PackedVertexStart = MeshResource.bSkinnedMesh
                    ? (uint32)MeshResource.MeshletData.MeshletSkinnedVertices.size()
                    : (uint32)MeshResource.MeshletData.MeshletVertices.size();

                if (MeshResource.bSkinnedMesh)
                {
                    const auto& Verts = eastl::get<TVector<FSkinnedVertex>>(MeshResource.Vertices);
                    for (uint32 i = 0; i < Out.VertexCount; ++i)
                    {
                        const uint32         SrcIdx = Result.Vertices[Out.VertexOffset + i];
                        const FSkinnedVertex& V     = Verts[SrcIdx];

                        const FPackedMeshletPosition PP = PackMeshletPosition(V.Position, MeshLo, MeshExtent);

                        FMeshletSkinnedVertex Packed{};
                        Packed.PositionXY   = PP.XY;
                        Packed.PositionZ    = PP.Z;
                        Packed.Normal       = V.Normal;
                        Packed.UV           = V.UV;
                        Packed.Color        = V.Color;
                        Packed.JointIndices = (uint32)V.JointIndices.x
                                            | ((uint32)V.JointIndices.y << 8)
                                            | ((uint32)V.JointIndices.z << 16)
                                            | ((uint32)V.JointIndices.w << 24);
                        Packed.JointWeights = (uint32)V.JointWeights.x
                                            | ((uint32)V.JointWeights.y << 8)
                                            | ((uint32)V.JointWeights.z << 16)
                                            | ((uint32)V.JointWeights.w << 24);
                        MeshResource.MeshletData.MeshletSkinnedVertices.push_back(Packed);
                    }
                }
                else
                {
                    const auto& Verts = eastl::get<TVector<FVertex>>(MeshResource.Vertices);
                    for (uint32 i = 0; i < Out.VertexCount; ++i)
                    {
                        const uint32   SrcIdx = Result.Vertices[Out.VertexOffset + i];
                        const FVertex& V      = Verts[SrcIdx];

                        const FPackedMeshletPosition PP = PackMeshletPosition(V.Position, MeshLo, MeshExtent);

                        FMeshletVertex Packed{};
                        Packed.PositionXY = PP.XY;
                        Packed.PositionZ  = PP.Z;
                        Packed.Normal     = V.Normal;
                        Packed.UV         = V.UV;
                        Packed.Color      = V.Color;
                        MeshResource.MeshletData.MeshletVertices.push_back(Packed);
                    }
                }

                // (3) Repack triangle micro-indices.
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
                MeshResource.MeshletData.MeshletBounds.push_back(Bounds);
            }
        }

        // Avoid a zero-byte SSBO upload for empty meshes.
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
        // Concatenate Src into Dst. Indices are rebased by Dst's current vertex
        // count, surface StartIndex by Dst's current index count. Material
        // indices are folded onto a unique-slot remap so primitives sharing
        // the same source material end up on the same slot in the merged asset.
        // Src.ImportTransform is the source asset's scene-graph world transform
        // for this mesh; it is baked into positions and normals as we copy so
        // each merged primitive lands at its authored world placement instead
        // of collapsing onto its mesh-local origin.
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
