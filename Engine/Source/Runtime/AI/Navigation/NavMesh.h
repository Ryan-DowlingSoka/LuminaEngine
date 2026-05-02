#pragma once

#include "AI/Navigation/NavTypes.h"

class dtNavMesh;
class dtNavMeshQuery;

namespace Lumina
{
    struct FNavTileData;

    /** Runtime navmesh: dtNavMesh + atomic-flag CAS query pool. Construct on main thread; query from any. */
    class RUNTIME_API FNavMesh
    {
    public:

        FNavMesh();
        ~FNavMesh();

        FNavMesh(const FNavMesh&) = delete;
        FNavMesh& operator=(const FNavMesh&) = delete;

        /** Tiles is consumed; each blob is copied into Detour memory. */
        bool Initialize(const glm::vec3& Origin, float TileWorldSize, int32 MaxTiles, int32 MaxPolysPerTile, TVector<FNavTileData>&& Tiles);

        void Shutdown();

        bool IsReady() const { return bReady; }

        bool ProjectPoint(const glm::vec3& World, const glm::vec3& Extents, const FNavQueryFilter& Filter, glm::vec3& Out) const;
        bool FindPath(const glm::vec3& Start, const glm::vec3& End, const FNavQueryFilter& Filter, FNavPath& Out) const;
        bool Raycast(const glm::vec3& Start, const glm::vec3& End, const FNavQueryFilter& Filter, glm::vec3& HitOut) const;

        bool FindRandomPoint(const glm::vec3& Center, float Radius, const FNavQueryFilter& Filter, glm::vec3& Out) const;

        /** NewBlob is consumed; empty removes the tile. Caller must serialize against in-flight queries on this tile. */
        bool RebuildTile(int32 TileX, int32 TileY, TVector<uint8>&& NewBlob);

        /** Iterates the cached flat triangle list (skip the dtNavMesh traversal cost). */
        using FTriangleVisitor = TMoveOnlyFunction<void(const glm::vec3&, const glm::vec3&, const glm::vec3&, uint8)>;
        void ForEachTriangle(FTriangleVisitor Visitor) const;

        /** Visitor MUST be thread-safe; invoked concurrently. */
        using FParallelTriangleVisitor = TFunction<void(const glm::vec3&, const glm::vec3&, const glm::vec3&, uint8)>;
        void ParallelForEachTriangle(const FParallelTriangleVisitor& Visitor) const;

        /** Auto-called by Initialize and RebuildTile. Read-only on dtNavMesh. */
        void RefreshTriangleCache();

    private:

        struct FQuerySlot
        {
            dtNavMeshQuery*     Query = nullptr;
            std::atomic<bool>   Busy{ false };
        };

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

        // Mutable so const query API can flip Busy flags.
        mutable TVector<FQuerySlot>         QueryPool;

        // Flat cache: 3 vec3 per tri in Verts; 1 area byte per tri.
        TVector<glm::vec3>                  CachedTriVerts;
        TVector<uint8>                      CachedTriAreas;

        glm::vec3                           Origin = glm::vec3(0.0f);
        float                               TileWorldSize = 0.0f;
        bool                                bReady = false;
    };
}
