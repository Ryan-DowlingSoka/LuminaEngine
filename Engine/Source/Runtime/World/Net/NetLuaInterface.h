#pragma once

#include "Platform/GenericPlatform.h"
#include "Containers/Array.h"
#include "entt/entt.hpp"

namespace Lumina
{
    class CWorld;

    // Lua-facing facade for the World.Net namespace. One instance lives on each CWorld and is bound
    // under World.Net (see World_SubsystemIndex). Queries route to this world's net mode + state.
    struct FNetLuaInterface
    {
        CWorld* World = nullptr;

        bool  IsServer() const;       // listen or dedicated server (the authority)
        bool  IsClient() const;
        bool  IsStandalone() const;   // not networked
        bool  IsNetworked() const;    // client or server
        bool  IsConnected() const;    // server: >=1 client; client: link to server established
        int32 GetConnectedClients() const;
        
        /** This peer's unique id: 0 (ServerPeerId) on the server; the server-assigned id on a client. */
        uint32 GetUniqueId() const;

        /** Server-side: connected clients, addressed by index (numeric loop -> GetConnectionAt). */
        int32  GetConnectionCount() const;
        uint32 GetConnectionAt(int32 Index) const;

        bool  HasAuthority(entt::entity Entity) const;   // LocalRole == Authority
        bool  IsLocallyOwned(entt::entity Entity) const; // LocalRole == AutonomousProxy (this peer controls it)
        int32 GetLocalRole(entt::entity Entity) const;   // ENetRole as int
        int32 GetRemoteRole(entt::entity Entity) const;
        uint32 GetOwner(entt::entity Entity) const;      // OwningConnectionId

        /** Server-only: set the owning connection of an entity (queues an ownership replication). */
        void SetOwner(entt::entity Entity, uint32 ConnectionId);

        /** Server-only: flag an entity's replicated properties dirty -> a reliable PropertyUpdate next tick. */
        void MarkDirty(entt::entity Entity);

        /** The entity THIS peer controls (its AutonomousProxy), or null. Lets a client find "my pawn". */
        entt::entity GetLocallyOwnedEntity() const;

        //~ Connection control (engine-level; deferred to the next frame). Port <= 0 defaults to 7777.

        /** Host the given map as a listen server. */
        void Host(FStringView Map, int32 Port) const;

        /** Connect to Host:Port as a client; the server tells us which level to load. */
        void Connect(FStringView Host, int32 Port) const;
    };
}
