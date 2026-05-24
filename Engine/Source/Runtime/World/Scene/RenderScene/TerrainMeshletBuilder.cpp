#include "pch.h"
#include "TerrainMeshletBuilder.h"
#include "World/Entity/Components/TerrainComponent.h"

namespace Lumina::TerrainMeshletBuilder
{
    namespace
    {
        // Sample (X, Y) clamped, returns world-space height (post-MaxHeight scale).
        FORCEINLINE float SampleWorldHeight(
            const TVector<float>& Heightmap,
            int32                 SampleX,
            int32                 SampleY,
            int32                 Resolution,
            float                 MaxHeight)
        {
            SampleX = glm::clamp(SampleX, 0, Resolution - 1);
            SampleY = glm::clamp(SampleY, 0, Resolution - 1);
            const size_t Index = (size_t)SampleY * (size_t)Resolution + (size_t)SampleX;
            return Heightmap[Index] * MaxHeight;
        }
    }

    void Build(STerrainComponent& Terrain, const glm::vec3& WorldOrigin)
    {
        FTerrainGPUState& State = Terrain.GPUState;
        State.Chunks.clear();
        State.Meshlets.clear();

        const int32 Resolution = Terrain.Resolution;
        const int32 ChunkRes   = Terrain.ChunkResolution;
        if (Resolution < 2 || ChunkRes < 2)
        {
            return;
        }
        if ((int32)Terrain.Heightmap.size() != Resolution * Resolution)
        {
            // Heightmap not sized yet; renderer will retry next frame after EnsureTerrainCpuBuffers().
            return;
        }

        const int32 QuadsPerChunk        = ChunkRes - 1;
        const int32 ChunksPerSide        = std::max(1, (Resolution - 1) / QuadsPerChunk);
        const int32 MeshletQuadSide      = GTerrainMeshletQuads;
        const int32 MeshletsPerChunkSide = (QuadsPerChunk + MeshletQuadSide - 1) / MeshletQuadSide;
        const int32 MeshletsPerChunk     = MeshletsPerChunkSide * MeshletsPerChunkSide;
        const int32 NumChunks            = ChunksPerSide * ChunksPerSide;
        const int32 NumMeshlets          = NumChunks * MeshletsPerChunk;

        const float HalfSize             = Terrain.TileWorldSize * 0.5f;
        const float Stride               = Terrain.TileWorldSize / float(Resolution - 1);
        const glm::vec2 OriginXZ         = glm::vec2(WorldOrigin.x - HalfSize, WorldOrigin.z - HalfSize);
        const float WorldOriginY         = WorldOrigin.y;
        const float MaxHeight            = Terrain.MaxHeight;

        State.Chunks.resize(NumChunks);
        State.Meshlets.resize(NumMeshlets);

        const float BoundsDilateXZ = Stride * 0.5f;
        // Y dilation: matrix rounding can produce Y values microscopically outside the CPU range -> silhouette culling.
        const float BoundsDilateY  = std::max(0.05f, MaxHeight * 0.01f);

        for (int32 cy = 0; cy < ChunksPerSide; ++cy)
        {
            for (int32 cx = 0; cx < ChunksPerSide; ++cx)
            {
                const int32 ChunkIndex = cy * ChunksPerSide + cx;

                FTerrainChunkInfo& Chunk = State.Chunks[ChunkIndex];
                Chunk.ChunkCoord    = glm::ivec2(cx, cy);
                Chunk.QuadOrigin    = glm::ivec2(cx * QuadsPerChunk, cy * QuadsPerChunk);
                Chunk.MeshletOffset = (uint32)(ChunkIndex * MeshletsPerChunk);
                Chunk.MeshletCount  = (uint32)MeshletsPerChunk;

                float ChunkHeightMin =  std::numeric_limits<float>::infinity();
                float ChunkHeightMax = -std::numeric_limits<float>::infinity();

                for (int32 my = 0; my < MeshletsPerChunkSide; ++my)
                {
                    for (int32 mx = 0; mx < MeshletsPerChunkSide; ++mx)
                    {
                        const int32 LocalMeshletIndex = my * MeshletsPerChunkSide + mx;
                        const int32 MeshletGlobalIndex = ChunkIndex * MeshletsPerChunk + LocalMeshletIndex;

                        FTerrainMeshletInfo& Meshlet = State.Meshlets[MeshletGlobalIndex];
                        Meshlet.ChunkIndex            = (uint32)ChunkIndex;
                        Meshlet.ChunkLocalQuadOrigin  = glm::ivec2(mx * MeshletQuadSide, my * MeshletQuadSide);

                        const int32 RemainingX = QuadsPerChunk - Meshlet.ChunkLocalQuadOrigin.x;
                        const int32 RemainingY = QuadsPerChunk - Meshlet.ChunkLocalQuadOrigin.y;
                        Meshlet.QuadExtent = glm::ivec2(
                            std::min(MeshletQuadSide, RemainingX),
                            std::min(MeshletQuadSide, RemainingY));

                        const int32 SampleX0 = Chunk.QuadOrigin.x + Meshlet.ChunkLocalQuadOrigin.x;
                        const int32 SampleY0 = Chunk.QuadOrigin.y + Meshlet.ChunkLocalQuadOrigin.y;
                        const int32 NVertsX  = Meshlet.QuadExtent.x + 1;
                        const int32 NVertsY  = Meshlet.QuadExtent.y + 1;

                        float MinH =  std::numeric_limits<float>::infinity();
                        float MaxH = -std::numeric_limits<float>::infinity();
                        for (int32 vy = 0; vy < NVertsY; ++vy)
                        {
                            for (int32 vx = 0; vx < NVertsX; ++vx)
                            {
                                const float H = SampleWorldHeight(
                                    Terrain.Heightmap,
                                    SampleX0 + vx,
                                    SampleY0 + vy,
                                    Resolution,
                                    MaxHeight);
                                MinH = std::min(MinH, H);
                                MaxH = std::max(MaxH, H);
                            }
                        }

                        ChunkHeightMin = std::min(ChunkHeightMin, MinH);
                        ChunkHeightMax = std::max(ChunkHeightMax, MaxH);

                        // Convert sample-index extents to world-space XZ.
                        const float WorldXMin = OriginXZ.x + float(SampleX0) * Stride - BoundsDilateXZ;
                        const float WorldXMax = OriginXZ.x + float(SampleX0 + NVertsX - 1) * Stride + BoundsDilateXZ;
                        const float WorldZMin = OriginXZ.y + float(SampleY0) * Stride - BoundsDilateXZ;
                        const float WorldZMax = OriginXZ.y + float(SampleY0 + NVertsY - 1) * Stride + BoundsDilateXZ;

                        Meshlet.BoundsMin = glm::vec3(WorldXMin, WorldOriginY + MinH - BoundsDilateY, WorldZMin);
                        Meshlet.BoundsMax = glm::vec3(WorldXMax, WorldOriginY + MaxH + BoundsDilateY, WorldZMax);
                    }
                }

                const float ChunkXMin = OriginXZ.x + float(Chunk.QuadOrigin.x) * Stride - BoundsDilateXZ;
                const float ChunkXMax = OriginXZ.x + float(Chunk.QuadOrigin.x + QuadsPerChunk) * Stride + BoundsDilateXZ;
                const float ChunkZMin = OriginXZ.y + float(Chunk.QuadOrigin.y) * Stride - BoundsDilateXZ;
                const float ChunkZMax = OriginXZ.y + float(Chunk.QuadOrigin.y + QuadsPerChunk) * Stride + BoundsDilateXZ;

                Chunk.BoundsMin = glm::vec3(ChunkXMin, WorldOriginY + ChunkHeightMin - BoundsDilateY, ChunkZMin);
                Chunk.BoundsMax = glm::vec3(ChunkXMax, WorldOriginY + ChunkHeightMax + BoundsDilateY, ChunkZMax);
            }
        }
    }
}
