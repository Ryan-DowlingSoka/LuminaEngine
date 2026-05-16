#pragma once

#include "EntitySystem.h"
#include "Core/Object/ObjectMacros.h"
#include "AI/Navigation/NavTypes.h"
#include "NavMeshSystem.generated.h"

namespace Lumina
{
    struct SNavMeshComponent;

    /** Owns SNavMeshComponent lifecycle: rehydrate, drain bakes, rebuild on Tiles change. */
    REFLECT(System)
    struct RUNTIME_API SNavMeshSystem
    {
        GENERATED_BODY()
        // Paused stage required so editor-mode ticks for bake button, debug draw, dirty detection.
        ENTITY_SYSTEM(RequiresUpdate(EUpdateStage::FrameStart), RequiresUpdate(EUpdateStage::Paused))

    public:

        static void Startup (const FSystemContext& Context) noexcept;
        static void Update  (const FSystemContext& Context) noexcept;
        static void Teardown(const FSystemContext& Context) noexcept;

        /** Kick async bake; no-op if one is already in flight. */
        static void RequestBake(const FSystemContext& Context, SNavMeshComponent& Component);
    };

    class CWorld;

    namespace Lua { class FRef; }

    /** Navigation helpers; each function dispatches to the first ready SNavMeshComponent. */
    namespace Nav
    {
        RUNTIME_API bool FindPath(const FSystemContext& Context, const glm::vec3& Start, const glm::vec3& End, const FNavQueryFilter& Filter, FNavPath& Out);
        RUNTIME_API bool ProjectPoint(const FSystemContext& Context, const glm::vec3& World, const glm::vec3& Extents, const FNavQueryFilter& Filter, glm::vec3& Out);
        RUNTIME_API bool Raycast(const FSystemContext& Context, const glm::vec3& Start, const glm::vec3& End, const FNavQueryFilter& Filter, glm::vec3& HitOut);

        RUNTIME_API bool IsReady(CWorld* World);
        RUNTIME_API bool FindPath(CWorld* World, const glm::vec3& Start, const glm::vec3& End, FNavPath& Out);
        RUNTIME_API bool ProjectPoint(CWorld* World, const glm::vec3& Point, const glm::vec3& Extents, glm::vec3& Out);
        RUNTIME_API bool Raycast(CWorld* World, const glm::vec3& Start, const glm::vec3& End, glm::vec3& OutHit);

        /** Random walkable point inside (Origin, Radius). */
        RUNTIME_API bool FindRandomReachablePoint(CWorld* World, const glm::vec3& Origin, float Radius, glm::vec3& Out);

        RUNTIME_API bool IsReachable(CWorld* World, const glm::vec3& From, const glm::vec3& To);

        /** Returns negative when no path exists. */
        RUNTIME_API float PathLength(CWorld* World, const glm::vec3& From, const glm::vec3& To);

        /** Visualize an FNavPath via the World's debug drawer. Lift offsets the polyline above the mesh. */
        RUNTIME_API void DrawPath(CWorld* World, const FNavPath& Path, const glm::vec4& Color, float Thickness = 3.0f, float Lift = 0.15f, float Duration = 0.0f);

        /** Convenience: FindPath + DrawPath. Returns true if a (possibly partial) path was found and drawn. */
        RUNTIME_API bool DrawDebugPath(CWorld* World, const glm::vec3& From, const glm::vec3& To, const glm::vec4& Color, float Duration = 0.0f);

        RUNTIME_API void RegisterLuaModule(Lua::FRef& Globals);
    }
}
