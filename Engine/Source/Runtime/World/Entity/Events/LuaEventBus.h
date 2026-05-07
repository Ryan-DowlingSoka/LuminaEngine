#pragma once

#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Scripting/Lua/Reference.h"

namespace Lumina
{
    /**
     * A decoupled event bus for entity communication in Lua scripts.
     *
     * Entities dispatch named events with an optional table payload; any script that
     * subscribed to that name receives a callback.  Subscriptions can be either global
     * (persist until manually removed) or entity-scoped (automatically removed when the
     * entity's SScriptComponent is destroyed).
     *
     * Lua API (available as the "Events" global inside every script):
     *
     *   Events:Subscribe("OnDoorOpened", function(payload) ... end)
     *   Events:SubscribeEntity(self.Entity, "OnDoorOpened", function(payload) ... end)
     *   Events:Unsubscribe("OnDoorOpened", myHandler)
     *   Events:Dispatch("OnDoorOpened", { Door = self.Entity, Speed = 2.0 })
     *   Events:DispatchDeferred("OnGameOver", nil) -- fires at start of next frame
     *   Events:ClearEvent("OnDoorOpened")
     *   local count = Events:GetSubscriberCount("OnDoorOpened")
     */
    class FLuaEventBus
    {
    public:
        
        /** Register a global Lua callback for the named event. */
        void Subscribe(FStringView EventName, Lua::FRef Callback);

        /**
         * Register a Lua callback owned by a specific entity.
         * The subscription is automatically removed when the entity's script component
         * is destroyed (via UnsubscribeEntity called from CWorld).
         */
        void SubscribeEntity(entt::entity Owner, FStringView EventName, Lua::FRef Callback);

        /** Remove a specific callback from the named event (global or entity-scoped). */
        void Unsubscribe(FStringView EventName, const Lua::FRef& Callback);
        
        /**
         * Immediately invoke all listeners registered for EventName.
         * Payload may be a Lua table, a primitive, or nil.
         */
        void Dispatch(FStringView EventName, const Lua::FRef& Payload);

        /**
         * Like Dispatch, but only invoke listeners whose Owner == Target. Used to deliver
         * entity-targeted events (e.g. collisions) to the owning script without leaking
         * to every other subscriber of the same name.
         */
        void DispatchToEntity(entt::entity Target, FStringView EventName, const Lua::FRef& Payload);

        /**
         * Queue an event to be dispatched at the beginning of the next frame
         * (during CWorld::Update FrameStart stage). Safe to call from within a
         * Dispatch callback without causing re-entrant iteration.
         */
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
        };

        struct FDeferredEvent
        {
            FName           EventName;
            Lua::FRef       Payload;
        };

        static void DispatchListeners(TVector<FListener>& Listeners, const Lua::FRef& Payload);

        THashMap<FName, TVector<FListener>>  Subscriptions;
        TVector<FDeferredEvent>              DeferredQueue;
    };
}
