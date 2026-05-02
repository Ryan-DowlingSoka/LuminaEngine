#include "pch.h"
#include "NavMeshBuilder.h"

#include "Memory/SmartPtr.h"
#include "TaskSystem/TaskSystem.h"

#if defined(LUMINA_HAS_RECAST)
    #include <Recast.h>
    #include <DetourNavMesh.h>
    #include <DetourNavMeshBuilder.h>
#endif

namespace Lumina::NavMeshBuilder
{
    namespace
    {
        struct FTileGrid
        {
            glm::vec3   Origin;             // BoundsMin
            float       TileWorldSize;      // == TileSizeVoxels * CellSize
            float       BorderSize;         // world units of expand-per-tile, for seam consistency
            int32       TilesX = 0;
            int32       TilesY = 0;         // Y in nav-grid sense = world Z
        };

        FTileGrid ComputeGrid(const FNavBuildInput& In)
        {
            FTileGrid Grid{};
            Grid.Origin = In.BoundsMin;
            Grid.TileWorldSize = (float)In.Settings.TileSizeVoxels * In.Settings.CellSize;
            // Recast needs each tile expanded by a few voxels of border so
            // rasterized triangles spanning a tile boundary contribute to
            // both. AgentRadius rounded up in voxels + 3 is the conventional
            // choice and what dtNavMeshBuilder expects to find.
            const int32 BorderVoxels = (int32)std::ceil(In.Settings.AgentRadius / In.Settings.CellSize) + 3;
            Grid.BorderSize = (float)BorderVoxels * In.Settings.CellSize;
            const glm::vec3 Span = In.BoundsMax - In.BoundsMin;
            Grid.TilesX = (int32)std::ceil(std::max(Span.x, 0.0f) / Grid.TileWorldSize);
            Grid.TilesY = (int32)std::ceil(std::max(Span.z, 0.0f) / Grid.TileWorldSize);
            Grid.TilesX = std::max(Grid.TilesX, 1);
            Grid.TilesY = std::max(Grid.TilesY, 1);
            return Grid;
        }

#if defined(LUMINA_HAS_RECAST)
        // Bake one tile in isolation. Pure function over (Input, Grid, X, Y) -
        // touches no shared state, so calling it concurrently from
        // Task::ParallelFor is safe.
        bool BakeTile(const FNavBuildInput& In, const FTileGrid& Grid, int32 TX, int32 TY, FNavTileData& Out)
        {
            const FNavBuildSettings& S = In.Settings;
            Out.X = TX;
            Out.Y = TY;
            Out.Blob.clear();

            // Tile world-space bounds (without border).
            const float TileMinX = Grid.Origin.x + (float)TX * Grid.TileWorldSize;
            const float TileMinZ = Grid.Origin.z + (float)TY * Grid.TileWorldSize;
            const float TileMaxX = TileMinX + Grid.TileWorldSize;
            const float TileMaxZ = TileMinZ + Grid.TileWorldSize;

            rcConfig Cfg{};
            Cfg.cs = S.CellSize;
            Cfg.ch = S.CellHeight;
            Cfg.walkableSlopeAngle = S.AgentMaxSlopeDeg;
            Cfg.walkableHeight     = (int)std::ceil(S.AgentHeight / S.CellHeight);
            Cfg.walkableClimb      = (int)std::floor(S.AgentMaxClimb / S.CellHeight);
            Cfg.walkableRadius     = (int)std::ceil(S.AgentRadius / S.CellSize);
            Cfg.maxEdgeLen         = (int)(S.EdgeMaxLength / S.CellSize);
            Cfg.maxSimplificationError = S.EdgeMaxError;
            Cfg.minRegionArea      = S.RegionMinSize * S.RegionMinSize;
            Cfg.mergeRegionArea    = S.RegionMergeSize * S.RegionMergeSize;
            Cfg.maxVertsPerPoly    = S.VertsPerPoly;
            Cfg.detailSampleDist   = S.DetailSampleDist < 0.9f ? 0.0f : S.CellSize * S.DetailSampleDist;
            Cfg.detailSampleMaxError = S.CellHeight * S.DetailSampleMaxError;
            Cfg.tileSize           = S.TileSizeVoxels;
            Cfg.borderSize         = Cfg.walkableRadius + 3;
            Cfg.width              = Cfg.tileSize + Cfg.borderSize * 2;
            Cfg.height             = Cfg.tileSize + Cfg.borderSize * 2;

            // bmin/bmax expanded by border so rasterization sees triangles
            // that span the seam; rcBuildPolyMesh strips border polys later.
            Cfg.bmin[0] = TileMinX - Grid.BorderSize;
            Cfg.bmin[1] = In.BoundsMin.y;
            Cfg.bmin[2] = TileMinZ - Grid.BorderSize;
            Cfg.bmax[0] = TileMaxX + Grid.BorderSize;
            Cfg.bmax[1] = In.BoundsMax.y;
            Cfg.bmax[2] = TileMaxZ + Grid.BorderSize;

            rcContext Ctx(false);

            rcHeightfield* Solid = rcAllocHeightfield();
            if (!Solid) return false;
            if (!rcCreateHeightfield(&Ctx, *Solid, Cfg.width, Cfg.height, Cfg.bmin, Cfg.bmax, Cfg.cs, Cfg.ch))
            {
                rcFreeHeightField(Solid);
                return false;
            }

            // Scratch arrays scoped to this tile.
            const int32 NumTris = (int32)(In.Indices.size() / 3);
            TVector<uint8> TriAreas(NumTris, 0);

            // Mark walkable triangles by slope, then rasterize. We rasterize
            // every input triangle - rcRasterizeTriangles internally clips
            // against bmin/bmax, so passing the tile bbox limits work to
            // tile-relevant geometry without a manual cull.
            const float* Verts = reinterpret_cast<const float*>(In.Vertices.data());
            const int32  NumVerts = (int32)In.Vertices.size();
            const int*   Tris  = reinterpret_cast<const int*>(In.Indices.data());

            rcMarkWalkableTriangles(&Ctx, Cfg.walkableSlopeAngle, Verts, NumVerts, Tris, NumTris, TriAreas.data());

            // Override areas if the caller supplied per-triangle area ids
            // (used by Nav modifiers stamping water/door/danger).
            if (!In.Areas.empty() && (int32)In.Areas.size() == NumTris)
            {
                for (int32 i = 0; i < NumTris; ++i)
                {
                    if (TriAreas[i] != 0) // keep unwalkable as-is
                    {
                        TriAreas[i] = In.Areas[i];
                    }
                }
            }

            if (!rcRasterizeTriangles(&Ctx, Verts, NumVerts, Tris, TriAreas.data(), NumTris, *Solid, Cfg.walkableClimb))
            {
                rcFreeHeightField(Solid);
                return false;
            }

            rcFilterLowHangingWalkableObstacles(&Ctx, Cfg.walkableClimb, *Solid);
            rcFilterLedgeSpans(&Ctx, Cfg.walkableHeight, Cfg.walkableClimb, *Solid);
            rcFilterWalkableLowHeightSpans(&Ctx, Cfg.walkableHeight, *Solid);

            rcCompactHeightfield* Compact = rcAllocCompactHeightfield();
            if (!Compact)
            {
                rcFreeHeightField(Solid);
                return false;
            }
            if (!rcBuildCompactHeightfield(&Ctx, Cfg.walkableHeight, Cfg.walkableClimb, *Solid, *Compact))
            {
                rcFreeHeightField(Solid);
                rcFreeCompactHeightfield(Compact);
                return false;
            }
            rcFreeHeightField(Solid);
            Solid = nullptr;

            if (!rcErodeWalkableArea(&Ctx, Cfg.walkableRadius, *Compact))
            {
                rcFreeCompactHeightfield(Compact);
                return false;
            }

            // Watershed regions - good polygonization, slightly slower than
            // monotone. Worth the cost; monotone produces long thin polys.
            if (!rcBuildDistanceField(&Ctx, *Compact) ||
                !rcBuildRegions(&Ctx, *Compact, Cfg.borderSize, Cfg.minRegionArea, Cfg.mergeRegionArea))
            {
                rcFreeCompactHeightfield(Compact);
                return false;
            }

            rcContourSet* CSet = rcAllocContourSet();
            if (!CSet ||
                !rcBuildContours(&Ctx, *Compact, Cfg.maxSimplificationError, Cfg.maxEdgeLen, *CSet))
            {
                rcFreeCompactHeightfield(Compact);
                if (CSet) rcFreeContourSet(CSet);
                return false;
            }
            if (CSet->nconts == 0)
            {
                // Empty tile is a valid result; no walkable surface here.
                rcFreeCompactHeightfield(Compact);
                rcFreeContourSet(CSet);
                return true;
            }

            rcPolyMesh* PMesh = rcAllocPolyMesh();
            if (!PMesh ||
                !rcBuildPolyMesh(&Ctx, *CSet, Cfg.maxVertsPerPoly, *PMesh))
            {
                rcFreeCompactHeightfield(Compact);
                rcFreeContourSet(CSet);
                if (PMesh) rcFreePolyMesh(PMesh);
                return false;
            }

            rcPolyMeshDetail* DMesh = rcAllocPolyMeshDetail();
            if (!DMesh ||
                !rcBuildPolyMeshDetail(&Ctx, *PMesh, *Compact, Cfg.detailSampleDist, Cfg.detailSampleMaxError, *DMesh))
            {
                rcFreeCompactHeightfield(Compact);
                rcFreeContourSet(CSet);
                rcFreePolyMesh(PMesh);
                if (DMesh)
                {
                    rcFreePolyMeshDetail(DMesh);
                }
                return false;
            }
            rcFreeCompactHeightfield(Compact);
            rcFreeContourSet(CSet);

            // Tag every poly with the Walk flag so default queries find them.
            // Area-specific flag tagging (door/danger) layers on top via Nav
            // modifiers when those land.
            for (int i = 0; i < PMesh->npolys; ++i)
            {
                if (PMesh->areas[i] == RC_WALKABLE_AREA)
                {
                    PMesh->areas[i] = (uint8)1; // ENavArea::Ground
                }
                if (PMesh->areas[i] != 0)
                {
                    PMesh->flags[i] = 1; // ENavPolyFlag::Walk
                }
            }

            dtNavMeshCreateParams Params{};
            Params.verts            = PMesh->verts;
            Params.vertCount        = PMesh->nverts;
            Params.polys            = PMesh->polys;
            Params.polyAreas        = PMesh->areas;
            Params.polyFlags        = PMesh->flags;
            Params.polyCount        = PMesh->npolys;
            Params.nvp              = PMesh->nvp;
            Params.detailMeshes     = DMesh->meshes;
            Params.detailVerts      = DMesh->verts;
            Params.detailVertsCount = DMesh->nverts;
            Params.detailTris       = DMesh->tris;
            Params.detailTriCount   = DMesh->ntris;
            Params.walkableHeight   = S.AgentHeight;
            Params.walkableRadius   = S.AgentRadius;
            Params.walkableClimb    = S.AgentMaxClimb;
            Params.tileX            = TX;
            Params.tileY            = TY;
            Params.tileLayer        = 0;
            rcVcopy(Params.bmin, PMesh->bmin);
            rcVcopy(Params.bmax, PMesh->bmax);
            Params.cs               = Cfg.cs;
            Params.ch               = Cfg.ch;
            Params.buildBvTree      = true;

            uint8* NavData = nullptr;
            int    NavDataSize = 0;
            const bool bCreated = dtCreateNavMeshData(&Params, &NavData, &NavDataSize);
            rcFreePolyMesh(PMesh);
            rcFreePolyMeshDetail(DMesh);

            if (!bCreated || !NavData)
            {
                return false;
            }

            // Copy out of Detour's allocator into our serialized blob; the
            // runtime makes its own copy at addTile so this can outlive both.
            Out.Blob.assign(NavData, NavData + NavDataSize);
            dtFree(NavData);
            return true;
        }
#endif
    }

    static void RunBake(const FNavBuildInput& Input, FNavBakeHandle& Handle)
    {
        const FTileGrid Grid = ComputeGrid(Input);
        const uint32 TileCount = (uint32)(Grid.TilesX * Grid.TilesY);

        Handle.Output.Origin = Grid.Origin;
        Handle.Output.TileWorldSize = Grid.TileWorldSize;
        Handle.Output.MaxTiles = (int32)TileCount;
        Handle.Output.MaxPolysPerTile = 1 << 14;
        Handle.Output.Tiles.resize(TileCount);
        Handle.TilesScheduled = TileCount;

        // Tiles are independent - each task voxelizes its tile against the
        // shared (read-only) Input snapshot and writes one slot in Tiles.
        Task::ParallelFor(TileCount, [&](uint32 Index)
        {
            if (Handle.bCancelRequested.load(std::memory_order_acquire))
            {
                Handle.TilesCompleted.fetch_add(1, std::memory_order_release);
                return;
            }

            const int32 TX = (int32)(Index % (uint32)Grid.TilesX);
            const int32 TY = (int32)(Index / (uint32)Grid.TilesX);

#if defined(LUMINA_HAS_RECAST)
            BakeTile(Input, Grid, TX, TY, Handle.Output.Tiles[Index]);
#else
            Handle.Output.Tiles[Index].X = TX;
            Handle.Output.Tiles[Index].Y = TY;
#endif
            Handle.TilesCompleted.fetch_add(1, std::memory_order_release);
        });

        Handle.bDone.store(true, std::memory_order_release);
    }

    TUniquePtr<FNavBakeHandle> Bake(FNavBuildInput Input)
    {
        TUniquePtr<FNavBakeHandle> Handle = MakeUnique<FNavBakeHandle>();
        FNavBakeHandle* Raw = Handle.get();

        Task::AsyncTask(1, 1, [Raw, In = std::move(Input)](uint32, uint32, uint32) mutable
        {
            RunBake(In, *Raw);
        });

        return Handle;
    }

    bool BakeSync(FNavBuildInput Input, FNavBuildOutput& Out)
    {
        FNavBakeHandle Handle;
        RunBake(Input, Handle);
        Out = std::move(Handle.Output);
        return Handle.bDone.load(std::memory_order_acquire);
    }

#if defined(LUMINA_HAS_RECAST)
    bool BakeSingleTile(const FNavBuildInput& Input, const FNavBuildOutput& BaseLayout, int32 TX, int32 TY, FNavTileData& Out)
    {
        // Reuse the same Recast pipeline but with the existing baked layout's
        // grid origin and tile size, so the new tile aligns to the live
        // dtNavMesh and hot-swap is byte-exact.
        FTileGrid Grid{};
        Grid.Origin = BaseLayout.Origin;
        Grid.TileWorldSize = BaseLayout.TileWorldSize;
        const int32 BorderVoxels = (int32)std::ceil(Input.Settings.AgentRadius / Input.Settings.CellSize) + 3;
        Grid.BorderSize = (float)BorderVoxels * Input.Settings.CellSize;
        Grid.TilesX = std::max(1, (int32)std::ceil((Input.BoundsMax.x - Input.BoundsMin.x) / Grid.TileWorldSize));
        Grid.TilesY = std::max(1, (int32)std::ceil((Input.BoundsMax.z - Input.BoundsMin.z) / Grid.TileWorldSize));
        return BakeTile(Input, Grid, TX, TY, Out);
    }
#else
    bool BakeSingleTile(const FNavBuildInput&, const FNavBuildOutput&, int32, int32, FNavTileData&)
    {
        return false;
    }
#endif
}
