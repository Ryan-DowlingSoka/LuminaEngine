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
        uint32      _Pad0           = 0u;
        uint32      _Pad1           = 0u;
        uint32      _Pad2           = 0u;
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
     * Per-chunk metadata consumed by CPU-side chunk culling. The GPU currently
     * draws every chunk as a single instanced draw; when frustum culling is
     * added this array is walked on the CPU to build a per-visible-chunk list
     * before the indirect dispatch is issued.
     */
    struct FTerrainChunkInfo
    {
        glm::vec3   BoundsMin;
        float       _Pad0;
        glm::vec3   BoundsMax;
        float       _Pad1;
        glm::ivec2  ChunkCoord;
        int32       _Pad2;
        int32       _Pad3;
    };
    VERIFY_SSBO_ALIGNMENT(FTerrainChunkInfo);
}
