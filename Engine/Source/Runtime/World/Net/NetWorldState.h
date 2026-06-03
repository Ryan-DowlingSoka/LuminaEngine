#pragma once

#include "Memory/SmartPtr.h"
#include "Containers/Array.h"
#include "Core/Math/Math.h"
#include "Networking/INetworkTransport.h"
#include "World/Net/NetGUID.h"

namespace Lumina
{
    // One timestamped transform sample for client-side snapshot interpolation. Time is the SERVER time the
    // pose represents (the client maps it to local time via FNetWorldState::ClockOffset).
    struct FNetInterpSample
    {
        double   Time = 0.0;
        FVector3 Pos;
        FQuat    Rot;
    };

    // Per-entity ring of recent samples for a SimulatedProxy. The client renders InterpDelay behind the
    // newest server time, slerp/lerp-ing between the two bracketing samples -> smooth movement decoupled
    // from the (throttled) send rate, and self-healing across dropped packets.
    struct FNetInterpState
    {
        static constexpr int Capacity = 12;
        FNetInterpSample Samples[Capacity];
        int Count = 0; // valid samples
        int Head  = 0; // physical index of the oldest

        FNetInterpSample& Logical(int Index) { return Samples[(Head + Index) % Capacity]; }

        void Push(double Time, const FVector3& Pos, const FQuat& Rot)
        {
            if (Count < Capacity)
            {
                Samples[(Head + Count) % Capacity] = { Time, Pos, Rot };
                ++Count;
            }
            else
            {
                Samples[Head] = { Time, Pos, Rot }; // overwrite oldest; it becomes the newest
                Head = (Head + 1) % Capacity;
            }
        }

        // Pose at RenderTime: clamps at the ends (hold), lerp/slerp between bracketing samples otherwise.
        void Evaluate(double RenderTime, FVector3& OutPos, FQuat& OutRot)
        {
            if (Count == 1 || RenderTime <= Logical(0).Time)
            {
                OutPos = Logical(0).Pos; OutRot = Logical(0).Rot; return;
            }
            if (RenderTime >= Logical(Count - 1).Time)
            {
                OutPos = Logical(Count - 1).Pos; OutRot = Logical(Count - 1).Rot; return;
            }
            for (int i = 0; i < Count - 1; ++i)
            {
                FNetInterpSample& A = Logical(i);
                FNetInterpSample& B = Logical(i + 1);
                if (RenderTime >= A.Time && RenderTime <= B.Time)
                {
                    const double Span = B.Time - A.Time;
                    const float  T    = (Span > 1e-9) ? static_cast<float>((RenderTime - A.Time) / Span) : 0.0f;
                    OutPos = Math::Mix(A.Pos, B.Pos, T);
                    OutRot = Math::Slerp(A.Rot, B.Rot, T);
                    return;
                }
            }
            OutPos = Logical(Count - 1).Pos; OutRot = Logical(Count - 1).Rot;
        }
    };

    // The server's reserved peer id (Unreal-style: the server is the authority, owns connection 0).
    inline constexpr uint32 ServerPeerId = 0;

    // Leading byte on every Data-channel packet so the receiver can route it.
    enum class ENetMessage : uint8
    {
        TransformSnapshot = 1,
        ScriptRpc         = 2,
        AssignPeerId      = 3, // server -> a client: "your unique peer id is N" (its connection handle)
        OwnershipUpdate   = 4, // server -> clients: { count, (NetGUID, OwningConnectionId)... }
        SpawnEntity       = 5, // server -> clients: { NetGUID, OwningConnectionId, serialized components }
        DespawnEntity     = 6, // server -> clients: { NetGUID }
        PropertyUpdate    = 7, // server -> clients: { NetGUID, serialized replicated components } (reliable)
        ClientTransform   = 8, // client -> server: { Count, (NetGUID, packed pos/rot)... } for entities it owns
        Welcome           = 9, // server -> a client: { uint16 len, map path } -- which level to load
        ClientReady       = 10,// client -> server: "I've loaded the map, send me the world" -> re-baseline
    };

    // Per-world networking state.
    struct FNetWorldState
    {
        TUniquePtr<INetworkTransport> Transport;

        // Client side: the link to the server (invalid until ConnectToServer succeeds).
        FConnectionHandle             ServerConnection;

        // Server side: count + connection handles of currently-connected clients.
        int32                         ConnectedClients = 0;
        TVector<uint32>               ConnectedClientIds;

        // This peer's unique id (Unreal GetNetMode/Godot get_unique_id equivalent): ServerPeerId on the
        // server; on a client, the connection handle the server assigned (via ENetMessage::AssignPeerId).
        uint32                        LocalPeerId = ServerPeerId;

        // Server side: re-broadcast the ownership table next tick (an entity's owner changed, or a client
        // joined and needs the current owners).
        bool                          bOwnershipDirty = true;

        // Client side: set once the link to the server is established.
        bool                          bClientConnected = false;

        // Server side: force the next transform snapshot to include every movement-replicating entity
        // (a full baseline), regardless of the per-entity change cache. Set on init and on each client
        // connect so a late joiner receives current poses even for entities that have stopped moving.
        bool                          bForceMovementResend = true;

        // Server side: seconds since the last full keyframe. Periodically re-arms bForceMovementResend so
        // a dropped delta self-heals (transform replication is state, not events -> eventual convergence).
        float                         TimeSinceKeyframe = 0.0f;

        // NetGUID <-> entity map for this world. Stable (pre-placed) ids are adopted once at init.
        FNetGUIDTable                 GuidTable;

        // Server side: dynamic (runtime-spawned) NetGUIDs already sent to clients. Diffed each tick against
        // the live set to emit SpawnEntity (new) / DespawnEntity (gone). Pre-placed ids are excluded.
        TVector<uint32>               KnownSpawnedGuids;
        
        bool                          bInitialized = false;
        bool                          bStableEntitiesAdopted = false;

        // Client side: ClientReady sent to the server once the link is up (asks for a full world baseline).
        bool                          bClientReadySent = false;

        //~ Client-side snapshot interpolation for SimulatedProxy movement.

        // Per-NetGUID sample rings. Only SimulatedProxy entities are buffered (the owner predicts/owns its
        // own; the server runs live). Pruned when the entity is gone.
        THashMap<uint32, FNetInterpState> InterpStates;

        // Newest server time seen across all snapshots, and the running estimate of (serverClock -
        // clientClock). RenderTime = clientNow + ClockOffset - InterpDelay, advanced smoothly each frame.
        double                        LatestServerTime = 0.0;
        double                        ClockOffset      = 0.0;
        bool                          bClockInitialized = false;
        double                        InterpDelay      = 0.1; // seconds rendered behind the newest server time
    };
}
