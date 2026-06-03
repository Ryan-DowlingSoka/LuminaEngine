#pragma once

#include "Memory/SmartPtr.h"
#include "Containers/Array.h"
#include "Containers/String.h"
#include "Core/Math/Math.h"
#include "GUID/GUID.h"
#include "Assets/AssetRef.h"
#include "Networking/INetworkTransport.h"
#include "World/Net/NetGUID.h"

namespace Lumina
{
    class CObject;
    // One timestamped transform sample for client snapshot interpolation. Time is server time.
    struct FNetInterpSample
    {
        double   Time = 0.0;
        FVector3 Pos;
        FQuat    Rot;
    };

    // Per-entity ring of recent samples for a SimulatedProxy. Client renders InterpDelay behind newest
    // server time, lerping between bracketing samples.
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
                Samples[Head] = { Time, Pos, Rot }; // overwrite oldest
                Head = (Head + 1) % Capacity;
            }
        }

        // Pose at RenderTime. Clamps at the ends, lerps between bracketing samples otherwise.
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

    // The server's reserved peer id (authority, owns connection 0).
    inline constexpr uint32 ServerPeerId = 0;

    // Leading byte on every Data-channel packet so the receiver can route it.
    enum class ENetMessage : uint8
    {
        TransformSnapshot = 1,
        ScriptRpc         = 2,
        AssignPeerId      = 3, // server to client, assigns its peer id
        OwnershipUpdate   = 4, // server to clients, ownership table
        SpawnEntity       = 5, // server to clients, spawn with components
        DespawnEntity     = 6, // server to clients, despawn
        PropertyUpdate    = 7, // server to clients, replicated components (reliable)
        ClientTransform   = 8, // client to server, owned-entity transforms
        Welcome           = 9, // server to client, which level to load
        ClientReady       = 10,// client to server, map loaded, request baseline
        ObjectExport      = 11,// CObject net-index map
        AssetExport       = 12,// FAssetRef net-index map
    };

    // CObject-ref net-index cache. A referenced object is sent as a compact varint index; the index/GUID
    // map is exported once over a reliable stream and replayed to late joiners. One struct, both directions.
    struct FNetObjectMap
    {
        THashMap<CObject*, uint32> ObjToIndex;     // outgoing object index
        THashMap<uint32, FGuid>    IndexToGuid;    // outgoing on assign, incoming from export
        THashMap<uint32, CObject*> IndexToObject;  // incoming resolved cache
        TVector<uint32>            PendingExports; // outgoing, assigned but not yet sent
        uint32                     NextIndex = 1;  // 0 is null
    };

    // FAssetRef net-index cache. Same export-once model as FNetObjectMap, keyed by GUID else Path.
    struct FNetAssetMap
    {
        THashMap<FString, uint32>   KeyToIndex;     // outgoing asset key index
        THashMap<uint32, FAssetRef> IndexToRef;     // outgoing on assign, incoming from export
        TVector<uint32>             PendingExports; // outgoing, assigned but not yet sent
        uint32                      NextIndex = 1;  // 0 is null
    };

    // Per-world networking state.
    struct FNetWorldState
    {
        TUniquePtr<INetworkTransport> Transport;

        // Client side, link to the server. Invalid until ConnectToServer succeeds.
        FConnectionHandle             ServerConnection;

        // Server side, connected clients.
        int32                         ConnectedClients = 0;
        TVector<uint32>               ConnectedClientIds;

        // This peer's unique id. ServerPeerId on the server, the assigned handle on a client.
        uint32                        LocalPeerId = ServerPeerId;

        // Server side, re-broadcast the ownership table next tick.
        bool                          bOwnershipDirty = true;

        // Client side, set once the link to the server is established.
        bool                          bClientConnected = false;

        // Server side, force the next transform snapshot to be a full baseline. Set on init and on each
        // client connect so late joiners get current poses for stopped entities.
        bool                          bForceMovementResend = true;

        // Server side, seconds since the last full keyframe. Periodically re-arms bForceMovementResend so
        // a dropped delta self-heals.
        float                         TimeSinceKeyframe = 0.0f;

        // NetGUID to entity map for this world. Stable ids are adopted once at init.
        FNetGUIDTable                 GuidTable;

        // Server side, dynamic NetGUIDs already sent to clients. Diffed each tick to emit spawn/despawn.
        TVector<uint32>               KnownSpawnedGuids;

        // Server side, adopted stable NetGUIDs still live. Diffed each tick so a level entity destroyed at
        // runtime is despawned for clients.
        TVector<uint32>               KnownStableGuids;

        // Server side, stable NetGUIDs destroyed at runtime, replayed to late joiners so they remove copies.
        TVector<uint32>               DestroyedStableGuids;

        bool                          bInitialized = false;
        bool                          bStableEntitiesAdopted = false;

        // Client side, ClientReady sent to the server once the link is up.
        bool                          bClientReadySent = false;

        //~ Client snapshot interpolation for SimulatedProxy movement.

        // Per-NetGUID sample rings. Only SimulatedProxy entities are buffered. Pruned when the entity is gone.
        THashMap<uint32, FNetInterpState> InterpStates;

        // Newest server time seen, and the running serverClock minus clientClock estimate.
        double                        LatestServerTime = 0.0;
        double                        ClockOffset      = 0.0;
        bool                          bClockInitialized = false;
        double                        InterpDelay      = 0.1; // seconds behind newest server time

        // Net-index caches. Outgoing maps this peer mints and exports; incoming maps are kept per sender
        // connection id since the index space is sender-owned.
        FNetObjectMap                    OutObjects;
        FNetAssetMap                     OutAssets;
        THashMap<uint32, FNetObjectMap>  InObjects;
        THashMap<uint32, FNetAssetMap>   InAssets;
    };
}
