#pragma once

#include "EntitySystem.h"
#include "Core/Object/ObjectMacros.h"
#include "AI/Navigation/NavTypes.h"
#include "NavMeshSystem.generated.h"

namespace Lumina
{
    struct SNavMeshComponent;

    /**
     * Owns the lifecycle of every SNavMeshComponent in the world:
     *   - Startup: rehydrate FNavMesh from each component's baked tile blobs.
     *   - Update : drain in-flight bakes; rebuild FNavMesh whenever a
     *              component's Tiles vector has changed since last tick.
     *   - Teardown: free runtimes.
     *
     * Also exposes free-function query helpers that route to the first
     * navmesh-equipped entity in the world. Gameplay code (native + Lua)
     * calls those instead of touching FNavMesh directly.
     */
    REFLECT(System)
    struct RUNTIME_API SNavMeshSystem
    {
        GENERATED_BODY()
        // Paused so editor mode also ticks: bake button, debug draw, and
        // the dynamic dirty-tile detector all need to run in edit time.
        ENTITY_SYSTEM(RequiresUpdate(EUpdateStage::FrameStart), RequiresUpdate(EUpdateStage::Paused))

    public:

        static void Startup (const FSystemContext& Context) noexcept;
        static void Update  (const FSystemContext& Context) noexcept;
        static void Teardown(const FSystemContext& Context) noexcept;

        /**
         * Kick a parallel bake on Component using the world snapshot in
         * Context. Returns immediately; the bake runs on workers and is
         * consumed by the next Update tick. No-op if a bake is already in
         * flight on the same component.
         */
        static void RequestBake(const FSystemContext& Context, SNavMeshComponent& Component);
    };

    class CWorld;

    namespace Lua { class FRef; }

    /**
     * Navigation helper API. Each function walks the world for the first
     * ready SNavMeshComponent and dispatches to its FNavMesh. Thread-safe
     * for queries; the underlying FNavMesh acquires queries from a
     * wait-free pool.
     *
     * Two flavors:
     *  - FSystemContext-taking: cheapest path from inside an entity system
     *    (the registry view is already the system context's).
     *  - CWorld-taking: usable from gameplay code, editor tooling, and
     *    Lua bindings. Identical semantics; pulls the registry from the
     *    world.
     */
    namespace Nav
    {
        // ---- FSystemContext flavor (in-system call sites) -------------
        RUNTIME_API bool FindPath(const FSystemContext& Context, const glm::vec3& Start, const glm::vec3& End, const FNavQueryFilter& Filter, FNavPath& Out);
        RUNTIME_API bool ProjectPoint(const FSystemContext& Context, const glm::vec3& World, const glm::vec3& Extents, const FNavQueryFilter& Filter, glm::vec3& Out);
        RUNTIME_API bool Raycast(const FSystemContext& Context, const glm::vec3& Start, const glm::vec3& End, const FNavQueryFilter& Filter, glm::vec3& HitOut);

        // ---- CWorld flavor (gameplay / editor / Lua) ------------------
        RUNTIME_API bool IsReady(CWorld* World);
        RUNTIME_API bool FindPath(CWorld* World, const glm::vec3& Start, const glm::vec3& End, FNavPath& Out);
        RUNTIME_API bool ProjectPoint(CWorld* World, const glm::vec3& Point, const glm::vec3& Extents, glm::vec3& Out);
        RUNTIME_API bool Raycast(CWorld* World, const glm::vec3& Start, const glm::vec3& End, glm::vec3& OutHit);

        /**
         * Random walkable point sampled inside (Origin, Radius), reachable
         * via the navmesh. Bread-and-butter for AI wander, patrol seed,
         * spawn-point selection.
         */
        RUNTIME_API bool FindRandomReachablePoint(CWorld* World, const glm::vec3& Origin, float Radius, glm::vec3& Out);

        /**
         * True when a navmesh-corner path exists between From and To.
         * Convenience wrapper over FindPath; no allocation visible to the
         * caller.
         */
        RUNTIME_API bool IsReachable(CWorld* World, const glm::vec3& From, const glm::vec3& To);

        /**
         * Approximate path length along the navmesh between From and To.
         * Returns a negative value when no path exists. Useful for AI
         * cost heuristics that don't need the corner list itself.
         */
        RUNTIME_API float PathLength(CWorld* World, const glm::vec3& From, const glm::vec3& To);

        /**
         * Bind the helpers above into a Lua "Nav" global table. Called
         * once during Scripting init.
         */
        RUNTIME_API void RegisterLuaModule(Lua::FRef& Globals);
    }
}
