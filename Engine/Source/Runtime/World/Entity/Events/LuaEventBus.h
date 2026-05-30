#pragma once

#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Scripting/Lua/Reference.h"

namespace Lumina
{
    // Decoupled entity event bus for Lua: dispatch named events with an optional payload; subscriptions are
    // global or entity-scoped (auto-removed on script destroy). Exposed as the "Events" global.
    class FLuaEventBus
    {
    public:
        
        /** Register a global Lua callback for the named event. */
        void Subscribe(FStringView EventName, Lua::FRef Callback);

        // Register an entity-owned callback; auto-removed when the entity's script component is destroyed.
        void SubscribeEntity(entt::entity Owner, FStringView EventName, Lua::FRef Callback);

        /** Remove a specific callback from the named event (global or entity-scoped). */
        void Unsubscribe(FStringView EventName, const Lua::FRef& Callback);
        
        // Immediately invoke all listeners for EventName. Payload may be a table, primitive, or nil.
        void Dispatch(FStringView EventName, const Lua::FRef& Payload);

        // Like Dispatch but only to listeners whose Owner == Target, so entity-targeted events (e.g. collisions)
        // reach the owning script without leaking to every other subscriber of the same name.
        void DispatchToEntity(entt::entity Target, FStringView EventName, const Lua::FRef& Payload);

        // Queue an event for the start of next frame (FrameStart). Safe to call inside a Dispatch
        // callback without re-entrant iteration.
        void DispatchDeferred(FStringView EventName, Lua::FRef Payload);
        
        /** Remove all subscriptions (global and entity-scoped) for an event. */
        void ClearEvent(FStringView EventName);

        /** Return the total number of active listeners for an event. */
        int32 GetSubscriberCount(FStringView EventName) const;

        /** Remove all entity-scoped subscriptions owned by Entity. */
        void UnsubscribeEntity(entt::entity Entity);

        /** Flush all deferred events. Called from CWorld::Update on FrameStart. */
        void ProcessDeferred();

        /** Remove everything. Called from CWorld::TeardownWorld. */
        void Clear();

    private:

        struct FListener
        {
            entt::entity    Owner    = entt::null;
            Lua::FRef       Callback;
            // Tombstone set when Unsubscribe lands mid-iteration; skipped during dispatch and reaped
            // when the outermost dispatch unwinds. Avoids the per-Dispatch full-vector copy.
            bool            bDead    = false;
        };

        struct FDeferredEvent
        {
            FName           EventName;
            Lua::FRef       Payload;
        };

        struct FPendingMutation
        {
            enum class EKind : uint8 { Subscribe, Unsubscribe, UnsubscribeEntity, ClearEvent };
            EKind           Kind;
            FName           EventName;
            entt::entity    Entity = entt::null;
            Lua::FRef       Callback;
        };

        // RAII guard around a Dispatch: bumps DispatchDepth on entry, drops it on exit,
        // and drains pending mutations + reaps tombstones when the outermost dispatch ends.
        struct FDispatchScope
        {
            FLuaEventBus* Bus;
            explicit FDispatchScope(FLuaEventBus* InBus) : Bus(InBus) { ++Bus->DispatchDepth; }
            ~FDispatchScope()
            {
                if (--Bus->DispatchDepth == 0)
                {
                    Bus->DrainPendingMutations();
                }
            }
            FDispatchScope(const FDispatchScope&) = delete;
            FDispatchScope& operator=(const FDispatchScope&) = delete;
        };

        void ApplySubscribe(FName EventName, entt::entity Owner, Lua::FRef Callback);
        void ApplyUnsubscribe(FName EventName, const Lua::FRef& Callback);
        void ApplyUnsubscribeEntity(entt::entity Entity);
        void ApplyClearEvent(FName EventName);
        void DrainPendingMutations();

        THashMap<FName, TVector<FListener>>  Subscriptions;
        TVector<FDeferredEvent>              DeferredQueue;
        TVector<FPendingMutation>            PendingMutations;
        int                                  DispatchDepth = 0;
    };
}
