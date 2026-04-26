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

    /**
     * Per-meshlet quad span. Each meshlet covers up to GTerrainMeshletQuads^2 quads
     * = (GTerrainMeshletQuads + 1)^2 vertices = 2 * GTerrainMeshletQuads^2 triangles.
     * Sized so a perfect-square meshlet fits inside the renderer's MESHLET_MAX_VERTICES
     * (64) and MESHLET_MAX_TRIANGLES (124) bounds: 7^2 = 49 quads, 8x8 = 64 verts,
     * 98 triangles.
     */
    constexpr int32 GTerrainMeshletQuads        = 7;
    constexpr int32 GTerrainMeshletVertsPerSide = GTerrainMeshletQuads + 1;       // 8
    constexpr int32 GTerrainMeshletMaxQuads     = GTerrainMeshletQuads * GTerrainMeshletQuads;     // 49
    constexpr int32 GTerrainMeshletMaxTris      = GTerrainMeshletMaxQuads * 2;    // 98
    constexpr int32 GTerrainMeshletMaxVerts     = GTerrainMeshletMaxTris * 3;     // 294 (six verts per quad emitted by VS)

    /**
     * CPU mirror of the terrain render params constant buffer. Layout must match
     * FTerrainRenderParams in TerrainBaseVertexPass.slang / TerrainBasePixelPass.slang
     * exactly. The MaterialIndex slot resolves per-terrain material uniforms through
     * the same bindless material buffer used by regular meshes; per-layer textures
     * and UV scales now live inside the material graph and are not pushed through
     * this buffer.
     */
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

    /**
     * Constant buffer feeding the normal-recompute compute shader. Matches
     * FTerrainNormalParams in TerrainNormalCompute.slang.
     */
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

    /**
     * Per-chunk metadata uploaded to the GPU. Used by the terrain cull compute
     * pass to early-reject whole chunks against the camera frustum and Hi-Z
     * pyramid before walking individual meshlets, and consumed in the vertex
     * shader to translate from meshlet-local quad coords to world space.
     *
     * BoundsMin/Max are tight world-space AABBs derived from the heightmap
     * sample range across the chunk.
     */
    struct alignas(16) FTerrainChunkInfo
    {
        glm::vec3   BoundsMin       = glm::vec3(0.0f);
        float       _Pad0           = 0.0f;
        glm::vec3   BoundsMax       = glm::vec3(0.0f);
        float       _Pad1           = 0.0f;

        // Origin of the chunk in the global heightmap quad grid (samples).
        // QuadOriginXY = ChunkCoord * QuadsPerChunk.
        glm::ivec2  ChunkCoord      = glm::ivec2(0);
        glm::ivec2  QuadOrigin      = glm::ivec2(0);

        // Range into the per-terrain meshlet array. MeshletCount mirrors
        // MeshletsPerChunkSide^2 today, but is stored explicitly so partial
        // chunks at the terrain edge can carry fewer meshlets later.
        uint32      MeshletOffset   = 0u;
        uint32      MeshletCount    = 0u;
        uint32      _Pad2           = 0u;
        uint32      _Pad3           = 0u;
    };
    VERIFY_SSBO_ALIGNMENT(FTerrainChunkInfo);

    /**
     * Per-meshlet metadata. Each meshlet owns a (QuadCountX x QuadCountY) sub-
     * region of its parent chunk's quad grid, where each axis is at most
     * GTerrainMeshletQuads. Bounds are world-space and tight to the heightmap.
     *
     * The cull pass tests Bounds against the camera frustum and Hi-Z pyramid;
     * survivors append (ChunkIndex, MeshletIndex) into the visible-meshlet list.
     */
    struct alignas(16) FTerrainMeshletInfo
    {
        glm::vec3   BoundsMin       = glm::vec3(0.0f);
        float       _Pad0           = 0.0f;
        glm::vec3   BoundsMax       = glm::vec3(0.0f);
        float       _Pad1           = 0.0f;

        // Quad-grid origin of this meshlet within its parent chunk.
        glm::ivec2  ChunkLocalQuadOrigin = glm::ivec2(0);
        // Quads covered along each axis. Always <= GTerrainMeshletQuads;
        // smaller for partial meshlets at the chunk's far edge.
        glm::ivec2  QuadExtent      = glm::ivec2(0);

        // Index of the parent chunk (into FTerrainChunkInfo[]).
        uint32      ChunkIndex      = 0u;
        uint32      _Pad2           = 0u;
        uint32      _Pad3           = 0u;
        uint32      _Pad4           = 0u;
    };
    VERIFY_SSBO_ALIGNMENT(FTerrainMeshletInfo);

    /**
     * Output entry from the terrain cull compute pass. One slot per surviving
     * meshlet. Consumed by the terrain vertex shader through SV_InstanceID.
     */
    struct alignas(8) FTerrainVisibleMeshlet
    {
        uint32  ChunkIndex   = 0u;
        uint32  MeshletIndex = 0u;
    };
    static_assert(sizeof(FTerrainVisibleMeshlet) == 8);

    /**
     * Push-constant payload for TerrainCull.slang. Camera frustum lives in
     * the shared scene globals; only the per-terrain layout is pushed here.
     */
    struct FTerrainCullPushConstants
    {
        uint32  ChunkCount      = 0u;
        uint32  MeshletCount    = 0u;
        uint32  _Pad0           = 0u;
        uint32  _Pad1           = 0u;
    };
}
