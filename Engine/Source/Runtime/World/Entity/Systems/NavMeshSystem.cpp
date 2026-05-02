#include "pch.h"
#include "NavMeshSystem.h"

#include "AI/Navigation/NavMesh.h"
#include "AI/Navigation/NavMeshBuilder.h"
#include "Assets/AssetTypes/Mesh/StaticMesh/StaticMesh.h"
#include "Renderer/MeshData.h"
#include "Renderer/Vertex.h"
#include "TaskSystem/TaskSystem.h"
#include "World/Entity/Components/CharacterControllerComponent.h"
#include "World/Entity/Components/NavMeshComponent.h"
#include "World/Entity/Components/StaticMeshComponent.h"
#include "World/Entity/Components/TransformComponent.h"
#include "World/World.h"
#include "Scripting/Lua/Reference.h"
#include "Scripting/Lua/Class.h"

namespace Lumina
{
    namespace
    {
        struct FGatherAccumulator
        {
            TVector<glm::vec3> Vertices;
            TVector<uint32>    Indices;
            glm::vec3          AABBMin = glm::vec3( FLT_MAX);
            glm::vec3          AABBMax = glm::vec3(-FLT_MAX);
        };

        // Triangle indices in MeshletTriangles are 8-bit local indices packed
        // 3 per uint32. Each meshlet's vertex indices are local to its
        // VertexOffset in MeshletVertices.
        FORCEINLINE void UnpackTri(uint32 Packed, uint32& A, uint32& B, uint32& C)
        {
            A = (Packed >> 0)  & 0xFFu;
            B = (Packed >> 8)  & 0xFFu;
            C = (Packed >> 16) & 0xFFu;
        }

        // Position is 10-10-10 unsigned. Decode is MeshOrigin + (LoInt + q) * GridStep.
        FORCEINLINE glm::vec3 DecodePosition(uint32 Packed, const glm::ivec3& LoInt, const glm::vec3& MeshOrigin, const glm::vec3& GridStep)
        {
            const glm::ivec3 Q(
                (int32)((Packed >>  0) & 0x3FFu),
                (int32)((Packed >> 10) & 0x3FFu),
                (int32)((Packed >> 20) & 0x3FFu));
            return MeshOrigin + glm::vec3(LoInt + Q) * GridStep;
        }

        bool TriIntersectsAABB(const glm::vec3& A, const glm::vec3& B, const glm::vec3& C, const glm::vec3& BMin, const glm::vec3& BMax)
        {
            const glm::vec3 TMin = glm::min(glm::min(A, B), C);
            const glm::vec3 TMax = glm::max(glm::max(A, B), C);
            return !(TMax.x < BMin.x || TMin.x > BMax.x ||
                     TMax.y < BMin.y || TMin.y > BMax.y ||
                     TMax.z < BMin.z || TMin.z > BMax.z);
        }

        // Decode LOD0 meshlets of a static mesh, transform by entity's world
        // matrix, clip-test against the bake AABB, and append to the
        // accumulator. Per-mesh AABB is also reported back for change-detection.
        void DecodeMeshIntoAccumulator(CStaticMesh* Mesh, const glm::mat4& World, const glm::vec3& BakeMin, const glm::vec3& BakeMax, FGatherAccumulator& Acc, glm::vec3& OutAABBMin, glm::vec3& OutAABBMax)
        {
            if (!Mesh) return;
            const FMeshResource& Res = Mesh->GetMeshResource();
            const FMeshletData&  Md  = Res.MeshletData;
            if (Md.IsEmpty()) return;
            // Skinned meshes need bone-driven vertex positions; nav for those
            // is its own problem. Skip for v1 - matches Godot.
            if (Res.bSkinnedMesh) return;

            const TVector<FMeshletVertex>& MV = Md.MeshletVertices;
            const TVector<uint32>&         MT = Md.MeshletTriangles;

            OutAABBMin = glm::vec3( FLT_MAX);
            OutAABBMax = glm::vec3(-FLT_MAX);

            for (const FGeometrySurface& Surface : Res.GeometrySurfaces)
            {
                const uint32 First = Surface.LODMeshletOffset[0];
                const uint32 Count = Surface.LODMeshletCount[0];
                for (uint32 m = 0; m < Count; ++m)
                {
                    const FMeshlet& Meshlet = Md.Meshlets[First + m];

                    // Decode this meshlet's vertices into a local cache so
                    // each triangle only does the matrix multiply once.
                    glm::vec3 LocalVerts[MESHLET_MAX_VERTICES];
                    const uint32 V0 = Meshlet.VertexOffset;
                    for (uint32 v = 0; v < Meshlet.VertexCount; ++v)
                    {
                        const glm::vec3 Local = DecodePosition(MV[V0 + v].Position, Meshlet.LoInt, Md.MeshOrigin, Md.MeshGridStep);
                        LocalVerts[v] = glm::vec3(World * glm::vec4(Local, 1.0f));
                        OutAABBMin = glm::min(OutAABBMin, LocalVerts[v]);
                        OutAABBMax = glm::max(OutAABBMax, LocalVerts[v]);
                    }

                    // Walk triangle dwords. TriangleOffset is in dwords already.
                    const uint32 T0 = Meshlet.TriangleOffset;
                    for (uint32 t = 0; t < Meshlet.TriangleCount; ++t)
                    {
                        uint32 A, B, C;
                        UnpackTri(MT[T0 + t], A, B, C);
                        const glm::vec3& VA = LocalVerts[A];
                        const glm::vec3& VB = LocalVerts[B];
                        const glm::vec3& VC = LocalVerts[C];
                        if (!TriIntersectsAABB(VA, VB, VC, BakeMin, BakeMax))
                        {
                            continue;
                        }
                        const uint32 Base = (uint32)Acc.Vertices.size();
                        Acc.Vertices.push_back(VA);
                        Acc.Vertices.push_back(VB);
                        Acc.Vertices.push_back(VC);
                        Acc.Indices.push_back(Base + 0);
                        Acc.Indices.push_back(Base + 1);
                        Acc.Indices.push_back(Base + 2);
                        Acc.AABBMin = glm::min(Acc.AABBMin, glm::min(VA, glm::min(VB, VC)));
                        Acc.AABBMax = glm::max(Acc.AABBMax, glm::max(VA, glm::max(VB, VC)));
                    }
                }
            }
        }

        // One pass over the registry collecting nav source entities. Returns
        // the accumulated geometry plus a per-entity AABB map for change
        // detection. AABBs use the entity id key so the dynamic update tick
        // can compare against the previous frame's snapshot.
        void GatherSourceGeometry(const FSystemContext& Context, const glm::vec3& BakeMin, const glm::vec3& BakeMax, FGatherAccumulator& Acc, THashMap<uint32, FNavSourceEntity>& OutAABBs)
        {
            // Exclude path-following agents: their meshes move every frame,
            // and including them would carve the agent itself out of the
            // navmesh AND trigger a per-frame tile-rebake storm as they
            // walk. Characters are dynamic by definition; static obstacles
            // are everything that isn't one.
            auto View = Context.CreateView<SStaticMeshComponent, STransformComponent>(entt::exclude<SCharacterControllerComponent>);
            for (entt::entity Entity : View)
            {
                SStaticMeshComponent& MeshComp = View.get<SStaticMeshComponent>(Entity);
                STransformComponent&  XformC   = View.get<STransformComponent>(Entity);

                CStaticMesh* Mesh = MeshComp.GetStaticMesh();
                if (!Mesh) continue;

                glm::vec3 EntityMin, EntityMax;
                DecodeMeshIntoAccumulator(Mesh, XformC.GetWorldMatrix(), BakeMin, BakeMax, Acc, EntityMin, EntityMax);

                FNavSourceEntity Snap;
                Snap.AABBMin = EntityMin;
                Snap.AABBMax = EntityMax;
                OutAABBs[(uint32)Entity] = Snap;
            }
        }

        // Compute the inclusive (TX,TY) range of tiles covered by a world AABB.
        FORCEINLINE void TilesForAABB(const glm::vec3& AABBMin, const glm::vec3& AABBMax, const glm::vec3& Origin, float TileWorldSize, int32 TilesX, int32 TilesY,
                                      int32& OutTX0, int32& OutTY0, int32& OutTX1, int32& OutTY1)
        {
            OutTX0 = std::clamp((int32)std::floor((AABBMin.x - Origin.x) / TileWorldSize), 0, TilesX - 1);
            OutTY0 = std::clamp((int32)std::floor((AABBMin.z - Origin.z) / TileWorldSize), 0, TilesY - 1);
            OutTX1 = std::clamp((int32)std::floor((AABBMax.x - Origin.x) / TileWorldSize), 0, TilesX - 1);
            OutTY1 = std::clamp((int32)std::floor((AABBMax.z - Origin.z) / TileWorldSize), 0, TilesY - 1);
        }

        FORCEINLINE uint64 PackTileKey(int32 TX, int32 TY) { return ((uint64)(uint32)TY << 32) | (uint32)TX; }

        // Cheap conservative world-space AABB for one static-mesh entity.
        // Transforms the mesh's local AABB corners by the entity matrix.
        // Both the bake-completion cache rebuild and the change-detector
        // share this so byte-identical values flow through both paths.
        bool ComputeEntityAABB(SStaticMeshComponent& MeshComp, STransformComponent& XformC, glm::vec3& OutMin, glm::vec3& OutMax)
        {
            CStaticMesh* Mesh = MeshComp.GetStaticMesh();
            if (!Mesh || Mesh->GetMeshResource().bSkinnedMesh) return false;

            const FAABB& Local = Mesh->GetAABB();
            const glm::vec3 Corners[8] = {
                {Local.Min.x, Local.Min.y, Local.Min.z}, {Local.Max.x, Local.Min.y, Local.Min.z},
                {Local.Min.x, Local.Max.y, Local.Min.z}, {Local.Max.x, Local.Max.y, Local.Min.z},
                {Local.Min.x, Local.Min.y, Local.Max.z}, {Local.Max.x, Local.Min.y, Local.Max.z},
                {Local.Min.x, Local.Max.y, Local.Max.z}, {Local.Max.x, Local.Max.y, Local.Max.z},
            };
            const glm::mat4& W = XformC.GetWorldMatrix();
            OutMin = glm::vec3( FLT_MAX);
            OutMax = glm::vec3(-FLT_MAX);
            for (int i = 0; i < 8; ++i)
            {
                const glm::vec3 P = glm::vec3(W * glm::vec4(Corners[i], 1.0f));
                OutMin = glm::min(OutMin, P);
                OutMax = glm::max(OutMax, P);
            }
            return true;
        }

        // Snapshot every nav-source entity's conservative AABB into the cache.
        // Called at bake completion so the next change-detector tick compares
        // apples to apples and reports zero diff.
        void RebuildEntityAABBCache(const FSystemContext& Context, THashMap<uint32, FNavSourceEntity>& OutCache)
        {
            OutCache.clear();
            // Exclude path-following agents: their meshes move every frame,
            // and including them would carve the agent itself out of the
            // navmesh AND trigger a per-frame tile-rebake storm as they
            // walk. Characters are dynamic by definition; static obstacles
            // are everything that isn't one.
            auto View = Context.CreateView<SStaticMeshComponent, STransformComponent>(entt::exclude<SCharacterControllerComponent>);
            for (entt::entity Entity : View)
            {
                glm::vec3 Mn, Mx;
                if (!ComputeEntityAABB(View.get<SStaticMeshComponent>(Entity), View.get<STransformComponent>(Entity), Mn, Mx))
                {
                    continue;
                }
                OutCache[(uint32)Entity] = FNavSourceEntity{ Mn, Mx };
            }
        }

        // Build the full FNavBuildInput consumed by Bake() / BakeSingleTile().
        // Lazy-evaluated by the caller and reused for both initial bake and
        // partial rebuilds.
        void FillBuildInput(const FSystemContext& Context, SNavMeshComponent& Comp, FNavBuildInput& Out, THashMap<uint32, FNavSourceEntity>& OutAABBs)
        {
            Out.Settings  = Comp.Settings;
            Out.BoundsMin = Comp.Center - Comp.Extents;
            Out.BoundsMax = Comp.Center + Comp.Extents;

            FGatherAccumulator Acc;
            GatherSourceGeometry(Context, Out.BoundsMin, Out.BoundsMax, Acc, OutAABBs);
            Out.Vertices = std::move(Acc.Vertices);
            Out.Indices  = std::move(Acc.Indices);
        }

        // Walk components, finalize completed bakes, drain per-tile rebakes,
        // detect entity AABB changes and dirty the affected tiles, and kick
        // any pending rebake jobs.
        void TickComponent(const FSystemContext& Context, entt::entity Entity, SNavMeshComponent& Comp)
        {
            // 0. Editor / gameplay requested a fresh bake. Convert the flag
            //    into a real RequestBake call now that we have a context.
            if (Comp.bBakeRequested)
            {
                Comp.bBakeRequested = false;
                SNavMeshSystem::RequestBake(Context, Comp);
            }

            // 1. Drain a finished full bake. We don't construct the FNavMesh
            // here - that work goes onto a worker via PendingInit so main
            // never pays the dtNavMesh::init + per-tile addTile cost.
            if (Comp.Runtime.ActiveBake && Comp.Runtime.ActiveBake->bDone.load(std::memory_order_acquire))
            {
                FNavBuildOutput& Out = Comp.Runtime.ActiveBake->Output;
                Comp.Tiles           = std::move(Out.Tiles);
                Comp.Origin          = Out.Origin;
                Comp.TileWorldSize   = Out.TileWorldSize;
                Comp.MaxPolysPerTile = Out.MaxPolysPerTile;
                Comp.Runtime.LiveLayout.Origin = Out.Origin;
                Comp.Runtime.LiveLayout.TileWorldSize = Out.TileWorldSize;
                Comp.Runtime.LiveLayout.MaxTiles = Out.MaxTiles;
                Comp.Runtime.LiveLayout.MaxPolysPerTile = Out.MaxPolysPerTile;
                Comp.Runtime.ActiveBake.reset();
                Comp.Runtime.bRuntimeDirty = true;
                RebuildEntityAABBCache(Context, Comp.Runtime.EntityAABBs);
                Comp.Runtime.DirtyTiles.clear();
            }

            // 2. Drain a finished async hydration. The worker built the
            // FNavMesh + triangle cache; main thread just hands it over.
            if (Comp.Runtime.PendingInit && Comp.Runtime.PendingInit->bDone.load(std::memory_order_acquire))
            {
                Comp.Runtime.Mesh = std::move(Comp.Runtime.PendingInit->ResultMesh);
                Comp.Runtime.PendingInit.reset();
                Comp.Runtime.State = ENavBakeState::Ready;
            }

            // 3. Kick async hydration when tiles are present and either we
            // have no live mesh yet (first bake / PIE clone / world load)
            // OR the tiles changed since the last init (subsequent bakes
            // set bRuntimeDirty in step 1). Without the bRuntimeDirty
            // branch, a re-bake leaves the old Mesh in place, the
            // condition stays false, and the editor stays stuck on
            // "Baking..." because State never leaves Building.
            if (Comp.HasBakedData() && !Comp.Runtime.PendingInit && (Comp.Runtime.bRuntimeDirty || !Comp.Runtime.Mesh))
            {
                const glm::vec3 BakeMin = Comp.Center - Comp.Extents;
                const glm::vec3 BakeMax = Comp.Center + Comp.Extents;
                Comp.Runtime.TilesX = std::max(1, (int32)std::ceil((BakeMax.x - BakeMin.x) / Comp.TileWorldSize));
                Comp.Runtime.TilesY = std::max(1, (int32)std::ceil((BakeMax.z - BakeMin.z) / Comp.TileWorldSize));
                Comp.Runtime.LiveLayout.Origin          = Comp.Origin;
                Comp.Runtime.LiveLayout.TileWorldSize   = Comp.TileWorldSize;
                Comp.Runtime.LiveLayout.MaxTiles        = (int32)Comp.Tiles.size();
                Comp.Runtime.LiveLayout.MaxPolysPerTile = Comp.MaxPolysPerTile;

                auto Job = MakeShared<FNavInitJob>();
                Comp.Runtime.PendingInit = Job;
                Comp.Runtime.bRuntimeDirty = false;
                Comp.Runtime.State = ENavBakeState::Initializing;

                // Copy tiles so the worker owns its own data; Comp.Tiles
                // remains the serialized source of truth and isn't touched
                // by the async init.
                TVector<FNavTileData> TilesCopy = Comp.Tiles;
                const glm::vec3 InitOrigin = Comp.Origin;
                const float InitTileSize = Comp.TileWorldSize;
                const int32 InitMaxTiles = (int32)TilesCopy.size();
                const int32 InitMaxPolys = Comp.MaxPolysPerTile;

                Task::AsyncTask(1, 1, [Job, Tiles = std::move(TilesCopy), InitOrigin, InitTileSize, InitMaxTiles, InitMaxPolys](uint32, uint32, uint32) mutable
                {
                    auto Mesh = MakeUnique<FNavMesh>();
                    Mesh->Initialize(InitOrigin, InitTileSize, InitMaxTiles, InitMaxPolys, std::move(Tiles));
                    Job->ResultMesh = std::move(Mesh);
                    Job->bDone.store(true, std::memory_order_release);
                });

                RebuildEntityAABBCache(Context, Comp.Runtime.EntityAABBs);
                Comp.Runtime.DirtyTiles.clear();
            }

            if (!Comp.Runtime.Mesh || !Comp.Runtime.Mesh->IsReady() || Comp.Runtime.State != ENavBakeState::Ready)
            {
                return;
            }

            // Debug draw runs FIRST so it always emits while the mesh is
            // ready, regardless of whether step 5 below decides to early
            // return on empty dirty tiles or saturated rebake jobs.
            if (Comp.bDrawDebug)
            {
                const glm::vec4 EdgeColor(0.2f, 0.9f, 0.4f, 1.0f);
                Comp.Runtime.Mesh->ForEachTriangle([&](const glm::vec3& A, const glm::vec3& B, const glm::vec3& C, uint8 /*Area*/)
                {
                    const glm::vec3 Lift(0.0f, 0.05f, 0.0f);
                    Context.DrawDebugLine(A + Lift, B + Lift, EdgeColor, 1.0f, -1.0f);
                    Context.DrawDebugLine(B + Lift, C + Lift, EdgeColor, 1.0f, -1.0f);
                    Context.DrawDebugLine(C + Lift, A + Lift, EdgeColor, 1.0f, -1.0f);
                });
            }

            // 3. Hot-swap completed per-tile rebakes.
            for (auto& Job : Comp.Runtime.PendingRebakes)
            {
                if (!Job || Job->bConsumed.load(std::memory_order_acquire))
                {
                    continue;
                }
                if (!Job->bDone.load(std::memory_order_acquire))
                {
                    continue;
                }

                Comp.Runtime.Mesh->RebuildTile(Job->TileX, Job->TileY, std::move(Job->ResultBlob));
                // Persist into Comp.Tiles so the next world save captures the
                // updated layout. Find the matching tile index by (X,Y).
                for (FNavTileData& T : Comp.Tiles)
                {
                    if (T.X == Job->TileX && T.Y == Job->TileY)
                    {
                        T.Blob.clear(); // fresh blob already moved into the mesh; serializing it back requires a copy at swap time
                        break;
                    }
                }
                Job->bConsumed.store(true, std::memory_order_release);
            }
            
            Comp.Runtime.PendingRebakes.erase(
                eastl::remove_if(Comp.Runtime.PendingRebakes.begin(), Comp.Runtime.PendingRebakes.end(),
                    [](const TSharedPtr<FNavTileRebake>& J) { return !J || J->bConsumed.load(std::memory_order_acquire); }),
                Comp.Runtime.PendingRebakes.end());

            // 4. Detect moved/added/removed source entities and dirty their tiles.
            const glm::vec3 BakeMin = Comp.Center - Comp.Extents;
            THashMap<uint32, FNavSourceEntity> CurrentAABBs;
            CurrentAABBs.reserve(Comp.Runtime.EntityAABBs.size());

            auto MeshView = Context.CreateView<SStaticMeshComponent, STransformComponent>(entt::exclude<SCharacterControllerComponent>);
            for (entt::entity E : MeshView)
            {
                glm::vec3 Mn, Mx;
                if (!ComputeEntityAABB(MeshView.get<SStaticMeshComponent>(E), MeshView.get<STransformComponent>(E), Mn, Mx))
                {
                    continue;
                }
                CurrentAABBs[(uint32)E] = FNavSourceEntity{ Mn, Mx };

                auto It = Comp.Runtime.EntityAABBs.find((uint32)E);
                const bool bNew = It == Comp.Runtime.EntityAABBs.end();
                const bool bMoved = !bNew && (!Math::IsNearlyEqual(It->second.AABBMin, Mn) || !Math::IsNearlyEqual(It->second.AABBMax, Mx));                
                if (bNew || bMoved)
                {
                    int32 TX0, TY0, TX1, TY1;
                    if (bMoved)
                    {
                        // Old footprint also gets dirtied so triangles we
                        // were standing on are re-evaluated.
                        TilesForAABB(It->second.AABBMin, It->second.AABBMax, Comp.Origin, Comp.TileWorldSize, Comp.Runtime.TilesX, Comp.Runtime.TilesY, TX0, TY0, TX1, TY1);
                        for (int32 ty = TY0; ty <= TY1; ++ty)
                        {
                            for (int32 tx = TX0; tx <= TX1; ++tx)
                            {
                                Comp.Runtime.DirtyTiles.insert(PackTileKey(tx, ty));
                            }
                        }
                    }
                    TilesForAABB(Mn, Mx, Comp.Origin, Comp.TileWorldSize, Comp.Runtime.TilesX, Comp.Runtime.TilesY, TX0, TY0, TX1, TY1);
                    for (int32 ty = TY0; ty <= TY1; ++ty)
                    {
                        for (int32 tx = TX0; tx <= TX1; ++tx)
                        {
                            Comp.Runtime.DirtyTiles.insert(PackTileKey(tx, ty));
                        }
                    }
                }
            }

            // Removed entities also dirty their last-known tiles.
            for (const auto& [Id, Snap] : Comp.Runtime.EntityAABBs)
            {
                if (CurrentAABBs.find(Id) == CurrentAABBs.end())
                {
                    int32 TX0, TY0, TX1, TY1;
                    TilesForAABB(Snap.AABBMin, Snap.AABBMax, Comp.Origin, Comp.TileWorldSize, Comp.Runtime.TilesX, Comp.Runtime.TilesY, TX0, TY0, TX1, TY1);
                    for (int32 ty = TY0; ty <= TY1; ++ty)
                    {
                        for (int32 tx = TX0; tx <= TX1; ++tx)
                        {
                            Comp.Runtime.DirtyTiles.insert(PackTileKey(tx, ty));
                        }
                    }
                }
            }

            Comp.Runtime.EntityAABBs = std::move(CurrentAABBs);

            // 5. Kick rebakes for dirty tiles. Cap concurrent jobs so a
            // burst of movement doesn't saturate the worker pool. The
            // remaining tiles stay dirty and pick up next tick.
            constexpr uint32 MaxConcurrent = 8;
            if (Comp.Runtime.DirtyTiles.empty() || Comp.Runtime.PendingRebakes.size() >= MaxConcurrent)
            {
                return;
            }

            // Snapshot only mesh asset pointers + world matrices on the main
            // thread. Cheap (one matrix copy per static mesh entity). The
            // expensive meshlet decode runs on a worker via the coordinator
            // task below - that decode used to live here and ate ~4ms.
            const uint32 Capacity = MaxConcurrent - (uint32)Comp.Runtime.PendingRebakes.size();
            TVector<TSharedPtr<FNavTileRebake>> BatchJobs;
            BatchJobs.reserve(Capacity);
            for (auto It = Comp.Runtime.DirtyTiles.begin(); It != Comp.Runtime.DirtyTiles.end() && BatchJobs.size() < Capacity; )
            {
                const uint64 Key = *It;
                It = Comp.Runtime.DirtyTiles.erase(It);

                auto Job = MakeShared<FNavTileRebake>();
                Job->TileX = (int32)(Key & 0xFFFFFFFFu);
                Job->TileY = (int32)(Key >> 32);
                BatchJobs.push_back(Job);
                Comp.Runtime.PendingRebakes.push_back(std::move(Job));
            }

            struct FInputSnapshot
            {
                TVector<CStaticMesh*>   Meshes;
                TVector<glm::mat4>      Matrices;
                glm::vec3               BakeMin;
                glm::vec3               BakeMax;
                FNavBuildSettings       Settings;
                FNavBuildOutput         Layout;
            };
            auto Snap = MakeShared<FInputSnapshot>();
            Snap->BakeMin  = Comp.Center - Comp.Extents;
            Snap->BakeMax  = Comp.Center + Comp.Extents;
            Snap->Settings = Comp.Settings;
            Snap->Layout   = Comp.Runtime.LiveLayout;
            {
                auto SnapView = Context.CreateView<SStaticMeshComponent, STransformComponent>(entt::exclude<SCharacterControllerComponent>);
                Snap->Meshes.reserve(SnapView.size_hint());
                Snap->Matrices.reserve(SnapView.size_hint());
                for (entt::entity E : SnapView)
                {
                    CStaticMesh* M = SnapView.get<SStaticMeshComponent>(E).GetStaticMesh();
                    if (!M || M->GetMeshResource().bSkinnedMesh) continue;
                    Snap->Meshes.push_back(M);
                    Snap->Matrices.push_back(SnapView.get<STransformComponent>(E).GetWorldMatrix());
                }
            }

            // One coordinator task: decodes geometry once on a worker, then
            // ParallelFors the per-tile bakes against the shared input.
            // Inner ParallelFor amortizes wait time across the worker pool.
            Task::AsyncTask(1, 1, [Snap, Jobs = std::move(BatchJobs)](uint32, uint32, uint32) mutable
            {
                FNavBuildInput Input;
                Input.BoundsMin = Snap->BakeMin;
                Input.BoundsMax = Snap->BakeMax;
                Input.Settings  = Snap->Settings;

                FGatherAccumulator Acc;
                for (size_t i = 0; i < Snap->Meshes.size(); ++i)
                {
                    glm::vec3 Mn, Mx;
                    DecodeMeshIntoAccumulator(Snap->Meshes[i], Snap->Matrices[i], Snap->BakeMin, Snap->BakeMax, Acc, Mn, Mx);
                }
                Input.Vertices = std::move(Acc.Vertices);
                Input.Indices  = std::move(Acc.Indices);

                Task::ParallelFor((uint32)Jobs.size(), [&](uint32 i)
                {
                    FNavTileData Out;
                    if (NavMeshBuilder::BakeSingleTile(Input, Snap->Layout, Jobs[i]->TileX, Jobs[i]->TileY, Out))
                    {
                        Jobs[i]->ResultBlob = std::move(Out.Blob);
                    }
                    Jobs[i]->bDone.store(true, std::memory_order_release);
                });
            });

            (void)Entity;
        }

        FNavMesh* FirstReadyNavMesh(const FSystemContext& Context)
        {
            auto View = Context.CreateView<SNavMeshComponent>();
            for (entt::entity Entity : View)
            {
                SNavMeshComponent& Comp = View.get<SNavMeshComponent>(Entity);
                if (Comp.Runtime.Mesh && Comp.Runtime.Mesh->IsReady())
                {
                    return Comp.Runtime.Mesh.get();
                }
            }
            return nullptr;
        }
    }

    namespace
    {
        // Fires when a SNavMeshComponent is added to an entity (inspector,
        // script, code). Initializes Center from the entity's transform so
        // the bake volume defaults to the entity's location rather than the
        // origin.
        void OnNavMeshConstructed(entt::registry& Reg, entt::entity Entity)
        {
            SNavMeshComponent& Nav = Reg.get<SNavMeshComponent>(Entity);
            if (auto* Xform = Reg.try_get<STransformComponent>(Entity))
            {
                Nav.Center = Xform->GetWorldTransform().Location;
            }
            // Make the new bounds visible by default so the user can see
            // where they placed it without hunting for the checkbox.
            Nav.bDrawDebug = true;
        }
    }

    void SNavMeshSystem::Startup(const FSystemContext& Context) noexcept
    {
        LUMINA_PROFILE_SCOPE();

        // Register the construction callback once. Connecting twice no-ops
        // because entt sinks dedupe on the same free-function pointer.
        Context.GetRegistry().on_construct<SNavMeshComponent>().connect<&OnNavMeshConstructed>();

        auto View = Context.CreateView<SNavMeshComponent>();
        for (entt::entity Entity : View)
        {
            TickComponent(Context, Entity, View.get<SNavMeshComponent>(Entity));
        }
    }

    void SNavMeshSystem::Update(const FSystemContext& Context) noexcept
    {
        LUMINA_PROFILE_SCOPE();
        auto View = Context.CreateView<SNavMeshComponent>();
        for (entt::entity Entity : View)
        {
            TickComponent(Context, Entity, View.get<SNavMeshComponent>(Entity));
        }
    }

    void SNavMeshSystem::Teardown(const FSystemContext& Context) noexcept
    {
        LUMINA_PROFILE_SCOPE();
        auto View = Context.CreateView<SNavMeshComponent>();
        for (entt::entity Entity : View)
        {
            SNavMeshComponent& Comp = View.get<SNavMeshComponent>(Entity);
            if (Comp.Runtime.ActiveBake)
            {
                Comp.Runtime.ActiveBake->bCancelRequested.store(true, std::memory_order_release);
            }
            Comp.Runtime.Mesh.reset();
            Comp.Runtime.ActiveBake.reset();
            // PendingInit's worker holds its own shared_ptr; clearing the
            // component's slot just stops it from being consumed when the
            // worker eventually finishes.
            Comp.Runtime.PendingInit.reset();
            Comp.Runtime.PendingRebakes.clear();
            Comp.Runtime.DirtyTiles.clear();
            Comp.Runtime.EntityAABBs.clear();
            Comp.Runtime.State = ENavBakeState::Idle;
        }
    }

    void SNavMeshSystem::RequestBake(const FSystemContext& Context, SNavMeshComponent& Comp)
    {
        if (Comp.Runtime.ActiveBake)
        {
            return;
        }

        FNavBuildInput Input;
        THashMap<uint32, FNavSourceEntity> Unused;
        FillBuildInput(Context, Comp, Input, Unused);

        // EntityAABB cache populated at bake-completion drain instead of
        // here - the gather's tight per-triangle AABBs would mismatch the
        // change-detector's conservative 8-corner AABBs and cause a
        // spurious tile-rebake storm right after the bake lands.
        Comp.Runtime.EntityAABBs.clear();
        Comp.Runtime.DirtyTiles.clear();
        Comp.Runtime.PendingRebakes.clear();
        Comp.Runtime.ActiveBake = NavMeshBuilder::Bake(std::move(Input));
        Comp.Runtime.State = ENavBakeState::Building;
    }

    namespace Nav
    {
        bool FindPath(const FSystemContext& Context, const glm::vec3& Start, const glm::vec3& End, const FNavQueryFilter& Filter, FNavPath& Out)
        {
            FNavMesh* Mesh = FirstReadyNavMesh(Context);
            return Mesh && Mesh->FindPath(Start, End, Filter, Out);
        }

        bool ProjectPoint(const FSystemContext& Context, const glm::vec3& World, const glm::vec3& Extents, const FNavQueryFilter& Filter, glm::vec3& Out)
        {
            FNavMesh* Mesh = FirstReadyNavMesh(Context);
            return Mesh && Mesh->ProjectPoint(World, Extents, Filter, Out);
        }

        bool Raycast(const FSystemContext& Context, const glm::vec3& Start, const glm::vec3& End, const FNavQueryFilter& Filter, glm::vec3& HitOut)
        {
            FNavMesh* Mesh = FirstReadyNavMesh(Context);
            return Mesh && Mesh->Raycast(Start, End, Filter, HitOut);
        }

        // ---- CWorld flavor -------------------------------------------

        namespace
        {
            // Locate the first SNavMeshComponent whose runtime FNavMesh is
            // ready in this world. Multi-bounds support layers on top later
            // (route by entity, area mask, etc.).
            FNavMesh* FirstReadyNavMeshFromWorld(CWorld* World)
            {
                if (!World) return nullptr;
                auto& Reg = World->GetEntityRegistry();
                auto View = Reg.view<SNavMeshComponent>();
                for (entt::entity E : View)
                {
                    SNavMeshComponent& Comp = View.get<SNavMeshComponent>(E);
                    if (Comp.Runtime.Mesh && Comp.Runtime.Mesh->IsReady())
                    {
                        return Comp.Runtime.Mesh.get();
                    }
                }
                return nullptr;
            }
        }

        bool IsReady(CWorld* World)
        {
            return FirstReadyNavMeshFromWorld(World) != nullptr;
        }

        bool FindPath(CWorld* World, const glm::vec3& Start, const glm::vec3& End, FNavPath& Out)
        {
            FNavMesh* Mesh = FirstReadyNavMeshFromWorld(World);
            FNavQueryFilter Filter;
            return Mesh && Mesh->FindPath(Start, End, Filter, Out);
        }

        bool ProjectPoint(CWorld* World, const glm::vec3& Point, const glm::vec3& Extents, glm::vec3& Out)
        {
            FNavMesh* Mesh = FirstReadyNavMeshFromWorld(World);
            FNavQueryFilter Filter;
            return Mesh && Mesh->ProjectPoint(Point, Extents, Filter, Out);
        }

        bool Raycast(CWorld* World, const glm::vec3& Start, const glm::vec3& End, glm::vec3& OutHit)
        {
            FNavMesh* Mesh = FirstReadyNavMeshFromWorld(World);
            FNavQueryFilter Filter;
            return Mesh && Mesh->Raycast(Start, End, Filter, OutHit);
        }

        bool FindRandomReachablePoint(CWorld* World, const glm::vec3& Origin, float Radius, glm::vec3& Out)
        {
            FNavMesh* Mesh = FirstReadyNavMeshFromWorld(World);
            FNavQueryFilter Filter;
            return Mesh && Mesh->FindRandomPoint(Origin, Radius, Filter, Out);
        }

        bool IsReachable(CWorld* World, const glm::vec3& From, const glm::vec3& To)
        {
            FNavPath Path;
            return FindPath(World, From, To, Path) && Path.bValid && !Path.bPartial;
        }

        float PathLength(CWorld* World, const glm::vec3& From, const glm::vec3& To)
        {
            FNavPath Path;
            if (!FindPath(World, From, To, Path) || !Path.bValid) return -1.0f;
            float Len = 0.0f;
            for (size_t i = 1; i < Path.Corners.size(); ++i)
            {
                Len += glm::length(Path.Corners[i] - Path.Corners[i - 1]);
            }
            return Len;
        }

        // ---- Lua module ----------------------------------------------

        void RegisterLuaModule(Lua::FRef& Globals)
        {
            Lua::FRef NavTable = Globals.NewTable("Nav");

            // Boolean status / reachability queries.
            NavTable.SetFunction<[](CWorld* W) { return Nav::IsReady(W); }>("IsReady");
            NavTable.SetFunction<[](CWorld* W, glm::vec3 From, glm::vec3 To) { return Nav::IsReachable(W, From, To); }>("IsReachable");

            // Float-returning helpers. PathLength returns < 0 when no path.
            NavTable.SetFunction<[](CWorld* W, glm::vec3 From, glm::vec3 To) { return Nav::PathLength(W, From, To); }>("PathLength");

            // Vec3-returning helpers. Returns the input on failure so Lua
            // code can no-op gracefully; pair with IsReady / IsReachable
            // when validity matters.
            NavTable.SetFunction<[](CWorld* W, glm::vec3 P, glm::vec3 E) -> glm::vec3
            {
                glm::vec3 Out = P;
                Nav::ProjectPoint(W, P, E, Out);
                return Out;
            }>("ProjectPoint");

            NavTable.SetFunction<[](CWorld* W, glm::vec3 S, glm::vec3 E) -> glm::vec3
            {
                glm::vec3 Out = E;
                Nav::Raycast(W, S, E, Out);
                return Out;
            }>("Raycast");

            NavTable.SetFunction<[](CWorld* W, glm::vec3 O, float R) -> glm::vec3
            {
                glm::vec3 Out = O;
                Nav::FindRandomReachablePoint(W, O, R, Out);
                return Out;
            }>("FindRandomReachablePoint");

            // Path corners as a Luau array. Empty when no path exists.
            NavTable.SetFunction<[](CWorld* W, glm::vec3 S, glm::vec3 E) -> TVector<glm::vec3>
            {
                FNavPath Path;
                Nav::FindPath(W, S, E, Path);
                return Path.Corners;
            }>("FindPath");
        }
    }
}
