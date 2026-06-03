#pragma once

#include "Platform/GenericPlatform.h"
#include "Containers/Array.h"
#include "Containers/Function.h"
#include "entt/entt.hpp"

namespace Lumina
{
    class FNetArchive;
    class INetworkTransport;
    class CObject;
    struct FConnectionHandle;
    struct FNetObjectMap;
    struct FNetAssetMap;
    struct FNetWorldState;
    enum class ESendMode : uint8;

    // Tag, this networked entity has replicated property changes pending (set by World.Net:MarkDirty).
    // The server sends a reliable PropertyUpdate for it next tick, then clears the tag.
    struct FNetDirty {};

    namespace Net
    {
        // Reflection-driven entity replication. The same generic path the world serializer uses, keyed by a
        // compact type hash, carrying only Replicated properties. Works for any REFLECT(Component).

        // Server, write one entity's replicated components. Used for both Spawn and PropertyUpdate.
        void WriteEntityComponents(FNetArchive& Ar, entt::registry& Registry, entt::entity Entity);

        // Client, recreate/refresh components on Entity.
        void ReadEntityComponents(FNetArchive& Ar, entt::registry& Registry, entt::entity Entity);

        //~ Packet batching. Many small messages per tick are concatenated into one length-prefixed datagram,
        //~ one ENet header and ack instead of N. Every packet on the wire is a batch.

        // Append one framed message to a batch buffer.
        void AppendFramedMessage(TVector<uint8>& Batch, const uint8* Msg, SIZE_T MsgSize);

        // Send a single message as a 1-message batch (RPC, peer-id handshake).
        void SendFramed(INetworkTransport& Transport, FConnectionHandle Connection, const uint8* Msg, SIZE_T MsgSize, uint8 Channel, ESendMode Mode);
        void BroadcastFramed(INetworkTransport& Transport, const uint8* Msg, SIZE_T MsgSize, uint8 Channel, ESendMode Mode);

        // Split a received batch into its framed messages.
        void ForEachFramedMessage(const uint8* Data, SIZE_T Size, const TFunction<void(const uint8*, SIZE_T)>& Fn);

        //~ Net-index caches. A CObject/FAssetRef reference is sent as a compact varint index; identity is
        //~ exported once and resolved in the sender's index space. Incoming maps are per-connection.

        // Bind both index caches on an archive. Writer uses the peer's outgoing maps, reader the sender's
        // incoming maps. Used for component + RPC-arg replication.
        void BindWriters(FNetArchive& Ar, FNetWorldState& State);
        void BindReaders(FNetArchive& Ar, FNetWorldState& State, uint32 SenderConn);

        // Build/apply the index-to-identity export messages (reliable). Apply ingests into the sender's map.
        void BuildObjectExport(const FNetObjectMap& Map, const TVector<uint32>& Indices, TVector<uint8>& OutMsg);
        void ApplyObjectExport(FNetObjectMap& Map, const uint8* Data, SIZE_T Size);
        void BuildAssetExport(const FNetAssetMap& Map, const TVector<uint32>& Indices, TVector<uint8>& OutMsg);
        void ApplyAssetExport(FNetAssetMap& Map, const uint8* Data, SIZE_T Size);
    }
}
