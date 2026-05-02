#pragma once

#include "AI/Navigation/NavTypes.h"

class dtNavMesh;
class dtNavMeshQuery;

namespace Lumina
{
    struct FNavTileData;

    /**
     * Runtime navmesh: thin opaque wrapper around dtNavMesh plus a small pool
     * of dtNavMeshQuery instances acquired with an atomic-flag CAS so any
     * thread can issue queries lock-free under typical contention.
     *
     * Owned 1:1 by SNavMeshComponent. Rebuilt from FNavTileData blobs on world
     * startup; not serialized. Construct on the main thread; query from any.
     */
    class RUNTIME_API FNavMesh
    {
    public:

        FNavMesh();
        ~FNavMesh();

        FNavMesh(const FNavMesh&) = delete;
        FNavMesh& operator=(const FNavMesh&) = delete;

        /**
         * Build a fresh dtNavMesh from baked tile blobs and pre-allocate the
         * query pool. Tiles is consumed: each blob is copied into Detour-owned
         * memory, so the input vector can be freed after this call returns.
         */
        bool Initialize(const glm::vec3& Origin, float TileWorldSize, int32 MaxTiles, int32 MaxPolysPerTile, TVector<FNavTileData>&& Tiles);

        void Shutdown();

        bool IsReady() const { return bReady; }

        bool ProjectPoint(const glm::vec3& World, const glm::vec3& Extents, const FNavQueryFilter& Filter, glm::vec3& Out) const;
        bool FindPath(const glm::vec3& Start, const glm::vec3& End, const FNavQueryFilter& Filter, FNavPath& Out) const;
        bool Raycast(const glm::vec3& Start, const glm::vec3& End, const FNavQueryFilter& Filter, glm::vec3& HitOut) const;

        /**
         * Pick a random walkable point inside the disk of (Center, Radius)
         * that's reachable from Center along the navmesh. Used by AI
         * wander behaviors and patrol-point sampling.
         */
        bool FindRandomPoint(const glm::vec3& Center, float Radius, const FNavQueryFilter& Filter, glm::vec3& Out) const;

        /**
         * Replace a single (TileX, TileY) tile. Used by dynamic rebuild after
         * a per-tile rebake completes. NewBlob is consumed; if empty, the
         * existing tile is just removed (covers fully-non-walkable regions).
         *
         * Caller must serialize this against in-flight queries on the same
         * tile - typically by running it on the main update tick where the
         * rest of the navmesh state changes happen.
         */
        bool RebuildTile(int32 TileX, int32 TileY, TVector<uint8>&& NewBlob);

        /**
         * Walk every walkable triangle in the navmesh. Iterates a flat
         * cache built from the dtNavMesh detail mesh (refreshed by
         * Initialize / RebuildTile), not the dtNavMesh itself - main
         * thread debug draw doesn't pay the per-frame poly traversal.
         */
        using FTriangleVisitor = TMoveOnlyFunction<void(const glm::vec3&, const glm::vec3&, const glm::vec3&, uint8)>;
        void ForEachTriangle(FTriangleVisitor Visitor) const;

        /**
         * Rebuild the triangle cache from the live dtNavMesh. Called
         * automatically at the end of Initialize and RebuildTile.
         * Read-only on dtNavMesh so it's safe to call from a worker
         * provided no other thread is mutating the same tile.
         */
        void RefreshTriangleCache();

    private:

        struct FQuerySlot
        {
            dtNavMeshQuery*     Query = nullptr;
            std::atomic<bool>   Busy{ false };
        };

        // RAII handle that releases its slot on destruction.
        struct FAcquiredQuery
        {
            FQuerySlot* Slot = nullptr;
            ~FAcquiredQuery() { if (Slot) Slot->Busy.store(false, std::memory_order_release); }
            dtNavMeshQuery* Get() const { return Slot ? Slot->Query : nullptr; }
            explicit operator bool() const { return Slot != nullptr; }
        };

        FAcquiredQuery AcquireQuery() const;

    private:

        dtNavMesh*                          NavMesh = nullptr;

        // Mutable so the const query API can flip Busy flags. The
        // observable result of a query is invariant under acquire/release
        // bookkeeping, so the const-correctness story holds.
        mutable TVector<FQuerySlot>         QueryPool;

        /**
         * Flat cache of every walkable triangle in world space, refreshed
         * after Initialize and RebuildTile. ForEachTriangle iterates this
         * directly so per-frame debug draw doesn't pay the dtNavMesh
         * traversal cost (which is non-trivial on big meshes).
         *
         * Layout: 3 vec3 per triangle in Verts; 1 area byte per triangle
         * in Areas. Sized so Verts.size() == 3 * Areas.size().
         */
        TVector<glm::vec3>                  CachedTriVerts;
        TVector<uint8>                      CachedTriAreas;

        glm::vec3                           Origin = glm::vec3(0.0f);
        float                               TileWorldSize = 0.0f;
        bool                                bReady = false;
    };
}
