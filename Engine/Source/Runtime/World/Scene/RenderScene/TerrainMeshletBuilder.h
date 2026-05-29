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
     * Called on the game thread (Extract) when STerrainComponent::CPUState.bChunksDirty
     * is set: rebuild of the CPU metadata, which Extract then snapshots for the
     * render thread to upload alongside a freshly-sized indirect-args /
     * visible-meshlet buffer.
     */
    namespace TerrainMeshletBuilder
    {
        /**
         * Walks the heightmap, populates Terrain.CPUState.Chunks and
         * Terrain.CPUState.Meshlets from scratch. Bounds are computed in
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
        RUNTIME_API void Build(STerrainComponent& Terrain, const FVector3& WorldOrigin);

        /**
         * Partial rebuild: recomputes bounds only for the chunks overlapping the
         * inclusive heightmap sample rect [SampleMin, SampleMax], leaving the rest
         * untouched. Falls back to a full Build if the chunk/meshlet structure
         * doesn't match the current resolution. Use after a localized height edit.
         */
        RUNTIME_API void UpdateRegion(STerrainComponent& Terrain, const FVector3& WorldOrigin, const FIntVector2& SampleMin, const FIntVector2& SampleMax);
    }
}
