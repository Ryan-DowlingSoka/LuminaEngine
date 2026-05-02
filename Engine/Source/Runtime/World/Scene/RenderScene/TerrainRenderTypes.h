#pragma once

#include <glm/glm.hpp>
#include "Platform/GenericPlatform.h"

#ifndef VERIFY_SSBO_ALIGNMENT
#define VERIFY_SSBO_ALIGNMENT(Type) \
    static_assert(sizeof(Type) % 16 == 0, #Type " must be 16-byte aligned");
#endif

namespace Lumina
{
    /** Maximum number of paintable layers the renderer blends in the terrain pixel shader. */
    constexpr uint32 GTerrainMaxLayers = 4;

    // 7x7 quads = 49 quads, 8x8 verts (64), 98 tris -- fits MESHLET_MAX_VERTICES/TRIANGLES.
    constexpr int32 GTerrainMeshletQuads        = 7;
    constexpr int32 GTerrainMeshletVertsPerSide = GTerrainMeshletQuads + 1;
    constexpr int32 GTerrainMeshletMaxQuads     = GTerrainMeshletQuads * GTerrainMeshletQuads;
    constexpr int32 GTerrainMeshletMaxTris      = GTerrainMeshletMaxQuads * 2;
    constexpr int32 GTerrainMeshletMaxVerts     = GTerrainMeshletMaxTris * 3;

    /** Mirror of FTerrainRenderParams in TerrainBase{Vertex,Pixel}Pass.slang. */
    struct alignas(16) FTerrainRenderParams
    {
        glm::vec2   OriginXZ        = glm::vec2(0.0f);
        float       TileWorldSize   = 4096.0f;
        float       MaxHeight       = 256.0f;

        int32       Resolution      = 0;
        int32       ChunkResolution = 0;
        int32       ChunksPerSide   = 0;
        int32       LayerCount      = 0;

        glm::vec3   WorldOriginY    = glm::vec3(0.0f);
        uint32      EntityID        = 0u;

        uint32      MaterialIndex   = 0u;
        int32       MeshletsPerChunkSide = 0;
        int32       MeshletQuadSide      = GTerrainMeshletQuads;
        uint32      _Pad0           = 0u;
    };
    VERIFY_SSBO_ALIGNMENT(FTerrainRenderParams);

    /** Mirror of FTerrainNormalParams in TerrainNormalCompute.slang. */
    struct alignas(16) FTerrainNormalParams
    {
        int32   Resolution   = 0;
        int32   RegionMinX   = 0;
        int32   RegionMinY   = 0;
        int32   RegionSizeX  = 0;

        int32   RegionSizeY   = 0;
        float   TileWorldSize = 4096.0f;
        float   MaxHeight     = 256.0f;
        int32   _Pad          = 0;
    };
    VERIFY_SSBO_ALIGNMENT(FTerrainNormalParams);

    /** Per-chunk metadata: tight world AABBs + meshlet range. Cull pass tests against frustum/HiZ. */
    struct alignas(16) FTerrainChunkInfo
    {
        glm::vec3   BoundsMin       = glm::vec3(0.0f);
        float       _Pad0           = 0.0f;
        glm::vec3   BoundsMax       = glm::vec3(0.0f);
        float       _Pad1           = 0.0f;

        // QuadOriginXY = ChunkCoord * QuadsPerChunk.
        glm::ivec2  ChunkCoord      = glm::ivec2(0);
        glm::ivec2  QuadOrigin      = glm::ivec2(0);

        // Range into per-terrain meshlet array; MeshletCount stored for future partial chunks.
        uint32      MeshletOffset   = 0u;
        uint32      MeshletCount    = 0u;
        uint32      _Pad2           = 0u;
        uint32      _Pad3           = 0u;
    };
    VERIFY_SSBO_ALIGNMENT(FTerrainChunkInfo);

    /** Per-meshlet sub-region of its parent chunk's quad grid; cull pass appends survivors. */
    struct alignas(16) FTerrainMeshletInfo
    {
        glm::vec3   BoundsMin       = glm::vec3(0.0f);
        float       _Pad0           = 0.0f;
        glm::vec3   BoundsMax       = glm::vec3(0.0f);
        float       _Pad1           = 0.0f;

        glm::ivec2  ChunkLocalQuadOrigin = glm::ivec2(0);
        // Quads covered per axis; smaller for partial meshlets at chunk far edge.
        glm::ivec2  QuadExtent      = glm::ivec2(0);

        uint32      ChunkIndex      = 0u;
        uint32      _Pad2           = 0u;
        uint32      _Pad3           = 0u;
        uint32      _Pad4           = 0u;
    };
    VERIFY_SSBO_ALIGNMENT(FTerrainMeshletInfo);

    /** Survivor slot from terrain cull; consumed by VS via SV_InstanceID. */
    struct alignas(8) FTerrainVisibleMeshlet
    {
        uint32  ChunkIndex   = 0u;
        uint32  MeshletIndex = 0u;
    };
    static_assert(sizeof(FTerrainVisibleMeshlet) == 8);

    /** TerrainCull.slang push constants; frustum lives in scene globals. */
    struct FTerrainCullPushConstants
    {
        uint32  ChunkCount      = 0u;
        uint32  MeshletCount    = 0u;
        uint32  _Pad0           = 0u;
        uint32  _Pad1           = 0u;
    };
}
