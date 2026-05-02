#include "pch.h"
#include "NavMesh.h"

#include "TaskSystem/TaskSystem.h"

// Recast/Detour vendoring is a follow-up step. Once
// Engine/Source/ThirdParty/Recast/ exists and is wired into premake, define
// LUMINA_HAS_RECAST in the Runtime module's PrivateDefines and the real
// implementation below switches on automatically. Until then, the runtime is
// a no-op shell so the rest of the engine can compile and the system can be
// scheduled and exercised end-to-end.
#if defined(LUMINA_HAS_RECAST)
    #include <DetourNavMesh.h>
    #include <DetourNavMeshQuery.h>
    #include <DetourCommon.h>
#endif

namespace Lumina
{
    namespace
    {
        FORCEINLINE void Pack(const glm::vec3& In, float* Out) { Out[0] = In.x; Out[1] = In.y; Out[2] = In.z; }
        FORCEINLINE glm::vec3 Unpack(const float* In) { return glm::vec3(In[0], In[1], In[2]); }

#if defined(LUMINA_HAS_RECAST)
        void ApplyFilter(const FNavQueryFilter& In, dtQueryFilter& Out)
        {
            Out.setIncludeFlags(In.IncludeFlags);
            Out.setExcludeFlags(In.ExcludeFlags);
            for (int32 i = 0; i < 64; ++i)
            {
                Out.setAreaCost(i, In.AreaCost[i]);
            }
        }
#endif
    }

    FNavMesh::FNavMesh() = default;

    FNavMesh::~FNavMesh()
    {
        Shutdown();
    }

    bool FNavMesh::Initialize(const glm::vec3& InOrigin, float InTileWorldSize, int32 MaxTiles, int32 MaxPolysPerTile, TVector<FNavTileData>&& Tiles)
    {
        Shutdown();

        Origin = InOrigin;
        TileWorldSize = InTileWorldSize;

#if defined(LUMINA_HAS_RECAST)
        NavMesh = dtAllocNavMesh();
        if (!NavMesh)
        {
            return false;
        }

        dtNavMeshParams Params{};
        Pack(Origin, Params.orig);
        Params.tileWidth  = TileWorldSize;
        Params.tileHeight = TileWorldSize;
        Params.maxTiles   = MaxTiles;
        Params.maxPolys   = MaxPolysPerTile;

        if (dtStatusFailed(NavMesh->init(&Params)))
        {
            dtFreeNavMesh(NavMesh);
            NavMesh = nullptr;
            return false;
        }

        // dtNavMesh::addTile is not thread-safe so tiles are added serially.
        // The expensive work (voxelization etc.) happened during the bake.
        for (FNavTileData& Tile : Tiles)
        {
            if (Tile.Blob.empty())
            {
                continue;
            }

            const size_t Size = Tile.Blob.size();
            uint8* Owned = (uint8*)dtAlloc((int)Size, DT_ALLOC_PERM);
            if (!Owned)
            {
                continue;
            }
            memcpy(Owned, Tile.Blob.data(), Size);

            dtTileRef Ref = 0;
            const dtStatus Status = NavMesh->addTile(Owned, (int)Size, DT_TILE_FREE_DATA, 0, &Ref);
            if (dtStatusFailed(Status))
            {
                dtFree(Owned);
            }
        }

        // Slightly over-provision so worker contention rarely blocks. Each
        // dtNavMeshQuery is a few hundred KB at 2048 nodes, so this is cheap
        // for typical worker counts.
        const uint32 PoolSize = (GTaskSystem ? GTaskSystem->GetNumWorkers() : 4u) + 2u;
        QueryPool = TVector<FQuerySlot>(PoolSize);
        for (uint32 i = 0; i < PoolSize; ++i)
        {
            dtNavMeshQuery* Query = dtAllocNavMeshQuery();
            if (Query && dtStatusSucceed(Query->init(NavMesh, 2048)))
            {
                QueryPool[i].Query = Query;
            }
            else if (Query)
            {
                dtFreeNavMeshQuery(Query);
            }
        }

        bReady = true;
        // Build the triangle cache here on the same thread that just
        // populated the dtNavMesh; safe and amortized inside Initialize.
        RefreshTriangleCache();
        return true;
#else
        (void)MaxTiles; (void)MaxPolysPerTile; (void)Tiles;
        bReady = false;
        return false;
#endif
    }

    void FNavMesh::Shutdown()
    {
#if defined(LUMINA_HAS_RECAST)
        for (FQuerySlot& Slot : QueryPool)
        {
            if (Slot.Query) dtFreeNavMeshQuery(Slot.Query);
        }
        if (NavMesh) dtFreeNavMesh(NavMesh);
#endif
        QueryPool.clear();
        NavMesh = nullptr;
        bReady = false;
    }

    FNavMesh::FAcquiredQuery FNavMesh::AcquireQuery() const
    {
        // Linear CAS scan. With PoolSize > NumWorkers the first sweep almost
        // always finds a free slot; the spin loop is a safety net for
        // pathological contention from many ad-hoc threads.
        const uint32 N = (uint32)QueryPool.size();
        if (N == 0) return {};

        for (int32 Attempt = 0; Attempt < 8; ++Attempt)
        {
            for (uint32 i = 0; i < N; ++i)
            {
                if (!QueryPool[i].Query) continue;
                bool Expected = false;
                if (QueryPool[i].Busy.compare_exchange_strong(Expected, true, std::memory_order_acq_rel))
                {
                    return FAcquiredQuery{ &QueryPool[i] };
                }
            }
            std::this_thread::yield();
        }
        return {};
    }

    bool FNavMesh::ProjectPoint(const glm::vec3& World, const glm::vec3& Extents, const FNavQueryFilter& Filter, glm::vec3& Out) const
    {
#if defined(LUMINA_HAS_RECAST)
        FAcquiredQuery Q = AcquireQuery();
        if (!Q) return false;

        dtQueryFilter F; ApplyFilter(Filter, F);
        float P[3]; Pack(World, P);
        float E[3]; Pack(Extents, E);
        float Nearest[3];
        dtPolyRef Ref = 0;
        if (dtStatusFailed(Q.Get()->findNearestPoly(P, E, &F, &Ref, Nearest)) || Ref == 0)
        {
            return false;
        }
        Out = Unpack(Nearest);
        return true;
#else
        (void)World; (void)Extents; (void)Filter; (void)Out;
        return false;
#endif
    }

    bool FNavMesh::FindPath(const glm::vec3& Start, const glm::vec3& End, const FNavQueryFilter& Filter, FNavPath& Out) const
    {
        Out = {};

#if defined(LUMINA_HAS_RECAST)
        FAcquiredQuery Q = AcquireQuery();
        if (!Q) return false;

        dtQueryFilter F; ApplyFilter(Filter, F);

        float SP[3]; Pack(Start, SP);
        float EP[3]; Pack(End,   EP);
        const float Extents[3] = { 2.0f, 4.0f, 2.0f };

        dtPolyRef SRef = 0, ERef = 0;
        float SNear[3], ENear[3];
        if (dtStatusFailed(Q.Get()->findNearestPoly(SP, Extents, &F, &SRef, SNear)) || SRef == 0) return false;
        if (dtStatusFailed(Q.Get()->findNearestPoly(EP, Extents, &F, &ERef, ENear)) || ERef == 0) return false;

        constexpr int32 MaxPolys = 256;
        dtPolyRef Path[MaxPolys];
        int32 PathLen = 0;
        const dtStatus PathStatus = Q.Get()->findPath(SRef, ERef, SNear, ENear, &F, Path, &PathLen, MaxPolys);
        if (dtStatusFailed(PathStatus) || PathLen == 0) return false;

        Out.bPartial = (PathStatus & DT_PARTIAL_RESULT) != 0;

        constexpr int32 MaxStraight = 256;
        float StraightPath[MaxStraight * 3];
        uint8 StraightFlags[MaxStraight];
        dtPolyRef StraightRefs[MaxStraight];
        int32 StraightCount = 0;
        if (dtStatusFailed(Q.Get()->findStraightPath(SNear, ENear, Path, PathLen, StraightPath, StraightFlags, StraightRefs, &StraightCount, MaxStraight)))
        {
            return false;
        }

        Out.Corners.reserve(StraightCount);
        for (int32 i = 0; i < StraightCount; ++i)
        {
            Out.Corners.push_back(Unpack(&StraightPath[i * 3]));
        }
        Out.bValid = true;
        return true;
#else
        (void)Start; (void)End; (void)Filter;
        return false;
#endif
    }

    void FNavMesh::RefreshTriangleCache()
    {
        CachedTriVerts.clear();
        CachedTriAreas.clear();

#if defined(LUMINA_HAS_RECAST)
        if (!NavMesh) return;

        const dtNavMesh* CMesh = NavMesh;
        const int32 MaxTiles = CMesh->getMaxTiles();

        // Pre-size conservatively so the inner loop doesn't reallocate.
        // 64 detail tris per poly is a high estimate; over-reserving is
        // cheap, under-reserving is multiple reallocs.
        size_t TriEstimate = 0;
        for (int32 i = 0; i < MaxTiles; ++i)
        {
            const dtMeshTile* Tile = CMesh->getTile(i);
            if (Tile && Tile->header) TriEstimate += (size_t)Tile->header->detailTriCount;
        }
        CachedTriVerts.reserve(TriEstimate * 3);
        CachedTriAreas.reserve(TriEstimate);

        for (int32 i = 0; i < MaxTiles; ++i)
        {
            const dtMeshTile* Tile = CMesh->getTile(i);
            if (!Tile || !Tile->header) continue;

            for (int p = 0; p < Tile->header->polyCount; ++p)
            {
                const dtPoly& Poly = Tile->polys[p];
                if (Poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) continue;

                const dtPolyDetail& Detail = Tile->detailMeshes[p];
                const uint8 Area = Poly.getArea();
                for (int t = 0; t < Detail.triCount; ++t)
                {
                    const uint8* Tri = &Tile->detailTris[(Detail.triBase + t) * 4];
                    for (int k = 0; k < 3; ++k)
                    {
                        const uint8 Idx = Tri[k];
                        const float* P = (Idx < Poly.vertCount)
                            ? &Tile->verts[Poly.verts[Idx] * 3]
                            : &Tile->detailVerts[(Detail.vertBase + (Idx - Poly.vertCount)) * 3];
                        CachedTriVerts.emplace_back(P[0], P[1], P[2]);
                    }
                    CachedTriAreas.push_back(Area);
                }
            }
        }
#endif
    }

    void FNavMesh::ForEachTriangle(FTriangleVisitor Visitor) const
    {
        const size_t NumTris = CachedTriAreas.size();
        for (size_t i = 0; i < NumTris; ++i)
        {
            Visitor(CachedTriVerts[i * 3 + 0], CachedTriVerts[i * 3 + 1], CachedTriVerts[i * 3 + 2], CachedTriAreas[i]);
        }
    }

    bool FNavMesh::RebuildTile(int32 TileX, int32 TileY, TVector<uint8>&& NewBlob)
    {
        LUMINA_PROFILE_SCOPE();
        
#if defined(LUMINA_HAS_RECAST)
        if (!NavMesh)
        {
            return false;
        }

        // Drop the existing tile at this slot. It's fine if there isn't one.
        const dtTileRef OldRef = NavMesh->getTileRefAt(TileX, TileY, 0);
        if (OldRef != 0)
        {
            NavMesh->removeTile(OldRef, nullptr, nullptr);
        }

        if (NewBlob.empty())
        {
            return true; // tile is now (intentionally) empty
        }

        const size_t Size = NewBlob.size();
        uint8* Owned = (uint8*)dtAlloc((int)Size, DT_ALLOC_PERM);
        if (!Owned)
        {
            return false;
        }
        memcpy(Owned, NewBlob.data(), Size);

        dtTileRef NewRef = 0;
        if (dtStatusFailed(NavMesh->addTile(Owned, (int)Size, DT_TILE_FREE_DATA, 0, &NewRef)))
        {
            dtFree(Owned);
            return false;
        }
        // Cache stays valid for unchanged tiles, but the rebuilt one's
        // triangles are stale. A full refresh keeps the cache simple; the
        // walk is a few hundred µs even for medium meshes.
        RefreshTriangleCache();
        return true;
#else
        (void)TileX; (void)TileY; (void)NewBlob;
        return false;
#endif
    }

    bool FNavMesh::FindRandomPoint(const glm::vec3& Center, float Radius, const FNavQueryFilter& Filter, glm::vec3& Out) const
    {
#if defined(LUMINA_HAS_RECAST)
        FAcquiredQuery Q = AcquireQuery();
        if (!Q) return false;

        dtQueryFilter F; ApplyFilter(Filter, F);

        // Locate the polygon closest to Center to seed the random walk.
        // Without a valid start ref the disk-walk has nothing to anchor to.
        float CP[3]; Pack(Center, CP);
        const float Extents[3] = { 2.0f, 4.0f, 2.0f };

        dtPolyRef StartRef = 0;
        float Snapped[3];
        if (dtStatusFailed(Q.Get()->findNearestPoly(CP, Extents, &F, &StartRef, Snapped)) || StartRef == 0)
        {
            return false;
        }

        // Detour drives sampling through a 0..1 RNG. xorshift gives us a
        // cheap thread-safe stream without dragging std::random into the
        // query path.
        static thread_local uint32 RngState = 0xDEADBEEF;
        auto Rand01 = []() -> float
        {
            uint32 X = RngState ? RngState : 1u;
            X ^= X << 13; X ^= X >> 17; X ^= X << 5;
            RngState = X;
            return (X & 0x00FFFFFFu) * (1.0f / 16777216.0f);
        };

        dtPolyRef RandomRef = 0;
        float RandomPt[3];
        if (dtStatusFailed(Q.Get()->findRandomPointAroundCircle(StartRef, Snapped, Radius, &F, Rand01, &RandomRef, RandomPt)) || RandomRef == 0)
        {
            return false;
        }
        Out = Unpack(RandomPt);
        return true;
#else
        (void)Center; (void)Radius; (void)Filter; (void)Out;
        return false;
#endif
    }

    bool FNavMesh::Raycast(const glm::vec3& Start, const glm::vec3& End, const FNavQueryFilter& Filter, glm::vec3& HitOut) const
    {
#if defined(LUMINA_HAS_RECAST)
        FAcquiredQuery Q = AcquireQuery();
        if (!Q) return false;

        dtQueryFilter F; ApplyFilter(Filter, F);
        float SP[3]; Pack(Start, SP);
        float EP[3]; Pack(End,   EP);
        const float Extents[3] = { 2.0f, 4.0f, 2.0f };

        dtPolyRef SRef = 0;
        float SNear[3];
        if (dtStatusFailed(Q.Get()->findNearestPoly(SP, Extents, &F, &SRef, SNear)) || SRef == 0) return false;

        float T = 0.0f;
        float Normal[3];
        dtPolyRef PathRefs[64];
        int32 PathCount = 0;
        if (dtStatusFailed(Q.Get()->raycast(SRef, SNear, EP, &F, &T, Normal, PathRefs, &PathCount, 64)))
        {
            return false;
        }
        HitOut = (T >= 1.0f) ? End : glm::mix(Start, End, T);
        return true;
#else
        (void)Start; (void)End; (void)Filter; (void)HitOut;
        return false;
#endif
    }
}
