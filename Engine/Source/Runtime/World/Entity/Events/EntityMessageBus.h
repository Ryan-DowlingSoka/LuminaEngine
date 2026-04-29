#pragma once

#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Scripting/Lua/Reference.h"
#include "entt/entt.hpp"

namespace Lumina
{
    class CWorld;

    /**
     * Directed message bus for entity-to-entity script communication.
     *
     * Where FLuaEventBus is broadcast/anonymous (any subscriber to a name receives every
     * dispatch), this bus routes a message to a single specific entity. The receiving
     * script declares a handler by convention -- a method whose name starts with "On" --
     * and the bus invokes it with the script self table plus the payload.
     *
     * Lua API (available as the "Messages" global inside every script):
     *
     *     Messages:Send(target, "OnDamage", { Amount = 10, Source = self.Entity })
     *     Messages:SendDeferred(target, "OnGameTick", payload)  -- fires next FrameStart
     *
     * Receiver:
     *
     *     function self:OnDamage(payload)
     *         self.Health -= payload.Amount
     *     end
     *
     * Handlers are discovered once during script attach (see CWorld::OnScriptComponentCreated)
     * and stored on SScriptComponent. Send is therefore O(1): one entt component lookup +
     * one hashmap probe + one Lua coroutine invoke.
     */
    class FEntityMessageBus
    {
    public:

        /** Bind to the owning world. Called once from CWorld::InitializeWorld. */
        void SetWorld(CWorld* InWorld) { World = InWorld; }

        /**
         * Synchronously dispatch MessageName to Target's script handler, if any.
         * Silently no-ops if Target is invalid, has no SScriptComponent, or doesn't
         * implement a handler with that name -- the sender shouldn't have to care.
         */
        void Send(entt::entity Target, FStringView MessageName, Lua::FRef Payload);

        /**
         * Queue a message for dispatch at the start of next frame. Safe to call from
         * inside a handler without re-entering the receiver's stack.
         */
        void SendDeferred(entt::entity Target, FStringView MessageName, Lua::FRef Payload);

        /** Drain the deferred queue. Called from CWorld::Update on FrameStart. */
        void ProcessDeferred();

        /** Drop all queued messages. Called from CWorld::TeardownWorld. */
        void Clear();

    private:

        struct FDeferredMessage
        {
            entt::entity    Target = entt::null;
            FName           Name;
            Lua::FRef       Payload;
        };

        CWorld*                     World = nullptr;
        TVector<FDeferredMessage>   DeferredQueue;
    };
}
