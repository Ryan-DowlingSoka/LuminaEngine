#pragma once

#include "AI/Navigation/NavTypes.h"
#include "Core/Object/ObjectMacros.h"
#include "PathFollowComponent.generated.h"

namespace Lumina
{
    /**
     * Drives an entity along a navmesh path. Pair with SCharacterControllerComponent
     * (or any other movement consumer) - the SPathFollowSystem fills its
     * MoveInput each tick from the direction toward the next path corner.
     *
     * Gameplay sets a target by world position (SetTargetLocation) or
     * tracks a moving entity (SetTargetEntity). The system internally
     * requests a path via Nav::FindPath, caches the corner list, advances
     * once the agent is within AcceptanceRadius of the current corner, and
     * triggers a replan if the tracked target's position drifts past
     * RepathDistance from where the path was originally generated.
     */
    REFLECT(Component)
    struct RUNTIME_API SPathFollowComponent
    {
        GENERATED_BODY()

        //~ Begin Lua-facing API. Designed so a script can drive an AI with
        //  three lines: SetTargetLocation, IsFollowing, IsAtDestination.

        /** Set a static world-space goal. Triggers a fresh path request next tick. */
        FUNCTION(Script)
        void SetTargetLocation(const glm::vec3& World)
        {
            TargetLocation = World;
            TargetEntity   = entt::null;
            bHasTarget     = true;
            bPathDirty     = true;
        }

        /** Track an entity. The system re-projects the entity's current location each tick. */
        FUNCTION(Script)
        void SetTargetEntity(entt::entity Entity)
        {
            TargetEntity = Entity;
            bHasTarget   = (Entity != entt::null);
            bPathDirty   = true;
        }

        /** Clear the goal and any cached path. */
        FUNCTION(Script)
        void Stop()
        {
            bHasTarget = false;
            TargetEntity = entt::null;
            CornerCount = 0;
            CurrentCorner = 0;
            bPathDirty = false;
        }

        FUNCTION(Script)
        bool IsFollowing() const { return bHasTarget && CornerCount > 0; }

        FUNCTION(Script)
        bool IsAtDestination() const { return bHasTarget && CornerCount > 0 && CurrentCorner >= CornerCount; }

        /** Closest queued path corner, or the target if no path is cached. */
        FUNCTION(Script)
        glm::vec3 GetNextCorner() const
        {
            if (CornerCount == 0 || CurrentCorner >= CornerCount) return TargetLocation;
            return PathCorners[CurrentCorner];
        }

        //~ End Lua-facing API.

        /** Distance below which the agent considers a corner "reached" and advances. */
        PROPERTY(Editable, Category = "PathFollow", ClampMin = 0.05f)
        float AcceptanceRadius = 0.5f;

        /** Linear movement scale fed into the controller. Tune per-agent for runspeed. */
        PROPERTY(Editable, Category = "PathFollow", ClampMin = 0.0f)
        float Speed = 1.0f;

        /**
         * Replan when the tracked target's position has moved more than this
         * from the source point of the cached path. Ignored when the goal is
         * a static world location.
         */
        PROPERTY(Editable, Category = "PathFollow", ClampMin = 0.0f)
        float RepathDistance = 1.5f;

        /** Hard repath interval as a backstop, in seconds. */
        PROPERTY(Editable, Category = "PathFollow", ClampMin = 0.0f)
        float RepathInterval = 1.0f;

        /** When true the system writes movement input each tick. Can be toggled by gameplay. */
        PROPERTY(Editable, Category = "PathFollow")
        bool bDriveCharacterController = true;

        /** When true, the system emits debug lines along the cached path each tick. */
        PROPERTY(Editable, Category = "PathFollow|Debug")
        bool bDrawDebugPath = false;

        /** Cached path corners filled by the system. Capped to a fixed array to avoid per-tick heap churn. */
        static constexpr int32 MaxCorners = 64;
        glm::vec3   PathCorners[MaxCorners] = {};
        int32       CornerCount   = 0;
        int32       CurrentCorner = 0;

        /** World location of the active goal (latched from entity if tracking one). */
        glm::vec3   TargetLocation = glm::vec3(0.0f);
        entt::entity TargetEntity = entt::null;
        glm::vec3   PathSourceTarget = glm::vec3(0.0f); // target location at the moment the cached path was generated
        float       TimeSinceLastPath = 0.0f;
        bool        bHasTarget = false;
        bool        bPathDirty = false;
    };
}
