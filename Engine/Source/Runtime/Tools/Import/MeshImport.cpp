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

    void AnalyzeMeshStatistics(FMeshResource& MeshResource, FMeshStatistics& OutMeshStats)
    {
        OutMeshStats.VertexFetchStatics.emplace_back(meshopt_analyzeVertexFetch(MeshResource.Indices.data(), MeshResource.Indices.size(), MeshResource.GetNumVertices(), MeshResource.GetVertexTypeSize()));
        OutMeshStats.OverdrawStatics.emplace_back(meshopt_analyzeOverdraw(MeshResource.Indices.data(), MeshResource.Indices.size(), static_cast<float*>(MeshResource.GetVertexData()), MeshResource.GetNumVertices(), MeshResource.GetVertexTypeSize()));
    }
}
