#pragma once

#include "AI/Navigation/NavMesh.h"
#include "AI/Navigation/NavMeshBuilder.h"
#include "AI/Navigation/NavTypes.h"
#include "Memory/SmartPtr.h"
#include "NavMeshComponent.generated.h"

namespace Lumina
{

    /**
     * Transient runtime state for a navmesh instance. Mirrors
     * FTerrainGPUState's role: rebuilt from serialized data on world startup,
     * never serialized itself.
     *
     * Holds the live FNavMesh (Detour-backed query target) plus the in-flight
     * bake handle, if any. Both are unique_ptrs so the component remains
     * cheaply copyable for editor undo/redo (deep copies reset transient
     * state, see SNavMeshComponent's copy ctor).
     */
    /**
     * Snapshot of one nav-relevant entity used to detect "moved since last
     * tick." Cached on the component, keyed by entt::entity. When the live
     * AABB diverges from LastAABB, the union of the old and new AABB's tile
     * coverage is marked dirty for incremental rebake.
     */
    struct FNavSourceEntity
    {
        glm::vec3   AABBMin = glm::vec3( FLT_MAX);
        glm::vec3   AABBMax = glm::vec3(-FLT_MAX);
    };

    /** Per-tile rebake task in flight. One slot per dirty tile being processed. */
    struct FNavTileRebake
    {
        int32                       TileX = 0;
        int32                       TileY = 0;
        TVector<uint8>              ResultBlob;
        std::atomic<bool>           bDone{ false };
        std::atomic<bool>           bConsumed{ false };
    };

    /**
     * Off-main initialization job. The bake produces tile blobs, but
     * dtNavMesh::init + per-tile addTile + per-worker dtNavMeshQuery
     * allocation are still expensive (ms-level for moderate bakes).
     * That work happens here on a worker; main thread just consumes
     * the resulting FNavMesh when bDone flips.
     */
    struct FNavInitJob
    {
        TUniquePtr<FNavMesh>        ResultMesh;
        std::atomic<bool>           bDone{ false };
    };

    struct FNavMeshRuntime
    {
        TUniquePtr<FNavMesh>            Mesh;
        TUniquePtr<FNavBakeHandle>      ActiveBake;

        /**
         * Async hydration job. While set, an FNavMesh is being built off
         * main; once bDone flips, the system consumes ResultMesh into Mesh
         * and transitions State from Initializing to Ready.
         */
        TSharedPtr<FNavInitJob>         PendingInit;

        ENavBakeState                   State = ENavBakeState::Idle;

        bool                            bRuntimeDirty = true;   // Tiles changed; rebuild FNavMesh next tick.

        // Dynamic-rebuild state. Populated lazily on the first tick after a
        // full bake completes; never serialized.

        /** Cached per-source-entity AABBs from the previous tick. */
        THashMap<uint32, FNavSourceEntity>      EntityAABBs;

        /** Tile coords (packed as TY*TilesX + TX) waiting to be rebuilt. */
        THashSet<uint64>                        DirtyTiles;

        /**
         * Per-tile rebake jobs currently running on workers. Shared_ptr
         * because the async coordinator holds its own reference - Teardown
         * clearing this vector cannot dangle an in-flight worker.
         */
        TVector<TSharedPtr<FNavTileRebake>>     PendingRebakes;

        /** Cached layout fed to BakeSingleTile so coords align with the live mesh. */
        FNavBuildOutput                         LiveLayout;

        /** Tile grid extent at bake time, used to pack/unpack DirtyTiles keys. */
        int32                                   TilesX = 0;
        int32                                   TilesY = 0;
    };

    /**
     * Volume defining where the bake gathers source geometry. World-space
     * AABB centered on Center with half-extents Extents. Multiple components
     * per world are unioned at bake time.
     */
    REFLECT(Component)
    struct RUNTIME_API SNavMeshComponent
    {
        GENERATED_BODY()

        SNavMeshComponent() = default;

        SNavMeshComponent(const SNavMeshComponent& Other)
            : Settings(Other.Settings)
            , Center(Other.Center)
            , Extents(Other.Extents)
            , bDrawDebug(Other.bDrawDebug)
            , Tiles(Other.Tiles)
            , Origin(Other.Origin)
            , TileWorldSize(Other.TileWorldSize)
            , MaxPolysPerTile(Other.MaxPolysPerTile)
        {
        }

        SNavMeshComponent& operator=(const SNavMeshComponent& Other)
        {
            if (this != &Other)
            {
                Settings        = Other.Settings;
                Center          = Other.Center;
                Extents         = Other.Extents;
                Tiles           = Other.Tiles;
                Origin          = Other.Origin;
                TileWorldSize   = Other.TileWorldSize;
                MaxPolysPerTile = Other.MaxPolysPerTile;
                bDrawDebug      = Other.bDrawDebug;
                Runtime         = FNavMeshRuntime{}; // transient, reset on copy
            }
            return *this;
        }

        SNavMeshComponent(SNavMeshComponent&&) noexcept            = default;
        SNavMeshComponent& operator=(SNavMeshComponent&&) noexcept = default;

        /** Voxelization, agent, and tiling parameters fed to Recast. */
        PROPERTY(Editable, Category = "NavMesh|Build")
        FNavBuildSettings Settings;

        /** World-space center of the bake volume. */
        PROPERTY(Editable, Category = "NavMesh|Bounds")
        glm::vec3 Center = glm::vec3(0.0f);

        /** Half-extents of the bake volume. */
        PROPERTY(Editable, Category = "NavMesh|Bounds")
        glm::vec3 Extents = glm::vec3(64.0f, 16.0f, 64.0f);

        /** When true, the system emits debug lines for every walkable triangle each tick. */
        PROPERTY(Editable, Category = "NavMesh|Debug")
        bool bDrawDebug = false;

        /**
         * Set by the editor "Bake" button (or gameplay code) and consumed by
         * SNavMeshSystem on the next tick, which dispatches the actual
         * RequestBake. Avoids exposing FSystemContext to the editor.
         */
        bool bBakeRequested = false;

        /**
         * Baked tile blobs. One per tile in the grid; empty blobs represent
         * fully-non-walkable tiles. Serialized with the world.
         */
        PROPERTY(Category = "NavMesh|Baked")
        TVector<FNavTileData> Tiles;

        /** Bake origin (== bounds min). Persisted so runtime init matches. */
        PROPERTY(Category = "NavMesh|Baked")
        glm::vec3 Origin = glm::vec3(0.0f);

        /** Tile size in world units (== TileSizeVoxels * CellSize at bake time). */
        PROPERTY(Category = "NavMesh|Baked")
        float TileWorldSize = 0.0f;

        /** Cap fed to dtNavMeshParams; oversize, costs memory not perf. */
        PROPERTY(Category = "NavMesh|Baked")
        int32 MaxPolysPerTile = 0;

        /** Live runtime state. Not serialized. */
        FNavMeshRuntime Runtime;

        bool HasBakedData() const { return !Tiles.empty() && TileWorldSize > 0.0f; }
    };
}
