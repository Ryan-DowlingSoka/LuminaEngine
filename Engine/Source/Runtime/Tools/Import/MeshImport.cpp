#include "PCH.h"
#include "ImportHelpers.h"
#include "Core/Templates/AsBytes.h"
#include "Renderer/MeshData.h"

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

        // 2) Per-surface vertex-cache + overdraw reorder. Cache order first,
        // then overdraw within the ACMR budget so overdraw doesn't undo the
        // cache gains.
        for (FGeometrySurface& Section : MeshResource.GeometrySurfaces)
        {
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
                static_cast<const float*>(MeshResource.GetVertexData()),
                NumVertices, VertexSize, Threshold);
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

        // Per-surface cache optimization on the shadow indices too: the whole
        // point is to submit the compacted index stream to the shadow draws,
        // so we want its cache order to be good as well.
        for (const FGeometrySurface& Section : MeshResource.GeometrySurfaces)
        {
            meshopt_optimizeVertexCache(
                &MeshResource.ShadowIndices[Section.StartIndex],
                &MeshResource.ShadowIndices[Section.StartIndex],
                Section.IndexCount, NumVertices);
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

        // Build per-surface so meshlets never cross material boundaries
        // each meshlet corresponds to one draw slot.
        for (FGeometrySurface& Section : MeshResource.GeometrySurfaces)
        {
            Section.MeshletOffset = (uint32)MeshResource.MeshletData.Meshlets.size();
            Section.MeshletCount  = 0;

            if (Section.IndexCount == 0)
            {
                continue;
            }

            const uint32* SurfaceIndices = &MeshResource.Indices[Section.StartIndex];
            const size_t  MaxMeshlets    = meshopt_buildMeshletsBound(Section.IndexCount, MaxVertices, MaxTriangles);

            TVector<meshopt_Meshlet> LocalMeshlets(MaxMeshlets);
            TVector<uint32>          LocalMeshletVertices(MaxMeshlets * MaxVertices);
            TVector<uint8>           LocalMeshletTriangles(MaxMeshlets * MaxTriangles * 3);

            const size_t MeshletCount = meshopt_buildMeshlets(
                LocalMeshlets.data(),
                LocalMeshletVertices.data(),
                LocalMeshletTriangles.data(),
                SurfaceIndices, Section.IndexCount,
                VertexPositions, NumVertices, VertexSize,
                MaxVertices, MaxTriangles, ConeWeight);

            if (MeshletCount == 0)
            {
                continue;
            }

            LocalMeshlets.resize(MeshletCount);

            // Trim the flat arrays to exactly what buildMeshlets produced.
            // The triangle buffer is padded to a multiple of 4 per meshlet
            // (documented meshopt invariant) so we align the tail the same way.
            const meshopt_Meshlet& Last = LocalMeshlets.back();
            LocalMeshletVertices.resize(Last.vertex_offset + Last.vertex_count);
            LocalMeshletTriangles.resize(Last.triangle_offset + ((Last.triangle_count * 3 + 3) & ~3u));

            // Reorder inside each meshlet for rasterizer locality. Safe to do
            // before bounds because optimizeMeshlet only permutes indices.
            for (const meshopt_Meshlet& M : LocalMeshlets)
            {
                meshopt_optimizeMeshlet(
                    &LocalMeshletVertices[M.vertex_offset],
                    &LocalMeshletTriangles[M.triangle_offset],
                    M.triangle_count, M.vertex_count);
            }

            // Offsets into the global flat arrays — used to rebase the
            // per-surface meshlets before appending.
            const uint32 GlobalVertexBase   = (uint32)MeshResource.MeshletData.MeshletVertices.size();
            const uint32 GlobalTriangleBase = (uint32)MeshResource.MeshletData.MeshletTriangles.size();

            MeshResource.MeshletData.MeshletVertices.insert(
                MeshResource.MeshletData.MeshletVertices.end(),
                LocalMeshletVertices.begin(), LocalMeshletVertices.end());

            MeshResource.MeshletData.MeshletTriangles.insert(
                MeshResource.MeshletData.MeshletTriangles.end(),
                LocalMeshletTriangles.begin(), LocalMeshletTriangles.end());

            MeshResource.MeshletData.Meshlets.reserve(MeshResource.MeshletData.Meshlets.size() + MeshletCount);
            MeshResource.MeshletData.MeshletBounds.reserve(MeshResource.MeshletData.MeshletBounds.size() + MeshletCount);

            for (const meshopt_Meshlet& M : LocalMeshlets)
            {
                FMeshlet Out{};
                Out.VertexOffset   = GlobalVertexBase   + M.vertex_offset;
                Out.TriangleOffset = GlobalTriangleBase + M.triangle_offset;
                Out.VertexCount    = M.vertex_count;
                Out.TriangleCount  = M.triangle_count;
                MeshResource.MeshletData.Meshlets.push_back(Out);

                const meshopt_Bounds B = meshopt_computeMeshletBounds(
                    &LocalMeshletVertices[M.vertex_offset],
                    &LocalMeshletTriangles[M.triangle_offset],
                    M.triangle_count,
                    VertexPositions, NumVertices, VertexSize);

                FMeshletBounds Bounds{};
                Bounds.Center     = glm::vec3(B.center[0],    B.center[1],    B.center[2]);
                Bounds.Radius     = B.radius;
                Bounds.ConeApex   = glm::vec3(B.cone_apex[0], B.cone_apex[1], B.cone_apex[2]);
                Bounds.ConeAxis   = glm::vec3(B.cone_axis[0], B.cone_axis[1], B.cone_axis[2]);
                Bounds.ConeCutoff = B.cone_cutoff;
                MeshResource.MeshletData.MeshletBounds.push_back(Bounds);
            }

            Section.MeshletCount = (uint32)MeshletCount;
        }
    }

    void AnalyzeMeshStatistics(FMeshResource& MeshResource, FMeshStatistics& OutMeshStats)
    {
        OutMeshStats.VertexFetchStatics.emplace_back(meshopt_analyzeVertexFetch(MeshResource.Indices.data(), MeshResource.Indices.size(), MeshResource.GetNumVertices(), MeshResource.GetVertexTypeSize()));
        OutMeshStats.OverdrawStatics.emplace_back(meshopt_analyzeOverdraw(MeshResource.Indices.data(), MeshResource.Indices.size(), static_cast<float*>(MeshResource.GetVertexData()), MeshResource.GetNumVertices(), MeshResource.GetVertexTypeSize()));
    }
}
