#pragma once

#include "TerrainRenderTypes.h"
#include "Containers/Array.h"

namespace Lumina
{
    struct STerrainComponent;

    /**
     * Stateless helpers that build the CPU-side chunk + meshlet metadata used
     * by the GPU cull pass. The bounds for each chunk and meshlet are tight
     * world-space AABBs derived from the heightmap sample range across the
     * patch they cover.
     *
     * Called by the renderer when STerrainComponent::GPUState.bChunksDirty is
     * set: full rebuild of the metadata, GPU upload, and a freshly-sized
     * indirect-args / visible-meshlet buffer follow.
     */
    namespace TerrainMeshletBuilder
    {
        /**
         * Walks the heightmap, populates Terrain.GPUState.Chunks and
         * Terrain.GPUState.Meshlets from scratch. Bounds are computed in
         * world space using the same OriginXZ / TileWorldSize / MaxHeight
         * conventions the vertex shader uses, and are inflated by half a
         * grid step on each axis so meshlet rasterization sliver-coverage
         * doesn't fall outside the cull AABB.
         *
         * Pass the terrain's world-space transform origin (the entity's
         * STransformComponent location); meshlets are stored in world space
         * so the cull shader doesn't have to multiply by a per-instance
         * matrix.
         */
        RUNTIME_API void Build(STerrainComponent& Terrain, const glm::vec3& WorldOrigin);
    }
}
