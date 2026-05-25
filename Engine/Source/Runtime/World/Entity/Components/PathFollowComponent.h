#pragma once

#include "AI/Navigation/NavTypes.h"
#include "Core/Object/ObjectMacros.h"
#include "PathFollowComponent.generated.h"

namespace Lumina
{
    /**
     * Result of the most recent path query. Distinct from "is the agent
     * currently moving" - the agent may keep walking the prior corners
     * after a failed replan, so a follower can be Following with a
     * Failed last query.
     */
    enum class EPathFollowStatus : uint8
    {
        None      = 0, // No target set, or target was just cleared.
        Searching = 1, // Target set; first query has not yet been issued (or just dirtied).
        Following = 2, // Last query succeeded; corners are fresh.
        Reached   = 3, // Agent has consumed the corner list.
        Failed    = 4, // Last query returned no valid path.
    };

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
    REFLECT(Component, Category = "AI")
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
            Status         = EPathFollowStatus::Searching;
            ConsecutiveFailures = 0;
        }

        /** Track an entity. The system re-projects the entity's current location each tick. */
        FUNCTION(Script)
        void SetTargetEntity(entt::entity Entity)
        {
            TargetEntity = Entity;
            bHasTarget   = (Entity != entt::null);
            bPathDirty   = true;
            Status       = bHasTarget ? EPathFollowStatus::Searching : EPathFollowStatus::None;
            ConsecutiveFailures = 0;
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
            Status = EPathFollowStatus::None;
            ConsecutiveFailures = 0;
        }

        FUNCTION(Script)
        bool IsFollowing() const { return bHasTarget && CornerCount > 0; }

        FUNCTION(Script)
        bool IsAtDestination() const { return bHasTarget && CornerCount > 0 && CurrentCorner >= CornerCount; }

        /** True if the most recent path query failed. Stays true until a subsequent query succeeds or the target is cleared. */
        FUNCTION(Script)
        bool DidPathFindingFail() const { return Status == EPathFollowStatus::Failed; }

        /** Number of consecutive failed queries since the last success. Useful for script-side give-up logic. */
        FUNCTION(Script)
        int32 GetConsecutivePathFailures() const { return ConsecutiveFailures; }

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

        /** Throttle (0..1) applied to the controller's MoveSpeed; 1 = full speed.
         *  Lower it for a walk; the movement system supplies the absolute m/s. */
        PROPERTY(Editable, Category = "PathFollow", ClampMin = 0.0f, ClampMax = 1.0f)
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

        /** Latched outcome of the most recent path query. Updated by SPathFollowSystem. */
        EPathFollowStatus Status = EPathFollowStatus::None;

        /** Resets to 0 on a successful query, increments on every failed query. */
        int32 ConsecutiveFailures = 0;
    };
}
