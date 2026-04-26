#include <algorithm>

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

    namespace
    {
        // Default ratios (vs LOD0 index count) for the simplification ladder.
        // 50% / 25% / 12.5% triangles is the textbook ramp; meshopt's
        // welded-collapse simplifier keeps surface direction surprisingly
        // well at these targets so the silhouette holds at typical viewing
        // distances.
        constexpr float kLODRatios[MAX_MESH_LODS]            = { 1.0f, 0.5f, 0.25f, 0.125f };
        // distance/radius ratio at which each LOD becomes the chosen LOD.
        // index 0 unused (LOD 0 is the default). 8/16/32 means LOD 1 kicks
        // in at ~12% of screen, LOD 2 at ~6%, LOD 3 at ~3%.
        constexpr float kLODScreenThresholds[MAX_MESH_LODS]  = { 0.0f, 8.0f, 16.0f, 32.0f };
        // Target error fed to meshopt_simplify (model-AABB-relative). Larger
        // values let the simplifier hit lower index counts on noisy meshes
        // at the cost of silhouette fidelity.
        constexpr float kLODTargetError                      = 0.05f;

        struct FSurfaceMeshletResult
        {
            TVector<uint32>          Vertices;       // local meshlet vertex indices
            TVector<uint8>           Triangles;      // local micro-indices
            TVector<FMeshlet>        OutMeshlets;    // local offsets
            TVector<FMeshletBounds>  Bounds;
            TVector<glm::vec3>       MeshletLo;      // per-meshlet world-space min P
            glm::vec3                MaxExtent = glm::vec3(0.0f);
            bool                     bHasData  = false;
        };

        // Build meshlets for one (LOD, Surface) cell from a (possibly
        // simplified) index range. Writes into Result; sets bHasData=true on
        // success. Position quantization is *not* done here -- the global
        // grid is sized after every LOD's meshlets exist, then a serial pack
        // pass turns Result into appended FMeshletData entries.
        template<typename TReadPos>
        void BuildLODMeshletsForRange(
            const uint32* SrcIndices, size_t SrcIndexCount,
            const float*  VertexPositions, size_t NumVertices, size_t VertexSize,
            TReadPos&&    ReadPosition,
            FSurfaceMeshletResult& Result)
        {
            constexpr size_t MaxVertices  = MESHLET_MAX_VERTICES;
            constexpr size_t MaxTriangles = MESHLET_MAX_TRIANGLES;
            constexpr float  ConeWeight   = 0.25f;

            if (SrcIndexCount < 3)
            {
                return;
            }

            const size_t MaxMeshlets = meshopt_buildMeshletsBound(SrcIndexCount, MaxVertices, MaxTriangles);

            TVector<meshopt_Meshlet> LocalMeshlets(MaxMeshlets);
            Result.Vertices.resize(MaxMeshlets * MaxVertices);
            Result.Triangles.resize(MaxMeshlets * MaxTriangles * 3);

            const size_t MeshletCount = meshopt_buildMeshlets(
                LocalMeshlets.data(),
                Result.Vertices.data(),
                Result.Triangles.data(),
                SrcIndices, SrcIndexCount,
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

            Result.bHasData = !Result.OutMeshlets.empty();
        }
    } // namespace

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
                Section.NumLODs       = 1;
                for (uint32 i = 0; i < MAX_MESH_LODS; ++i)
                {
                    Section.LODMeshletOffset[i]   = 0;
                    Section.LODMeshletCount[i]    = 0;
                    Section.LODScreenThreshold[i] = i == 0 ? 0.0f : FLT_MAX;
                }
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

        const uint32 NumSurfaces = (uint32)MeshResource.GeometrySurfaces.size();

        // Phase 1: per-surface parallel build of every LOD. Each cell
        // [LOD * NumSurfaces + SurfaceIdx] is owned exclusively by one
        // worker, so the parallel writes are race-free without a barrier.
        TVector<FSurfaceMeshletResult> Results(MAX_MESH_LODS * NumSurfaces);

        // Per-surface count of LODs that produced usable meshlets. LOD 0
        // always succeeds when the surface has indices; later LODs may
        // bail out if simplification can't make further progress.
        TVector<uint32> PerSurfaceNumLODs(NumSurfaces, 0u);

        Task::ParallelFor(NumSurfaces, [&](uint32 SurfaceIdx)
        {
            const FGeometrySurface& Section = MeshResource.GeometrySurfaces[SurfaceIdx];
            if (Section.IndexCount == 0)
            {
                PerSurfaceNumLODs[SurfaceIdx] = 0;
                return;
            }

            const uint32* SurfaceIndices = &MeshResource.Indices[Section.StartIndex];

            // LOD 0: build directly from the optimized source slice.
            FSurfaceMeshletResult& LOD0 = Results[0 * NumSurfaces + SurfaceIdx];
            BuildLODMeshletsForRange(
                SurfaceIndices, Section.IndexCount,
                VertexPositions, NumVertices, VertexSize,
                ReadPosition, LOD0);

            uint32 LODsBuilt = LOD0.bHasData ? 1u : 0u;

            // Scratch buffer reused across LODs of this surface.
            TVector<uint32> Simplified(Section.IndexCount);
            size_t LastIndexCount = Section.IndexCount;

            for (uint32 lod = 1; lod < MAX_MESH_LODS; ++lod)
            {
                // Target index count, snapped to a multiple of 3 (must be
                // whole triangles). meshopt clamps internally too, but we
                // also reject anything below a 2-triangle floor.
                size_t TargetIndices = (size_t)((float)Section.IndexCount * kLODRatios[lod]);
                TargetIndices = (TargetIndices / 3u) * 3u;
                if (TargetIndices < 6u)
                {
                    break;
                }

                float ResultError = 0.0f;
                const size_t NewCount = meshopt_simplify(
                    Simplified.data(),
                    SurfaceIndices, Section.IndexCount,
                    VertexPositions, NumVertices, VertexSize,
                    TargetIndices, kLODTargetError,
                    meshopt_SimplifyLockBorder,
                    &ResultError);

                // Bail out once the simplifier stops making meaningful
                // progress -- duplicating LOD0 burns memory and bandwidth.
                // A 5% reduction floor catches the "topology forced us to
                // stop" case where the next ratio would also fail.
                if (NewCount < 6u || (float)NewCount > (float)LastIndexCount * 0.95f)
                {
                    break;
                }
                LastIndexCount = NewCount;

                // Restore vertex-cache locality on the simplified output;
                // simplification reorders triangles by collapse priority.
                meshopt_optimizeVertexCache(
                    Simplified.data(),
                    Simplified.data(),
                    NewCount, NumVertices);

                FSurfaceMeshletResult& LODi = Results[lod * NumSurfaces + SurfaceIdx];
                BuildLODMeshletsForRange(
                    Simplified.data(), NewCount,
                    VertexPositions, NumVertices, VertexSize,
                    ReadPosition, LODi);

                if (!LODi.bHasData)
                {
                    break;
                }
                LODsBuilt = lod + 1u;
            }

            PerSurfaceNumLODs[SurfaceIdx] = LODsBuilt;
        });

        // Mesh AABB + per-axis max meshlet extent. Reduce extents across
        // *every* (LOD, surface) cell so the global quantization grid is
        // sized for the coarsest LOD's largest meshlet.
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
            if (R.bHasData)
            {
                MaxMeshletExtent = glm::max(MaxMeshletExtent, R.MaxExtent);
            }
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

        // Reserve totals across every LOD.
        size_t TotalMeshlets  = 0;
        size_t TotalVertices  = 0;
        size_t TotalTriangles = 0;
        for (const FSurfaceMeshletResult& R : Results)
        {
            if (!R.bHasData)
            {
                continue;
            }
            TotalMeshlets += R.OutMeshlets.size();
            TotalVertices += R.Vertices.size();
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

        // Initialize per-surface LOD descriptors. Defaults: LOD 0 active,
        // higher LODs marked unavailable (FLT_MAX threshold).
        for (FGeometrySurface& Section : MeshResource.GeometrySurfaces)
        {
            Section.NumLODs       = 1;
            Section.MeshletOffset = 0;
            Section.MeshletCount  = 0;
            for (uint32 i = 0; i < MAX_MESH_LODS; ++i)
            {
                Section.LODMeshletOffset[i]   = 0;
                Section.LODMeshletCount[i]    = 0;
                Section.LODScreenThreshold[i] = i == 0 ? 0.0f : FLT_MAX;
            }
        }

        // Phase 3: serial pack. Iterate LOD-major so the legacy
        // (MeshletOffset, MeshletCount) range that mirrors LOD 0 is at the
        // front of the buffer -- keeps any code that reads only those
        // legacy fields well-defined.
        for (uint32 lod = 0; lod < MAX_MESH_LODS; ++lod)
        {
            for (uint32 SurfaceIdx = 0; SurfaceIdx < NumSurfaces; ++SurfaceIdx)
            {
                FGeometrySurface&      Section = MeshResource.GeometrySurfaces[SurfaceIdx];
                FSurfaceMeshletResult& Result  = Results[lod * NumSurfaces + SurfaceIdx];

                if (!Result.bHasData)
                {
                    continue;
                }

                // Surfaces that bailed out earlier than this LOD index
                // shouldn't have it counted. Guard against simplification
                // succeeding non-monotonically (e.g. LOD 1 fails but LOD 2
                // happens to fit); we treat per-surface NumLODs as a dense
                // run from 0 to PerSurfaceNumLODs[i].
                if (lod >= PerSurfaceNumLODs[SurfaceIdx])
                {
                    continue;
                }

                const uint32 LODOffset = (uint32)MeshResource.MeshletData.Meshlets.size();
                const uint32 LODCount  = (uint32)Result.OutMeshlets.size();

                Section.LODMeshletOffset[lod] = LODOffset;
                Section.LODMeshletCount[lod]  = LODCount;
                Section.LODScreenThreshold[lod] = kLODScreenThresholds[lod];

                if (lod == 0)
                {
                    Section.MeshletOffset = LODOffset;
                    Section.MeshletCount  = LODCount;
                }

                Section.NumLODs = std::max(lod + 1u, Section.NumLODs);

                for (size_t MeshletIdx = 0; MeshletIdx < Result.OutMeshlets.size(); ++MeshletIdx)
                {
                    FMeshlet Out = Result.OutMeshlets[MeshletIdx];

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
        // Coalesce surfaces that share a MaterialIndex into one surface each.
        // Importers (especially glTF) emit one FGeometrySurface per source
        // primitive, which can blow up to thousands of surfaces sharing a
        // handful of materials and quadratically inflate scene-prep CPU.
        // We rebuild the index buffer so each material's indices are
        // contiguous, then collapse the surface list.
        void MergeSurfacesByMaterial(FMeshResource& MeshResource)
        {
            const TVector<FGeometrySurface>& OldSurfaces = MeshResource.GeometrySurfaces;
            if (OldSurfaces.size() <= 1 || MeshResource.Indices.empty())
            {
                return;
            }

            // First-seen MaterialIndex order. Stable so material slot indices
            // line up with the order downstream code expects.
            THashMap<int16, uint32> MaterialToNewIdx;
            TVector<int16>          MaterialOrder;
            MaterialOrder.reserve(OldSurfaces.size());

            for (const FGeometrySurface& S : OldSurfaces)
            {
                if (MaterialToNewIdx.find(S.MaterialIndex) == MaterialToNewIdx.end())
                {
                    MaterialToNewIdx.emplace(S.MaterialIndex, (uint32)MaterialOrder.size());
                    MaterialOrder.push_back(S.MaterialIndex);
                }
            }

            if (MaterialOrder.size() == OldSurfaces.size())
            {
                return; // Already 1:1.
            }

            const uint32 NumMerged = (uint32)MaterialOrder.size();

            // Per-merged-surface index totals + start offsets.
            TVector<uint32> CountPerMerged(NumMerged, 0u);
            for (const FGeometrySurface& S : OldSurfaces)
            {
                CountPerMerged[MaterialToNewIdx[S.MaterialIndex]] += S.IndexCount;
            }

            TVector<uint32> StartPerMerged(NumMerged, 0u);
            uint32 Running = 0;
            for (uint32 i = 0; i < NumMerged; ++i)
            {
                StartPerMerged[i] = Running;
                Running += CountPerMerged[i];
            }

            // Scatter old slices into their merged contiguous range.
            TVector<uint32> NewIndices(MeshResource.Indices.size());
            TVector<uint32> WriteCursor = StartPerMerged;
            for (const FGeometrySurface& S : OldSurfaces)
            {
                if (S.IndexCount == 0)
                {
                    continue;
                }
                const uint32 NewIdx = MaterialToNewIdx[S.MaterialIndex];
                memcpy(NewIndices.data() + WriteCursor[NewIdx],
                       MeshResource.Indices.data() + S.StartIndex,
                       S.IndexCount * sizeof(uint32));
                WriteCursor[NewIdx] += S.IndexCount;
            }

            // Build the new surface list. Inherit the first source surface's
            // ID so re-imports keep stable surface identities.
            TVector<FGeometrySurface> NewSurfaces;
            NewSurfaces.reserve(NumMerged);
            for (uint32 i = 0; i < NumMerged; ++i)
            {
                FGeometrySurface NewSurface;
                for (const FGeometrySurface& Old : OldSurfaces)
                {
                    if (Old.MaterialIndex == MaterialOrder[i])
                    {
                        NewSurface.ID = Old.ID;
                        break;
                    }
                }
                NewSurface.IndexCount    = CountPerMerged[i];
                NewSurface.StartIndex    = StartPerMerged[i];
                NewSurface.MaterialIndex = MaterialOrder[i];
                NewSurfaces.push_back(NewSurface);
            }

            MeshResource.Indices          = Move(NewIndices);
            MeshResource.GeometrySurfaces = Move(NewSurfaces);
        }

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
            // Coalesce surfaces by material first so the optimize pass operates
            // on the bigger contiguous slices and the renderer iterates one
            // FGeometrySurface per material instead of per source primitive.
            MergeSurfacesByMaterial(M);
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
