#pragma once

#include "AI/Navigation/NavTypes.h"
#include "Memory/SmartPtr.h"

namespace Lumina
{
    /**
     * Snapshot of source triangle data fed into the bake. Triangles are in
     * world space, indexed by Indices (3 per triangle). Areas, when supplied,
     * is one ENavArea per triangle; if empty all triangles take Ground.
     *
     * The system module is responsible for filling this from world entities
     * (static meshes, terrain, nav modifiers). The builder treats it as
     * opaque and never touches the entity registry.
     */
    struct FNavBuildInput
    {
        TVector<glm::vec3>      Vertices;
        TVector<uint32>         Indices;
        TVector<uint8>          Areas;          // optional, size = NumTris

        // World AABB enclosing all bake-relevant geometry. Tiles are laid out
        // on a grid spanning this volume.
        glm::vec3               BoundsMin = glm::vec3( FLT_MAX);
        glm::vec3               BoundsMax = glm::vec3(-FLT_MAX);

        FNavBuildSettings       Settings;
    };

    /** Output of a successful bake; passed straight into FNavMesh::Initialize. */
    struct FNavBuildOutput
    {
        glm::vec3               Origin = glm::vec3(0.0f);
        float                   TileWorldSize = 0.0f;
        int32                   MaxTiles = 0;
        int32                   MaxPolysPerTile = 0;
        TVector<FNavTileData>   Tiles;
    };

    /**
     * Async bake handle. Bake() returns one immediately and the caller polls
     * IsDone or hands it to FNavMeshSystem to consume. Cancellation is
     * cooperative; in-flight tile tasks may still run to completion.
     */
    struct FNavBakeHandle
    {
        std::atomic<uint32>     TilesCompleted{ 0 };
        uint32                  TilesScheduled = 0;
        std::atomic<bool>       bDone{ false };
        std::atomic<bool>       bCancelRequested{ false };
        FNavBuildOutput         Output;

        float Progress() const
        {
            if (TilesScheduled == 0) return 1.0f;
            return (float)TilesCompleted.load(std::memory_order_acquire) / (float)TilesScheduled;
        }
    };

    namespace NavMeshBuilder
    {
        /**
         * Tile-parallel bake. Returns a heap-allocated handle the caller owns;
         * tasks run on the engine task system and write into Handle->Output.
         * When Handle->bDone flips true, Output is safe to consume.
         *
         * Calling thread is not blocked. Cancel by setting Handle->bCancelRequested.
         */
        RUNTIME_API TUniquePtr<FNavBakeHandle> Bake(FNavBuildInput Input);

        /**
         * Synchronous variant - blocks until done. Useful for editor "rebuild
         * now" flows and for tests. Internally still parallelizes tile work.
         */
        RUNTIME_API bool BakeSync(FNavBuildInput Input, FNavBuildOutput& Out);

        /**
         * Bake a single (TX, TY) tile against the layout of an existing bake.
         * Used by dynamic rebuild: when an entity moves, the system computes
         * the affected tile rect and rebakes only those tiles, then hot-swaps
         * each result into the live dtNavMesh.
         *
         * BaseLayout supplies the grid origin / tile size so the new tile
         * aligns exactly with the existing dtNavMesh's tile slots.
         */
        RUNTIME_API bool BakeSingleTile(const FNavBuildInput& Input, const FNavBuildOutput& BaseLayout, int32 TX, int32 TY, FNavTileData& Out);
    }
}
