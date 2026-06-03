#pragma once
#include "Core/Object/ObjectMacros.h"
#include "Core/Math/Math.h"
#include "World/Net/NetGUID.h"
#include "World/Net/NetRole.h"
#include "NetworkComponent.generated.h"

namespace Lumina
{
    // Marks an entity for networking. Authored fields (bReplicates/...) serialize with the world; the
    // runtime net state (NetGUID/LocalRole/RemoteRole/OwningConnectionId) is derived per-peer by
    // SNetworkSystem -- exposed ReadOnly + NoSerialize so it shows in details for debugging but is never saved.
    REFLECT(Component, Category = "Networking")
    struct RUNTIME_API SNetworkComponent
    {
        GENERATED_BODY()

        /** When false the entity has a stable identity but is not replicated. */
        PROPERTY(Editable, Category = "Networking")
        bool bReplicates = true;

        /** Relevant to every connection (skips relevancy culling once that exists). */
        PROPERTY(Editable, Category = "Networking")
        bool bAlwaysRelevant = false;

        /** Replicate this entity's transform. The server only sends it on frames where it actually moved. */
        PROPERTY(Editable, Category = "Networking")
        bool bReplicatesMovement = true;

        /** How many times per second this entity's movement is sent (caps the send rate; clients interpolate
         *  between updates). <= 0 sends every tick. Replicated so the owning client throttles its sends to match. */
        PROPERTY(Editable, Replicated, Category = "Networking")
        float NetUpdateFrequency = 30.0f;

        //~ Runtime net state (derived per-peer; not serialized). Read-only debug visibility.

        /** Stable network identity, resolved per peer. */
        PROPERTY(ReadOnly, NoSerialize, Category = "Networking|Debug")
        FNetGUID NetGUID;

        /** This peer's role: Authority on the server, Autonomous/Simulated proxy on clients. */
        PROPERTY(ReadOnly, NoSerialize, Category = "Networking|Debug")
        ENetRole LocalRole = ENetRole::None;

        /** The far end's role (Authority as seen from a client; the proxy role as seen from the server). */
        PROPERTY(ReadOnly, NoSerialize, Category = "Networking|Debug")
        ENetRole RemoteRole = ENetRole::None;

        /** Owning connection (FConnectionHandle::Value); 0 = server-owned / unowned. */
        PROPERTY(ReadOnly, NoSerialize, Category = "Networking|Debug")
        uint32 OwningConnectionId = 0;

        // Client-only latch: a proxy's physics body has been switched to Kinematic so the local sim no
        // longer fights replicated transforms. Set once the body exists.
        bool     bProxyPhysicsConfigured = false;

        // Server-only movement-replication cache: the last local pose actually sent, so a snapshot
        // includes only entities whose transform changed since. Invalid until the first send.
        FVector3 LastSentLocation;
        FQuat    LastSentRotation;
        bool     bMovementCacheValid = false;

        // Send-rate throttle accumulator (seconds since this entity's transform was last sent). Used by the
        // server's snapshot and the owning client's upstream send to honor NetUpdateFrequency.
        float    TimeSinceLastNetUpdate = 0.0f;
    };
}
